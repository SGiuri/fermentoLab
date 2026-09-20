#include "sensor_watchdog.h"

#include "config.h"

#include <DallasTemperature.h>
#include <OneWire.h>
#include <math.h>

static OneWire g_onewire(PIN_DS18B20);
static DallasTemperature g_ds18(&g_onewire);

static unsigned long g_ts_conversion_start = 0;
static unsigned long g_ts_ultima_richiesta = 0;
static bool g_conversion_pending = false;
static bool g_richiesta_fatta = false;

// --- Storico 3 campioni per filtro oscillazione incoerente ---
static float   g_raw_prev1        = 0.0f;
static float   g_raw_prev2        = 0.0f;
static bool    g_raw_prev1_valida = false;
static bool    g_raw_prev2_valida = false;
static uint8_t g_osc_consecutive  = 0;

static void push_raw_temp(float t) {
  g_raw_prev2        = g_raw_prev1;
  g_raw_prev2_valida = g_raw_prev1_valida;
  g_raw_prev1        = t;
  g_raw_prev1_valida = true;
}

// Ritorna true se l'ultima lettura forma un pattern spike alternato:
// |prev1-prev2| >= DELTA  &&  |t-prev1| >= DELTA  &&  i due delta hanno segno opposto.
static bool oscillazione_incoerente(float t) {
  if (!g_raw_prev1_valida || !g_raw_prev2_valida) return false;
  const float d1 = t        - g_raw_prev1;
  const float d2 = g_raw_prev1 - g_raw_prev2;
  return fabsf(d1) >= SENSOR_SPIKE_DELTA_C
      && fabsf(d2) >= SENSOR_SPIKE_DELTA_C
      && (d1 * d2) < 0.0f;
}

enum class CausaNonValida : uint8_t { NESSUNA = 0, SCOLLEGATO, POWERON_85, FUORI_SCALA, PICCO };

// Solo le cause "fisiche": il picco si riconosce dopo, con lo storico dei campioni.
static CausaNonValida classifica_lettura(float t) {
  if (isnan(t) || t == DEVICE_DISCONNECTED_C) return CausaNonValida::SCOLLEGATO;
  if (t == 85.0f) return CausaNonValida::POWERON_85;  // DS18B20 power-on marker
  if (t < -10.0f || t > 40.0f) return CausaNonValida::FUORI_SCALA;
  return CausaNonValida::NESSUNA;
}

static bool temp_valida(float t) {
  return classifica_lettura(t) == CausaNonValida::NESSUNA;
}

static void conta_non_valida(SensorState& s, CausaNonValida causa, unsigned long ora,
                             unsigned long ts_ultima_commutazione_ms) {
  s.diag.non_valide++;
  switch (causa) {
    case CausaNonValida::SCOLLEGATO:  s.diag.scollegato++;  break;
    case CausaNonValida::POWERON_85:  s.diag.poweron_85++;  break;
    case CausaNonValida::FUORI_SCALA: s.diag.fuori_scala++; break;
    case CausaNonValida::PICCO:       s.diag.picco++;       break;
    case CausaNonValida::NESSUNA:     break;
  }
  if (ts_ultima_commutazione_ms != 0 && ora - ts_ultima_commutazione_ms <= SENSOR_DIAG_FINESTRA_RELE_MS) {
    s.diag.vicino_rele++;
  }
}

// Inizializza bus e scheduling senza toccare lo stato di errore/allarme.
static void bus_init() {
  g_ds18.begin();
  g_ds18.setWaitForConversion(false);
  g_ds18.setResolution(12);

  g_conversion_pending  = false;
  g_richiesta_fatta     = false;
  g_ts_conversion_start = 0;
  g_ts_ultima_richiesta = 0;

  g_raw_prev1        = 0.0f;
  g_raw_prev2        = 0.0f;
  g_raw_prev1_valida = false;
  g_raw_prev2_valida = false;
  g_osc_consecutive  = 0;
}

