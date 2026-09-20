#pragma once

#include <Arduino.h>

// Esito di una chiamata a sensor_aggiorna().
enum class LetturaSensore : uint8_t {
  NESSUNA_NOVITA = 0,  // nessuna conversione completata in questa chiamata
  VALIDA         = 1,  // nuova lettura valida: ultima_temp_valida aggiornata
  NON_VALIDA     = 2   // conversione completata ma lettura scartata
};

// Diagnostica delle letture del DS18B20 dal boot (solo RAM: si azzera a ogni riavvio).
// Le letture scartate non arrivano mai a HA: senza questi contatori un sensore che perde
// qualche lettura, ma non abbastanza da far scattare l'allarme, resterebbe invisibile.
struct SensorDiagnostica {
  uint32_t letture;        // conversioni completate (valide + scartate)
  uint32_t non_valide;     // scartate, totale = scollegato + poweron_85 + fuori_scala + picco
  uint32_t scollegato;     // NaN o -127: bus vuoto / sensore assente / CRC
  uint32_t poweron_85;     // 85 °C: marcatore di power-on, l'alimentazione del sensore è ceduta
  uint32_t fuori_scala;    // fuori da -10..40 °C
  uint32_t picco;          // filtro anti-picco (oscillazione incoerente)
  uint32_t vicino_rele;    // sottoinsieme di non_valide: entro SENSOR_DIAG_FINESTRA_RELE_MS da una commutazione
  uint32_t power_cycle;    // cicli di alimentazione del sensore eseguiti
};

struct SensorState {
  float ultima_temp_valida;
  unsigned long ts_ultima_valida_ms;  // millis() dell'ultima lettura valida, 0 = mai
  bool in_allarme;
  bool era_in_allarme;
  unsigned long ts_primo_errore_ms;
  bool ha_avuto_errore;
  SensorDiagnostica diag;
};

void sensor_init(SensorState& s);

// Da chiamare a ogni loop. Avvia una conversione ogni SENSOR_READ_INTERVAL_MS e la
// legge dopo SENSOR_CONVERSION_MS. Non segnala mai come nuova una lettura vecchia.
// ts_ultima_commutazione_ms: millis() dell'ultima commutazione di un relè (0 = nessuna dal boot);
// serve solo alla diagnostica, per contare le letture scartate vicino ai relè.
LetturaSensore sensor_aggiorna(SensorState& s, uint32_t allarme_timeout_minuti,
                               unsigned long ts_ultima_commutazione_ms = 0);

// Età in ms dell'ultima lettura valida; UINT32_MAX se non ce n'è mai stata una.
uint32_t sensor_eta_ultima_valida_ms(const SensorState& s);

bool sensor_allarme_nuovo(const SensorState& s);
bool sensor_ripristinato(const SensorState& s);

// Power management for DS18B20 VDD using PIN_SENSOR_PWR.
// Il power-cycle non azzera il timer di errore: l'allarme scatta comunque in tempo.
void sensor_power_cycle_init();
bool sensor_power_cycle(SensorState& s);
