#include "sonde.h"

#include "config.h"

#include <DallasTemperature.h>
#include <OneWire.h>
#include <driver/gpio.h>
#include <math.h>
#include <string.h>

// Fasi di una sonda. ATTESA -> CONVERSIONE -> (lettura) -> ATTESA; il power-cycle passa da
// SPENTA (VDD e DQ a massa) e ACCENSIONE (VDD alto, attesa prima di ripartire).
enum class Fase : uint8_t { ATTESA, CONVERSIONE, SPENTA, ACCENSIONE };

struct Bus {
  OneWire* ow;
  DallasTemperature* ds;
  Fase fase;
  unsigned long ts_fase_ms;
  unsigned long ts_prossima_ms;
  unsigned long ts_ultimo_pc_ms;
};

static OneWire g_ow_f1(PIN_SONDA_F1_DQ);
static OneWire g_ow_ch(PIN_SONDA_CHILLER_DQ);
static OneWire g_ow_f2(PIN_SONDA_F2_DQ);
static DallasTemperature g_ds_f1(&g_ow_f1);
static DallasTemperature g_ds_ch(&g_ow_ch);
static DallasTemperature g_ds_f2(&g_ow_f2);

static Sonda g_sonde[] = {
  {"f1", PIN_SONDA_F1_DQ, PIN_SONDA_F1_VDD, SONDA_FERMENTATORE_MIN_C, SONDA_FERMENTATORE_MAX_C},
  {"ch", PIN_SONDA_CHILLER_DQ, PIN_SONDA_CHILLER_VDD, SONDA_CHILLER_MIN_C, SONDA_CHILLER_MAX_C},
  {"f2", PIN_SONDA_F2_DQ, PIN_SONDA_F2_VDD, SONDA_FERMENTATORE_MIN_C, SONDA_FERMENTATORE_MAX_C},
};
static Bus g_bus[] = {
  {&g_ow_f1, &g_ds_f1},
  {&g_ow_ch, &g_ds_ch},
  {&g_ow_f2, &g_ds_f2},
};
static const int N_SONDE = sizeof(g_sonde) / sizeof(g_sonde[0]);

static LetturaSondaFn g_on_lettura = nullptr;
static PowerCycleFn g_on_power_cycle = nullptr;

static void bus_avvia(Bus& b) {
  b.ds->begin();
  b.ds->setResolution(12);
  b.ds->setWaitForConversion(false);
}

static void alimenta(const Sonda& s, bool acceso) {
  digitalWrite(s.pin_vdd, acceso ? HIGH : LOW);
}

// Classifica una lettura: "" = valida, altrimenti la causa dello scarto.
static const char* classifica(const Sonda& s, float t) {
  if (isnan(t) || t == DEVICE_DISCONNECTED_C) return "scollegato";
  if (t == 85.0f) return "poweron_85";  // marcatore di accensione del DS18B20
  if (t < s.min_c || t > s.max_c) return "fuori_scala";
  return "";
}

static void registra(Sonda& s, float t, unsigned long now) {
  s.diag.letture++;
  const char* err = classifica(s, t);
  if (err[0] == '\0') {
    s.ultima_valida = t;
    s.ts_ultima_valida_ms = now ? now : 1;  // 0 = mai
    s.ts_persa_ms = 0;
    s.power_cycle_tentativi = 0;
    s.ultimo_errore = "";
    if (g_on_lettura) g_on_lettura(s, true, t, "");
    return;
  }
  s.diag.non_valide++;
  if (strcmp(err, "scollegato") == 0) s.diag.scollegato++;
  else if (strcmp(err, "poweron_85") == 0) s.diag.poweron_85++;
  else s.diag.fuori_scala++;
  s.ultimo_errore = err;
  if (g_on_lettura) g_on_lettura(s, false, NAN, err);
}

