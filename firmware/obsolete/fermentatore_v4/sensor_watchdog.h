#pragma once
#include <Arduino.h>

struct SensorState {
  float         ultima_temp_valida;
  bool          in_allarme;
  bool          era_in_allarme;         // stato del ciclo precedente (rilevamento transizioni)
  unsigned long ts_primo_errore_ms;     // millis() del primo errore consecutivo
  bool          ha_avuto_errore;        // true = sequenza di errori in corso
};

// Inizializza il bus OneWire e lo stato interno.
void sensor_init(SensorState& s);

// Legge il DS18B20. Ritorna true se la lettura è valida.
// temp_out: valore letto, oppure ultimo valido in caso di errore.
// Se l'errore persiste oltre allarme_timeout_minuti, setta s.in_allarme = true.
bool sensor_leggi(SensorState& s, float& temp_out, uint32_t allarme_timeout_minuti);

// true solo nel ciclo in cui il sensore è APPENA entrato in allarme (ok → allarme).
bool sensor_allarme_nuovo(const SensorState& s);

// true solo nel ciclo in cui il sensore si è APPENA ripristinato (allarme → ok).
bool sensor_ripristinato(const SensorState& s);

// Configura PIN_SENSOR_PWR come output e porta il sensore in alimentazione.
// Chiamare in setup() PRIMA di sensor_init().
void sensor_power_cycle_init();

// Power cycle del DS18B20: spegne alimentazione 500ms, riaccende, attende
// prima conversione (750ms), verifica lettura. Bloccante ~1.45s.
// Ritorna true se il sensore risponde con un valore plausibile dopo il ciclo.
// Usa solo in condizioni di fault (relè già off).
bool sensor_power_cycle(SensorState& s);