static void registra_errore(SensorState& s, unsigned long ora, uint32_t allarme_timeout_minuti) {
  if (!s.ha_avuto_errore) {
    s.ts_primo_errore_ms = ora;
    s.ha_avuto_errore = true;
  }
  const unsigned long timeout_ms = allarme_timeout_minuti * 60UL * 1000UL;
  if (ora - s.ts_primo_errore_ms >= timeout_ms) {
    s.in_allarme = true;
  }
}

void sensor_init(SensorState& s) {
  bus_init();
  s.diag = SensorDiagnostica();

  s.ultima_temp_valida = DEFAULT_STANDBY_TEMP_C;
  s.ts_ultima_valida_ms = 0;
  s.in_allarme = false;
  s.era_in_allarme = false;
  s.ts_primo_errore_ms = 0;
  s.ha_avuto_errore = false;
}

LetturaSensore sensor_aggiorna(SensorState& s, uint32_t allarme_timeout_minuti,
                               unsigned long ts_ultima_commutazione_ms) {
  s.era_in_allarme = s.in_allarme;

  unsigned long ora = millis();

  if (!g_conversion_pending) {
    if (g_richiesta_fatta && (ora - g_ts_ultima_richiesta < SENSOR_READ_INTERVAL_MS)) {
      return LetturaSensore::NESSUNA_NOVITA;
    }
    g_ds18.requestTemperatures();
    g_ts_ultima_richiesta = ora;
    g_ts_conversion_start = ora;
    g_conversion_pending  = true;
    g_richiesta_fatta     = true;
    return LetturaSensore::NESSUNA_NOVITA;
  }

  if (ora - g_ts_conversion_start < SENSOR_CONVERSION_MS) {
    return LetturaSensore::NESSUNA_NOVITA;
  }

  g_conversion_pending = false;
  const float t = g_ds18.getTempCByIndex(0);
  s.diag.letture++;

  const CausaNonValida causa = classifica_lettura(t);
  if (causa != CausaNonValida::NESSUNA) {
    // Lettura fisicamente non valida
    conta_non_valida(s, causa, ora, ts_ultima_commutazione_ms);
    g_osc_consecutive = 0;
    registra_errore(s, ora, allarme_timeout_minuti);
    return LetturaSensore::NON_VALIDA;
  }

  if (oscillazione_incoerente(t)) {
    conta_non_valida(s, CausaNonValida::PICCO, ora, ts_ultima_commutazione_ms);
    g_osc_consecutive++;
    push_raw_temp(t);
    if (g_osc_consecutive >= SENSOR_OSC_CONSECUTIVE_LIMIT || s.ha_avuto_errore) {
      registra_errore(s, ora, allarme_timeout_minuti);
    }
    return LetturaSensore::NON_VALIDA;
  }

  // Lettura stabile: azzera tutto
  if (ora == 0) ora = 1;  // 0 è riservato a "mai letto"
  g_osc_consecutive     = 0;
  s.ultima_temp_valida  = t;
  s.ts_ultima_valida_ms = ora;
  s.in_allarme          = false;
  s.ha_avuto_errore     = false;
  s.ts_primo_errore_ms  = 0;
  push_raw_temp(t);
  return LetturaSensore::VALIDA;
}

uint32_t sensor_eta_ultima_valida_ms(const SensorState& s) {
  if (s.ts_ultima_valida_ms == 0) return UINT32_MAX;
  return (uint32_t)(millis() - s.ts_ultima_valida_ms);
}

bool sensor_allarme_nuovo(const SensorState& s) {
  return s.in_allarme && !s.era_in_allarme;
}

bool sensor_ripristinato(const SensorState& s) {
  return !s.in_allarme && s.era_in_allarme;
}

void sensor_power_cycle_init() {
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, HIGH);
}

bool sensor_power_cycle(SensorState& s) {
  s.diag.power_cycle++;  // stato errore/allarme intenzionalmente preservato

  digitalWrite(PIN_SENSOR_PWR, LOW);
  delay(500);

  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(200);

  bus_init();

  g_ds18.requestTemperatures();
  delay(SENSOR_CONVERSION_MS);
  g_richiesta_fatta     = true;
  g_ts_ultima_richiesta = millis();

  // La validità torna solo attraverso le letture normali (streak in fermentatore_v6.ino).
  return temp_valida(g_ds18.getTempCByIndex(0));
}
