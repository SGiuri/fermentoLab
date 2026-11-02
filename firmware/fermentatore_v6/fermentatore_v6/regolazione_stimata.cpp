#include "regolazione_stimata.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

// Apprendimento persistito: RTC (sopravvive ai riavvii software) e NVS (sopravvive al blackout).
struct StimaPersistita {
  uint32_t magic;
  float    u_freddo;      // frazione 0..1
  float    u_caldo;
  float    t_media;
  float    pendenza_c_h;
  uint32_t blocchi;
  uint32_t checksum;
};

static const uint32_t STIMA_MAGIC = 0x46365354UL;  // "F6ST"
static const char*    NVS_KEY_STIMA = "stima";

static RTC_NOINIT_ATTR StimaPersistita g_rtc_stima;
static RTC_NOINIT_ATTR uint32_t g_rtc_secondi_attiva;
static RTC_NOINIT_ATTR uint32_t g_rtc_secondi_check;  // = ~g_rtc_secondi_attiva se integro

struct Blocco {
  float u_freddo;
  float u_caldo;
  float t_media;
};

static Blocco   g_blocchi[STIMA_BLOCCHI_MAX];
static uint8_t  g_testa = 0;       // prossimo blocco da scrivere
static uint8_t  g_contigui = 0;    // blocchi validi contigui più recenti (<= STIMA_BLOCCHI_MAX)

// Blocco in corso
static uint32_t g_acc_ms = 0;
static uint32_t g_acc_freddo_ms = 0;
static uint32_t g_acc_caldo_ms = 0;
static double   g_acc_temp_ms = 0.0;
static bool     g_acc_valido = true;

static unsigned long g_ts_prec_ms = 0;
static unsigned long g_ts_inizio_attiva_ms = 0;
static uint32_t      g_resto_attiva_ms = 0;
static unsigned long g_ts_nvs_ms = 0;
static bool          g_nvs_da_salvare = false;

static StimaPersistita g_stima = {};
static StatoStima      g_stato = {};

static uint32_t checksum(const StimaPersistita& s) {
  // FNV-1a sui campi prima del checksum
  const uint8_t* b = reinterpret_cast<const uint8_t*>(&s);
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < offsetof(StimaPersistita, checksum); i++) {
    h ^= b[i];
    h *= 16777619UL;
  }
  return h;
}

static bool stima_valida(const StimaPersistita& s) {
  return s.magic == STIMA_MAGIC && s.checksum == checksum(s) &&
         isfinite(s.u_freddo) && s.u_freddo >= 0.0f && s.u_freddo <= 1.0f &&
         isfinite(s.u_caldo) && s.u_caldo >= 0.0f && s.u_caldo <= 1.0f &&
         isfinite(s.t_media) && s.t_media > -5.0f && s.t_media < 40.0f &&
         isfinite(s.pendenza_c_h) && fabsf(s.pendenza_c_h) <= STIMA_PENDENZA_MAX_C_H;
}

void stima_calcola_duty(float u_freddo, float u_caldo, float t_media, float pendenza_c_h,
                        float setpoint_c, float* duty_freddo_pct, float* duty_caldo_pct) {
  // Potenza netta di raffreddamento [°C/h] che terrebbe la temperatura al setpoint
  const float p = u_freddo * STIMA_R_FREDDO_C_H - u_caldo * STIMA_R_CALDO_C_H +
                  pendenza_c_h + (t_media - setpoint_c) / STIMA_TAU_H;
  float df = 0.0f;
  float dc = 0.0f;
  if (p >= 0.0f) {
    df = 100.0f * p / STIMA_R_FREDDO_C_H;
  } else {
    dc = 100.0f * -p / STIMA_R_CALDO_C_H;
  }
  if (!(df >= 0.0f)) df = 0.0f;  // anche NaN
  if (!(dc >= 0.0f)) dc = 0.0f;
  if (df > STIMA_DUTY_MAX_PCT) df = STIMA_DUTY_MAX_PCT;
  if (dc > STIMA_DUTY_MAX_PCT) dc = STIMA_DUTY_MAX_PCT;
  *duty_freddo_pct = df;
  *duty_caldo_pct = dc;
}

static void pubblica_apprendimento() {
  g_stato.pronta = stima_valida(g_stima);
  g_stato.u_freddo_pct = g_stato.pronta ? 100.0f * g_stima.u_freddo : 0.0f;
  g_stato.u_caldo_pct = g_stato.pronta ? 100.0f * g_stima.u_caldo : 0.0f;
  g_stato.t_media = g_stato.pronta ? g_stima.t_media : 0.0f;
  g_stato.pendenza_c_h = g_stato.pronta ? g_stima.pendenza_c_h : 0.0f;
}

static void salva_nvs() {
  Preferences prefs;
  prefs.begin(NVS_NS_FERM, false);
  prefs.putBytes(NVS_KEY_STIMA, &g_stima, sizeof(g_stima));
  prefs.end();
}

