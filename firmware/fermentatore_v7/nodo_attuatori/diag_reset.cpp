#include "diag_reset.h"
#include <esp_rom_sys.h>   // esp_rom_get_reset_reason
#include <soc/rtc.h>       // rtc_time_get, rtc_clk_slow_freq_get_hz
#include <stddef.h>

// Il segnale di vita in RTC si aggiorna al massimo ogni DIAG_RTC_PERIODO_MS: rtc_time_get() costa qualche
// microsecondo e il loop gira a migliaia di giri al secondo.
#define DIAG_RTC_PERIODO_MS 100UL

static const uint32_t DIAG_MAGIC = 0x44494147UL;  // "DIAG"

// Dati sopravvissuti al reset. Ordine dei campi: niente padding, `check` per ultimo.
struct DiagRtc {
  uint64_t giro_ticks;     // rtc_time_get() dell'ultimo segnale di vita del loop
  uint64_t comm_ticks;     // rtc_time_get() dell'ultima commutazione di un relè
  uint32_t magic;
  uint32_t freq_hz;        // frequenza nominale del clock RTC lento al momento del salvataggio
  uint32_t loop_max_ms;
  uint8_t  comm_rele;
  uint8_t  comm_acceso;
  uint8_t  loop_max_sez;
  uint8_t  riserva;
  uint32_t check;
};
static_assert(offsetof(DiagRtc, check) % 4 == 0, "check deve essere allineato a 32 bit");
static_assert(offsetof(DiagRtc, check) == 32, "DiagRtc: padding inatteso");

static RTC_NOINIT_ATTR DiagRtc g_diag_rtc;
// La sezione cambia a ogni chiamata: stesso valore e suo complemento, senza ricalcolare il checksum.
static RTC_NOINIT_ATTR uint8_t g_diag_rtc_sez;
static RTC_NOINIT_ATTR uint8_t g_diag_rtc_sez_inv;

static DiagReset g_diag_prec = {};
static DiagLoop  g_diag_loop = {};

static unsigned long g_diag_prev_giro_ms = 0;      // fine del giro precedente (0 = primo giro)
static unsigned long g_diag_ultimo_rtc_ms = 0;
static uint8_t       g_diag_sez = SEZ_NESSUNA;
static unsigned long g_diag_sez_inizio_ms = 0;
static uint32_t      g_diag_sez_max_giro_ms = 0;   // sezione più lunga del giro in corso
static uint8_t       g_diag_sez_max_giro = SEZ_NESSUNA;

// ============================================================
// Nomi
// ============================================================
const char* diag_reset_nome_hw(uint32_t codice) {
  // Nomi stampati dalla ROM dell'ESP32 ("rst:0x.. (NOME)"); sull'S3 i codici principali coincidono.
  switch (codice) {
    case 1:  return "POWERON_RESET";
    case 3:  return "SW_RESET";
    case 4:  return "OWDT_RESET";
    case 5:  return "DEEPSLEEP_RESET";
    case 6:  return "SDIO_RESET";
    case 7:  return "TG0WDT_SYS_RESET";
    case 8:  return "TG1WDT_SYS_RESET";
    case 9:  return "RTCWDT_SYS_RESET";
    case 10: return "INTRUSION_RESET";
    case 11: return "TGWDT_CPU_RESET";
    case 12: return "SW_CPU_RESET";
    case 13: return "RTCWDT_CPU_RESET";
    case 14: return "EXT_CPU_RESET";
    case 15: return "RTCWDT_BROWN_OUT_RESET";
    case 16: return "RTCWDT_RTC_RESET";
    default: return "SCONOSCIUTO";
  }
}

const char* diag_reset_nome_sezione(uint8_t s) {
  switch (s) {
    case SEZ_NESSUNA:    return "nessuna";
    case SEZ_BOOT:       return "boot";
    case SEZ_SERIALE:    return "seriale";
    case SEZ_RETE:       return "rete";
    case SEZ_SENSORE:    return "sensore";
    case SEZ_DISPLAY:    return "display";
    case SEZ_CONTROLLO:  return "controllo";
    case SEZ_USCITE:     return "uscite";
    case SEZ_HEARTBEAT:  return "heartbeat";
    case SEZ_NVS:        return "nvs";
    default:             return "ignota";
  }
}

const char* diag_reset_nome_rele(char rele) {
  switch (rele) {
    case 'C': return "caldo";
    case 'F': return "freddo";
    default:  return "";
  }
}

// ============================================================
// Integrità e conversioni
// ============================================================
static uint32_t calcola_check(const DiagRtc& r) {
  const uint32_t* w = reinterpret_cast<const uint32_t*>(&r);
  const size_t n = offsetof(DiagRtc, check) / sizeof(uint32_t);
  uint32_t c = 0x5A5A5A5AUL;
  for (size_t i = 0; i < n; i++) {
    c = ((c << 5) | (c >> 27)) ^ w[i];
  }
  return c;
}

static void salva_rtc() {
  g_diag_rtc.check = calcola_check(g_diag_rtc);
}

// Differenza di tick RTC in millisecondi; -1 se il contatore è tornato indietro.
static int32_t tick_a_ms(uint64_t ora, uint64_t prima, uint32_t freq_hz) {
  if (ora < prima || freq_hz == 0) return -1;
  const uint64_t ms = ((ora - prima) * 1000ULL) / freq_hz;
  return ms > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int32_t)ms;
}

