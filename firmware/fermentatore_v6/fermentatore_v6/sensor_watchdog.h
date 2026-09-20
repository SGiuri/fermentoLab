#pragma once

#include <Arduino.h>

// Esito di una chiamata a sensor_aggiorna().
enum class LetturaSensore : uint8_t {
  NESSUNA_NOVITA = 0,  // nessuna conversione completata in questa chiamata
  VALIDA         = 1,  // nuova lettura valida: ultima_temp_valida aggiornata
  NON_VALIDA     = 2   // conversione completata ma lettura scartata
};

struct SensorState {
  float ultima_temp_valida;
  unsigned long ts_ultima_valida_ms;  // millis() dell'ultima lettura valida, 0 = mai
  bool in_allarme;
  bool era_in_allarme;
  unsigned long ts_primo_errore_ms;
  bool ha_avuto_errore;
};

void sensor_init(SensorState& s);

// Da chiamare a ogni loop. Avvia una conversione ogni SENSOR_READ_INTERVAL_MS e la
// legge dopo SENSOR_CONVERSION_MS. Non segnala mai come nuova una lettura vecchia.
LetturaSensore sensor_aggiorna(SensorState& s, uint32_t allarme_timeout_minuti);

// Età in ms dell'ultima lettura valida; UINT32_MAX se non ce n'è mai stata una.
uint32_t sensor_eta_ultima_valida_ms(const SensorState& s);

bool sensor_allarme_nuovo(const SensorState& s);
bool sensor_ripristinato(const SensorState& s);

// Power management for DS18B20 VDD using PIN_SENSOR_PWR.
// Il power-cycle non azzera il timer di errore: l'allarme scatta comunque in tempo.
void sensor_power_cycle_init();
bool sensor_power_cycle(SensorState& s);