static void imposta_secondi_attiva(uint32_t s) {
  g_rtc_secondi_attiva = s;
  g_rtc_secondi_check = ~s;
}

void stima_init() {
  memset(g_blocchi, 0, sizeof(g_blocchi));
  g_testa = 0;
  g_contigui = 0;
  g_acc_ms = g_acc_freddo_ms = g_acc_caldo_ms = 0;
  g_acc_temp_ms = 0.0;
  g_acc_valido = true;
  g_ts_prec_ms = 0;
  g_ts_inizio_attiva_ms = 0;
  g_resto_attiva_ms = 0;
  g_ts_nvs_ms = 0;
  g_nvs_da_salvare = false;
  g_stato = {};

  const esp_reset_reason_t rr = esp_reset_reason();
  const bool rtc_affidabile = (rr != ESP_RST_POWERON && rr != ESP_RST_BROWNOUT);
  if (rtc_affidabile && stima_valida(g_rtc_stima)) {
    g_stima = g_rtc_stima;
    LOG_I("STIMA", "Stima del carico ripristinata dalla memoria RTC");
  } else {
    StimaPersistita s = {};
    Preferences prefs;
    prefs.begin(NVS_NS_FERM, true);
    const size_t n = prefs.getBytes(NVS_KEY_STIMA, &s, sizeof(s));
    prefs.end();
    if (n == sizeof(s) && stima_valida(s)) {
      g_stima = s;
      g_rtc_stima = s;
      LOG_I("STIMA", "Stima del carico ripristinata dalla NVS");
    } else {
      g_stima = {};
      g_rtc_stima = {};
    }
  }
  if (!rtc_affidabile || g_rtc_secondi_check != ~g_rtc_secondi_attiva) {
    imposta_secondi_attiva(0);
  }
  g_stato.ore_attiva = g_rtc_secondi_attiva / 3600.0f;
  pubblica_apprendimento();
}

// Chiude il blocco corrente: se valido entra nella finestra e aggiorna la stima.
static void chiudi_blocco(unsigned long now_ms) {
  if (g_acc_valido && g_acc_ms > 0) {
    Blocco& b = g_blocchi[g_testa];
    b.u_freddo = (float)g_acc_freddo_ms / (float)g_acc_ms;
    b.u_caldo = (float)g_acc_caldo_ms / (float)g_acc_ms;
    b.t_media = (float)(g_acc_temp_ms / (double)g_acc_ms);
    g_testa = (uint8_t)((g_testa + 1) % STIMA_BLOCCHI_MAX);
    if (g_contigui < STIMA_BLOCCHI_MAX) g_contigui++;
  } else {
    g_contigui = 0;  // un buco interrompe la finestra: la stima precedente resta valida
  }
  g_acc_ms = g_acc_freddo_ms = g_acc_caldo_ms = 0;
  g_acc_temp_ms = 0.0;
  g_acc_valido = true;
  g_stato.blocchi = g_contigui;

  if (g_contigui < STIMA_BLOCCHI_MIN) return;

  // Medie e regressione lineare della temperatura sui blocchi, dal più vecchio al più recente
  const int n = g_contigui;
  const float dx_h = STIMA_BLOCCO_S / 3600.0f;
  double su_f = 0.0, su_c = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  for (int i = 0; i < n; i++) {
    const Blocco& b = g_blocchi[(g_testa + STIMA_BLOCCHI_MAX - n + i) % STIMA_BLOCCHI_MAX];
    const double x = i * dx_h;
    su_f += b.u_freddo;
    su_c += b.u_caldo;
    sx += x;
    sy += b.t_media;
    sxx += x * x;
    sxy += x * b.t_media;
  }
  const double den = n * sxx - sx * sx;
  float pendenza = den > 0.0 ? (float)((n * sxy - sx * sy) / den) : 0.0f;
  if (pendenza > STIMA_PENDENZA_MAX_C_H) pendenza = STIMA_PENDENZA_MAX_C_H;
  if (pendenza < -STIMA_PENDENZA_MAX_C_H) pendenza = -STIMA_PENDENZA_MAX_C_H;

  StimaPersistita s = {};
  s.magic = STIMA_MAGIC;
  s.u_freddo = (float)(su_f / n);
  s.u_caldo = (float)(su_c / n);
  s.t_media = (float)(sy / n);
  s.pendenza_c_h = pendenza;
  s.blocchi = (uint32_t)n;
  s.checksum = checksum(s);
  if (!stima_valida(s)) return;

  const bool prima = !g_stato.pronta;
  g_stima = s;
  g_rtc_stima = s;
  pubblica_apprendimento();
  g_nvs_da_salvare = true;
  if (prima || g_ts_nvs_ms == 0 || now_ms - g_ts_nvs_ms >= STIMA_NVS_INTERVALLO_MS) {
    salva_nvs();
    g_ts_nvs_ms = now_ms;
    g_nvs_da_salvare = false;
  }
  if (prima) {
    LOG_I("STIMA", "Stima del carico pronta: freddo %.1f%% caldo %.1f%% T media %.2f pendenza %.3f C/h",
          100.0f * s.u_freddo, 100.0f * s.u_caldo, s.t_media, s.pendenza_c_h);
  }
}