static void power_cycle_avvia(Sonda& s, Bus& b, unsigned long now) {
  // VDD e DQ a massa insieme: con il solo VDD a massa la sonda resterebbe alimentata in parassita
  // dalla linea dati (V6, F1, 01/10/2026). Il pull-up è sulla VDD commutata, quindi si spegne anche lui.
  pinMode(s.pin_dq, OUTPUT);
  digitalWrite(s.pin_dq, LOW);
  alimenta(s, false);
  b.fase = Fase::SPENTA;
  b.ts_fase_ms = now;
  b.ts_ultimo_pc_ms = now;
  s.diag.power_cycle++;
  if (s.power_cycle_tentativi < 255) s.power_cycle_tentativi++;
}

// Persa = nessuna lettura valida da SONDA_PERSA_MS (dal boot, se mai letta). Il primo power-cycle dopo
// SONDA_POWER_CYCLE_PRIMO_MS di perdita, i successivi ogni SONDA_POWER_CYCLE_MS per i primi
// SONDA_POWER_CYCLE_RAPIDI tentativi, poi ogni SONDA_POWER_CYCLE_LENTO_MS senza limite.
static void sorveglia(Sonda& s, Bus& b, unsigned long now) {
  const unsigned long rif = s.ts_ultima_valida_ms ? s.ts_ultima_valida_ms : 0;
  const bool persa = now - rif > SONDA_PERSA_MS;
  if (!persa) return;
  if (s.ts_persa_ms == 0) s.ts_persa_ms = now ? now : 1;
  if (b.fase != Fase::ATTESA) return;
  const unsigned long attesa = s.power_cycle_tentativi == 0 ? SONDA_POWER_CYCLE_PRIMO_MS
                             : s.power_cycle_tentativi < SONDA_POWER_CYCLE_RAPIDI ? SONDA_POWER_CYCLE_MS
                             : SONDA_POWER_CYCLE_LENTO_MS;
  const unsigned long da = s.power_cycle_tentativi == 0 ? s.ts_persa_ms : b.ts_ultimo_pc_ms;
  if (now - da >= attesa) power_cycle_avvia(s, b, now);
}

// Controllo del cablaggio all'avvio, sulla seriale. Con la VDD della sonda alta, la linea dati letta con il
// pull-down interno (~45 kΩ) resta alta solo se c'è il pull-up da 4,7 kΩ verso la VDD e la linea non è a massa.
// La VDD letta col pull-up interno, prima di accenderla, va bassa se è in corto verso massa.
static void verifica_cablaggio(const Sonda& s) {
  pinMode(s.pin_vdd, INPUT_PULLUP);
  delay(2);
  const bool vdd_a_massa = digitalRead(s.pin_vdd) == LOW;
  pinMode(s.pin_vdd, OUTPUT);
  digitalWrite(s.pin_vdd, HIGH);
  pinMode(s.pin_dq, INPUT_PULLDOWN);
  delay(2);
  const bool dq_alta = digitalRead(s.pin_dq) == HIGH;
  // Linea bassa: con la VDD spenta e il pull-up interno, una linea aperta sale, una linea a massa resta bassa.
  bool dq_a_massa = false;
  if (!dq_alta) {
    digitalWrite(s.pin_vdd, LOW);
    pinMode(s.pin_dq, INPUT_PULLUP);
    delay(2);
    dq_a_massa = digitalRead(s.pin_dq) == LOW;
    digitalWrite(s.pin_vdd, HIGH);
  }
  pinMode(s.pin_dq, INPUT);
  Serial.printf("[CABLAGGIO] %s: VDD GPIO%u %s, DQ GPIO%u %s\n", s.id, (unsigned)s.pin_vdd,
                vdd_a_massa ? "A MASSA (corto o carico forte)" : "ok", (unsigned)s.pin_dq,
                dq_alta ? "alta: pull-up presente"
                : dq_a_massa ? "A MASSA (corto verso GND, o pull-up verso la VDD spenta)"
                             : "aperta: manca il pull-up da 4,7 kΩ verso la VDD (o il filo)");
}

