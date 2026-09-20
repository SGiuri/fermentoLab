#include "sensor_watchdog.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Oggetti statici: PIN definito in config.h al momento della compilazione
static OneWire          _ow(PIN_DS18B20);
static DallasTemperature _dt(&_ow);

// Verifica che la temperatura letta sia fisica e non un codice di errore DS18B20
static bool temp_valida(float t) {
  if (t == DEVICE_DISCONNECTED_C) return false;
  if (t == 85.0f)                 return false;  // Valore di power-on-reset DS18B20
  if (t < -10.0f || t > 40.0f)   return false;  // Fuori range fisico del fermentatore
  return true;
}

void sensor_init(SensorState& s) {
  _dt.begin();
  s.ultima_temp_valida    = 20.0f;
  s.in_allarme            = false;
  s.era_in_allarme        = false;
  s.ts_primo_errore_ms    = 0;
  s.ha_avuto_errore       = false;
}

bool sensor_leggi(SensorState& s, float& temp_out, uint32_t allarme_timeout_minuti) {
  // Salva lo stato del ciclo precedente per rilevare transizioni
  s.era_in_allarme = s.in_allarme;

  _dt.requestTemperatures();
  float t = _dt.getTempCByIndex(0);

  if (temp_valida(t)) {
    // Lettura buona: azzera lo stato di errore
    s.ultima_temp_valida  = t;
    s.in_allarme          = false;
    s.ha_avuto_errore     = false;
    s.ts_primo_errore_ms  = 0;
    temp_out = t;
    return true;
  }

  // Lettura non valida
  if (!s.ha_avuto_errore) {
    s.ts_primo_errore_ms = millis();
    s.ha_avuto_errore    = true;
  }

  unsigned long ms_in_errore = millis() - s.ts_primo_errore_ms;
  unsigned long timeout_ms   = (unsigned long)allarme_timeout_minuti * 60UL * 1000UL;

  if (ms_in_errore >= timeout_ms) {
    s.in_allarme = true;
  }

  temp_out = s.ultima_temp_valida;  // Restituisce l'ultimo valore valido
  return false;
}

bool sensor_allarme_nuovo(const SensorState& s) {
  return (s.in_allarme && !s.era_in_allarme);
}

bool sensor_ripristinato(const SensorState& s) {
  return (!s.in_allarme && s.era_in_allarme);
}