// ============================================================
// Ciclo di vita
// ============================================================
void diag_reset_init() {
  const uint64_t ora = rtc_time_get();
  g_diag_prec = {};
  g_diag_prec.hw_codice = (uint8_t)esp_rom_get_reset_reason(0);
  g_diag_prec.hw_nome = diag_reset_nome_hw(g_diag_prec.hw_codice);
  g_diag_prec.giro_ms = -1;
  g_diag_prec.comm_ms = -1;
  g_diag_prec.sezione = SEZ_IGNOTA;
  g_diag_prec.loop_max_sez = SEZ_IGNOTA;

  const bool integra = g_diag_rtc.magic == DIAG_MAGIC && g_diag_rtc.check == calcola_check(g_diag_rtc) &&
                       g_diag_rtc.freq_hz > 0;
  g_diag_prec.valida = integra;
  if (integra) {
    g_diag_prec.rtc_azzerato = ora < g_diag_rtc.giro_ticks;
    g_diag_prec.giro_ms = tick_a_ms(ora, g_diag_rtc.giro_ticks, g_diag_rtc.freq_hz);
    if (g_diag_rtc.comm_rele != 0) {
      g_diag_prec.comm_ms = tick_a_ms(ora, g_diag_rtc.comm_ticks, g_diag_rtc.freq_hz);
      g_diag_prec.comm_rele = (char)g_diag_rtc.comm_rele;
      g_diag_prec.comm_acceso = g_diag_rtc.comm_acceso != 0;
    }
    if ((uint8_t)~g_diag_rtc_sez_inv == g_diag_rtc_sez && g_diag_rtc_sez < SEZ_N) {
      g_diag_prec.sezione = g_diag_rtc_sez;
    }
    g_diag_prec.loop_max_ms = g_diag_rtc.loop_max_ms;
    g_diag_prec.loop_max_sez = g_diag_rtc.loop_max_sez < SEZ_N ? g_diag_rtc.loop_max_sez : (uint8_t)SEZ_IGNOTA;
  }

  // Nuova registrazione per il boot che inizia.
  g_diag_rtc = {};
  g_diag_rtc.magic = DIAG_MAGIC;
  g_diag_rtc.giro_ticks = ora;
  g_diag_rtc.freq_hz = rtc_clk_slow_freq_get_hz();
  salva_rtc();

  g_diag_loop = {};
  g_diag_prev_giro_ms = 0;
  g_diag_ultimo_rtc_ms = millis();
  g_diag_sez_max_giro_ms = 0;
  g_diag_sez_max_giro = SEZ_NESSUNA;
  g_diag_sez = SEZ_BOOT;
  g_diag_sez_inizio_ms = millis();
  g_diag_rtc_sez = SEZ_BOOT;
  g_diag_rtc_sez_inv = (uint8_t)~SEZ_BOOT;
}

void diag_reset_boot_completato() {
  g_diag_loop.setup_ms = millis();
  g_diag_prev_giro_ms = millis();
  diag_reset_sezione(SEZ_NESSUNA);
}

void diag_reset_sezione(DiagSezione s) {
  const unsigned long ora = millis();
  const uint32_t durata = (uint32_t)(ora - g_diag_sez_inizio_ms);
  if (durata > g_diag_sez_max_giro_ms) {
    g_diag_sez_max_giro_ms = durata;
    g_diag_sez_max_giro = g_diag_sez;
  }
  g_diag_sez = (uint8_t)s;
  g_diag_sez_inizio_ms = ora;
  g_diag_rtc_sez = (uint8_t)s;
  g_diag_rtc_sez_inv = (uint8_t)~s;
}

void diag_reset_giro() {
  diag_reset_sezione(SEZ_NESSUNA);
  const unsigned long ora = millis();

  if (g_diag_prev_giro_ms != 0) {
    const uint32_t gap = (uint32_t)(ora - g_diag_prev_giro_ms);
    if (gap >= 1000UL) g_diag_loop.lenti_1s++;
    if (gap >= 10000UL) g_diag_loop.lenti_10s++;
    if (gap > g_diag_loop.max_ms) {
      g_diag_loop.max_ms = gap;
      g_diag_loop.max_sez = g_diag_sez_max_giro;
      g_diag_rtc.loop_max_ms = gap;
      g_diag_rtc.loop_max_sez = g_diag_sez_max_giro;
      salva_rtc();
    }
  }
  g_diag_prev_giro_ms = ora;
  g_diag_sez_max_giro_ms = 0;
  g_diag_sez_max_giro = SEZ_NESSUNA;

  if (ora - g_diag_ultimo_rtc_ms >= DIAG_RTC_PERIODO_MS) {
    g_diag_ultimo_rtc_ms = ora;
    g_diag_rtc.giro_ticks = rtc_time_get();
    g_diag_rtc.freq_hz = rtc_clk_slow_freq_get_hz();
    salva_rtc();
  }
}

void diag_reset_commutazione(char rele, bool acceso) {
  g_diag_rtc.comm_ticks = rtc_time_get();
  g_diag_rtc.comm_rele = (uint8_t)rele;
  g_diag_rtc.comm_acceso = acceso ? 1 : 0;
  salva_rtc();
}

const DiagReset& diag_reset_precedente() { return g_diag_prec; }
const DiagLoop&  diag_reset_loop() { return g_diag_loop; }
