#include "sensor_watchdog.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <math.h>

// Oggetti statici: il pin è definito a compile-time in config.h
static OneWire           _ow(PIN_DS18B20);
static DallasTemperature _dt(&_ow);

// Intervallo di attesa conversione DS18B20 (in ms, 12-bit = 750ms)
// Usiamo setWaitForConversion(false) + polling per ridurre il blocking
static unsigned long _ts_conversion_start = 0;
static bool          _conversion_pending   = false;

// Storico minimo per riconoscere pattern oscillanti incoerenti (+delta/-delta)
static float _raw_prev1 = 0.0f;
static float _raw_prev2 = 0.0f;
static bool  _raw_prev1_valida = false;
static bool  _raw_prev2_valida = false;
static uint8_t _osc_incoerenti_consecutive = 0;

static void push_raw_temp(float t) {
  _raw_prev2 = _raw_prev1;
  _raw_prev2_valida = _raw_prev1_valida;
  _raw_prev1 = t;
  _raw_prev1_valida = true;
}

static bool oscillazione_incoerente(float t) {
  if (!_raw_prev1_valida || !_raw_prev2_valida) return false;

  float d1 = t - _raw_prev1;
  float d2 = _raw_prev1 - _raw_prev2;

  bool spike1 = fabsf(d1) >= SENSOR_SPIKE_DELTA_C;
  bool spike2 = fabsf(d2) >= SENSOR_SPIKE_DELTA_C;
  bool inversione_rapida = (d1 * d2) < 0.0f;

  return spike1 && spike2 && inversione_rapida;
}

static bool temp_valida(float t) {
  if (t == DEVICE_DISCONNECTED_C) return false;
  if (t == 85.0f)                 return false;  // valore DS18B20 a power-on-reset
  if (t < -10.0f || t > 40.0f)   return false;  // range fisico del fermentatore
  return true;
}

void sensor_init(SensorState& s) {
  _dt.begin();
  _dt.setWaitForConversion(false);   // conversione asincrona, non blocca loop()
  _dt.setResolution(12);             // 0.0625°C di risoluzione

  s.ultima_temp_valida  = 20.0f;
  s.in_allarme          = false;
  s.era_in_allarme      = false;
  s.ts_primo_errore_ms  = 0;
  s.ha_avuto_errore     = false;

  _raw_prev1 = 0.0f;
  _raw_prev2 = 0.0f;
  _raw_prev1_valida = false;
  _raw_prev2_valida = false;
  _osc_incoerenti_consecutive = 0;
}

bool sensor_leggi(SensorState& s, float& temp_out, uint32_t allarme_timeout_minuti) {
  // Salva stato ciclo precedente per rilevare transizioni
  s.era_in_allarme = s.in_allarme;

  unsigned long ora = millis();

  // Avvia conversione se non ce n'è una in corso
  if (!_conversion_pending) {
    _dt.requestTemperatures();
    _ts_conversion_start = ora;
    _conversion_pending  = true;
    temp_out = s.ultima_temp_valida;
    return true;
  }

  // Attendi il completamento conversione (12-bit = 750ms)
  if (ora - _ts_conversion_start < 750UL) {
    temp_out = s.ultima_temp_valida;
    return true;
  }

  // Conversione pronta: leggi il valore
  _conversion_pending = false;
  float t = _dt.getTempCByIndex(0);

  if (temp_valida(t)) {
    if (oscillazione_incoerente(t)) {
      _osc_incoerenti_consecutive++;

      if (_osc_incoerenti_consecutive >= SENSOR_OSC_CONSECUTIVE_LIMIT && !s.ha_avuto_errore) {
        s.ts_primo_errore_ms = ora;
        s.ha_avuto_errore    = true;
      }

      if (s.ha_avuto_errore) {
        unsigned long ms_in_errore = ora - s.ts_primo_errore_ms;
        unsigned long timeout_ms   = (unsigned long)allarme_timeout_minuti * 60UL * 1000UL;
        if (ms_in_errore >= timeout_ms) {
          s.in_allarme = true;
        }
      }

      push_raw_temp(t);
      temp_out = s.ultima_temp_valida;
      return false;
    }

    _osc_incoerenti_consecutive = 0;

    // Lettura valida: azzera stato errore
    s.ultima_temp_valida = t;
    s.in_allarme         = false;
    s.ha_avuto_errore    = false;
    s.ts_primo_errore_ms = 0;
    push_raw_temp(t);
    temp_out = t;
    return true;
  }

  // Lettura non valida
  _osc_incoerenti_consecutive = 0;
  if (!s.ha_avuto_errore) {
    s.ts_primo_errore_ms = ora;
    s.ha_avuto_errore    = true;
  }

  unsigned long ms_in_errore = ora - s.ts_primo_errore_ms;
  unsigned long timeout_ms   = (unsigned long)allarme_timeout_minuti * 60UL * 1000UL;

  if (ms_in_errore >= timeout_ms) {
    s.in_allarme = true;
  }

  temp_out = s.ultima_temp_valida;  // restituisce l'ultimo valore valido noto
  return false;
}

bool sensor_allarme_nuovo(const SensorState& s) {
  return (s.in_allarme && !s.era_in_allarme);
}

bool sensor_ripristinato(const SensorState& s) {
  return (!s.in_allarme && s.era_in_allarme);
}

void sensor_power_cycle_init() {
  pinMode(PIN_SENSOR_PWR, OUTPUT);
  digitalWrite(PIN_SENSOR_PWR, HIGH);  // alimenta subito il sensore
}

bool sensor_power_cycle(SensorState& s) {
  _conversion_pending = false;  // annulla qualsiasi conversione in corso

  // Power off: scarica il condensatore di bypass del DS18B20
  digitalWrite(PIN_SENSOR_PWR, LOW);
  delay(500);

  // Power on + stabilizzazione
  digitalWrite(PIN_SENSOR_PWR, HIGH);
  delay(200);

  // Re-init completa: azzera storia oscillazioni, stato allarme, stato errore
  sensor_init(s);

  // Lettura sincrona di verifica (bloccante: requestTemperatures + 750ms conversione)
  _dt.requestTemperatures();
  delay(750);
  _conversion_pending = false;
  float temp = _dt.getTempCByIndex(0);

  if (temp_valida(temp)) {
    s.ultima_temp_valida = temp;
    push_raw_temp(temp);
    return true;
  }
  return false;
}
