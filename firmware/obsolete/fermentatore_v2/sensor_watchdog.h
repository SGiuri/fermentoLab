#pragma once
#include <Arduino.h>

// Stato del watchdog per il sensore DS18B20.
// OneWire e DallasTemperature sono globali statici in sensor_watchdog.cpp.
struct SensorState {
  float         ultima_temp_valida;
  bool          in_allarme;
  bool          era_in_allarme;          // stato del ciclo precedente (rilevamento transizioni)
  unsigned long ts_primo_errore_ms;      // millis() del primo errore consecutivo
  bool          ha_avuto_errore;         // true = c'è una sequenza di errori in corso
};

// Inizializza il bus OneWire e lo stato interno.
void sensor_init(SensorState& s);

// Legge il DS18B20. Restituisce true se la lettura è valida.
// temp_out: valore letto (valido) o ultimo valido in caso di errore.
// Se l'errore persiste oltre allarme_timeout_minuti, setta s.in_allarme = true.
bool sensor_leggi(SensorState& s, float& temp_out, uint32_t allarme_timeout_minuti);

// true se il sensore è APPENA entrato in allarme (transizione ok→allarme).
bool sensor_allarme_nuovo(const SensorState& s);

// true se il sensore si è APPENA ripristinato (transizione allarme→ok).
bool sensor_ripristinato(const SensorState& s);