void sonde_init(LetturaSondaFn on_lettura, PowerCycleFn on_power_cycle) {
  g_on_lettura = on_lettura;
  g_on_power_cycle = on_power_cycle;
  for (int i = 0; i < N_SONDE; i++) verifica_cablaggio(g_sonde[i]);
  const unsigned long now = millis();
  for (int i = 0; i < N_SONDE; i++) {
    Sonda& s = g_sonde[i];
    Bus& b = g_bus[i];
    s.ultima_valida = NAN;
    s.ts_ultima_valida_ms = 0;
    s.ts_persa_ms = 0;
    s.ultimo_errore = "mai_letta";
    s.power_cycle_tentativi = 0;
    s.diag = DiagSonda();
    // VDD dal GPIO con la corrente massima disponibile: il DS18B20 assorbe ~1,5 mA in conversione.
    pinMode(s.pin_vdd, OUTPUT);
    gpio_set_drive_capability((gpio_num_t)s.pin_vdd, GPIO_DRIVE_CAP_3);
    alimenta(s, true);
    b.fase = Fase::ACCENSIONE;
    b.ts_fase_ms = now;
    b.ts_ultimo_pc_ms = 0;
    // Letture sfasate: un bus alla volta.
    b.ts_prossima_ms = now + SONDA_ACCENSIONE_MS + i * (SONDA_INTERVALLO_MS / N_SONDE);
  }
}

void sonde_aggiorna(unsigned long now) {
  for (int i = 0; i < N_SONDE; i++) {
    Sonda& s = g_sonde[i];
    Bus& b = g_bus[i];
    switch (b.fase) {
      case Fase::SPENTA:
        if (now - b.ts_fase_ms >= SONDA_POWER_CYCLE_OFF_MS) {
          alimenta(s, true);
          pinMode(s.pin_dq, INPUT);
          b.fase = Fase::ACCENSIONE;
          b.ts_fase_ms = now;
          b.ts_prossima_ms = now + SONDA_ACCENSIONE_MS;
          if (g_on_power_cycle) g_on_power_cycle(s);
        }
        break;
      case Fase::ACCENSIONE:
        if ((long)(now - b.ts_prossima_ms) >= 0) {
          bus_avvia(b);
          b.fase = Fase::ATTESA;
        }
        break;
      case Fase::ATTESA:
        if ((long)(now - b.ts_prossima_ms) >= 0) {
          // Sonda collegata dopo l'avvio: il bus va rienumerato, altrimenti l'indice 0 resta vuoto.
          if (b.ds->getDeviceCount() == 0) bus_avvia(b);
          b.ds->requestTemperatures();
          b.fase = Fase::CONVERSIONE;
          b.ts_fase_ms = now;
          b.ts_prossima_ms += SONDA_INTERVALLO_MS;
          if ((long)(now - b.ts_prossima_ms) >= 0) b.ts_prossima_ms = now + SONDA_INTERVALLO_MS;
        }
        break;
      case Fase::CONVERSIONE:
        if (now - b.ts_fase_ms >= SONDA_CONVERSIONE_MS) {
          b.fase = Fase::ATTESA;
          registra(s, b.ds->getTempCByIndex(0), now);
        }
        break;
    }
    sorveglia(s, b, now);
  }
}

bool sonde_power_cycle(const char* id) {
  for (int i = 0; i < N_SONDE; i++) {
    if (strcmp(g_sonde[i].id, id) != 0) continue;
    if (g_bus[i].fase == Fase::SPENTA || g_bus[i].fase == Fase::ACCENSIONE) return false;
    power_cycle_avvia(g_sonde[i], g_bus[i], millis());
    return true;
  }
  return false;
}

int sonde_numero() { return N_SONDE; }

const Sonda& sonde_get(int i) { return g_sonde[i]; }

const Sonda* sonde_persa_da(unsigned long now, unsigned long ms) {
  for (int i = 0; i < N_SONDE; i++) {
    const Sonda& s = g_sonde[i];
    // Solo sonde che hanno funzionato dopo l'avvio: una sonda mai collegata non fa riavviare il nodo.
    if (s.ts_ultima_valida_ms != 0 && s.ts_persa_ms != 0 && now - s.ts_persa_ms >= ms) return &s;
  }
  return nullptr;
}

bool sonde_tutte_ok() {
  for (int i = 0; i < N_SONDE; i++) {
    if (g_sonde[i].ts_ultima_valida_ms != 0 && g_sonde[i].ts_persa_ms != 0) return false;
  }
  return true;
}