bool stima_aggiorna(unsigned long now_ms,
                    bool in_run,
                    bool misura_ok,
                    bool sensore_perso,
                    float temp_c,
                    float setpoint_c,
                    bool rele_caldo,
                    bool rele_freddo,
                    RichiestaUscite* richiesta) {
  uint32_t dt_ms = 0;
  bool salto = false;
  if (g_ts_prec_ms != 0) {
    dt_ms = (uint32_t)(now_ms - g_ts_prec_ms);
    salto = dt_ms > STIMA_TICK_MAX_MS;  // loop fermo a lungo: il blocco non è rappresentativo
  }
  g_ts_prec_ms = now_ms;

  // --- Apprendimento: solo con controllo normale in corso ---
  if (dt_ms > 0) {
    if (!in_run || !misura_ok || g_stato.attiva || salto || isnan(temp_c)) g_acc_valido = false;
    g_acc_ms += dt_ms;
    if (rele_freddo) g_acc_freddo_ms += dt_ms;
    if (rele_caldo) g_acc_caldo_ms += dt_ms;
    if (!isnan(temp_c)) g_acc_temp_ms += (double)temp_c * dt_ms;
    if (g_acc_ms >= STIMA_BLOCCO_S * 1000UL) chiudi_blocco(now_ms);
  }
  if (g_nvs_da_salvare && now_ms - g_ts_nvs_ms >= STIMA_NVS_INTERVALLO_MS) {
    salva_nvs();
    g_ts_nvs_ms = now_ms;
    g_nvs_da_salvare = false;
  }

  // Duty che la stima applicherebbe al setpoint corrente (pubblicato anche quando non attiva)
  if (g_stato.pronta) {
    stima_calcola_duty(g_stima.u_freddo, g_stima.u_caldo, g_stima.t_media, g_stima.pendenza_c_h,
                       setpoint_c, &g_stato.duty_freddo_pct, &g_stato.duty_caldo_pct);
  } else {
    g_stato.duty_freddo_pct = 0.0f;
    g_stato.duty_caldo_pct = 0.0f;
  }

  // --- Regolazione stimata ---
  if (misura_ok) {
    // Sensore tornato: si riparte da zero col conteggio della durata massima
    if (g_stato.attiva) LOG_I("STIMA", "Sensore tornato valido: fine regolazione stimata dopo %.2f h", g_stato.ore_attiva);
    g_stato.attiva = false;
    g_stato.scaduta = false;
    if (g_rtc_secondi_attiva != 0) imposta_secondi_attiva(0);
    g_resto_attiva_ms = 0;
    g_stato.ore_attiva = 0.0f;
    return false;
  }

  const bool richiesta_attiva = in_run && sensore_perso && g_stato.pronta && !isnan(setpoint_c);
  if (!richiesta_attiva || g_stato.scaduta) {
    g_stato.attiva = false;
    return false;
  }

  if (!g_stato.attiva) {
    g_stato.attiva = true;
    g_ts_inizio_attiva_ms = now_ms;
    LOG_W("STIMA", "Sensore perso: regolazione stimata, freddo %.1f%% caldo %.1f%% (gia' %.2f h)",
          g_stato.duty_freddo_pct, g_stato.duty_caldo_pct, g_rtc_secondi_attiva / 3600.0f);
  } else {
    g_resto_attiva_ms += dt_ms;
    if (g_resto_attiva_ms >= 1000UL) {
      imposta_secondi_attiva(g_rtc_secondi_attiva + g_resto_attiva_ms / 1000UL);
      g_resto_attiva_ms %= 1000UL;
    }
  }
  g_stato.ore_attiva = g_rtc_secondi_attiva / 3600.0f;

  if (g_rtc_secondi_attiva >= (uint32_t)STIMA_DURATA_MAX_H * 3600UL) {
    LOG_E("STIMA", "Regolazione stimata oltre %u h: rele' spenti", (unsigned)STIMA_DURATA_MAX_H);
    g_stato.scaduta = true;
    g_stato.attiva = false;
    return false;
  }

  // Finestra time-proportional lunga: impulsi della pompa simili a quelli del controllo normale
  const unsigned long fase = (now_ms - g_ts_inizio_attiva_ms) % STIMA_FINESTRA_MS;
  const unsigned long on_freddo = (unsigned long)(STIMA_FINESTRA_MS * (g_stato.duty_freddo_pct / 100.0f));
  const unsigned long on_caldo = (unsigned long)(STIMA_FINESTRA_MS * (g_stato.duty_caldo_pct / 100.0f));
  RichiestaUscite r = {};
  r.cool_on = on_freddo >= RELE_IMPULSO_MIN_MS && fase < on_freddo;
  r.heat_on = on_caldo >= RELE_IMPULSO_MIN_MS && fase < on_caldo;
  *richiesta = r;
  return true;
}

const StatoStima& stima_stato() {
  return g_stato;
}
