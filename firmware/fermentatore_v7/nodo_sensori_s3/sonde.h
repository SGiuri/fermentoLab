#pragma once
#include <Arduino.h>

// Tre DS18B20, ognuno sul suo bus OneWire con l'alimentazione (VDD) presa da un GPIO: un guasto su un
// cavo non blocca le altre sonde, e il power-cycle di una sonda non tocca le altre.
// Tutto non bloccante: conversione avviata, letta 750 ms dopo; power-cycle a fasi.

struct DiagSonda {
  uint32_t letture;      // conversioni completate (valide + scartate)
  uint32_t non_valide;   // scartate: scollegato + poweron_85 + fuori_scala
  uint32_t scollegato;   // NaN o -127: sonda assente, cavo, CRC
  uint32_t poweron_85;   // 85 °C: la sonda si è appena riaccesa (alimentazione ceduta)
  uint32_t fuori_scala;  // fuori dall'intervallo della sonda
  uint32_t power_cycle;  // power-cycle eseguiti
};

struct Sonda {
  const char* id;            // "f1", "ch", "f2": anche l'ultima parte del topic
  uint8_t pin_dq;
  uint8_t pin_vdd;
  float min_c;               // letture fuori da min_c..max_c scartate
  float max_c;

  float ultima_valida;
  unsigned long ts_ultima_valida_ms;  // 0 = mai dal boot
  unsigned long ts_persa_ms;          // inizio della perdita in corso, 0 = sonda ok
  const char* ultimo_errore;          // "" se l'ultima lettura era valida
  uint8_t power_cycle_tentativi;      // dall'inizio della perdita in corso
  DiagSonda diag;
};

// Esito di una lettura appena completata: valida (t) o scartata (errore).
typedef void (*LetturaSondaFn)(const Sonda& s, bool valida, float t, const char* errore);
// Fine di un power-cycle (per il registro eventi).
typedef void (*PowerCycleFn)(const Sonda& s);

void sonde_init(LetturaSondaFn on_lettura, PowerCycleFn on_power_cycle);
void sonde_aggiorna(unsigned long now);
// Power-cycle immediato di una sonda (comando da HA). false se l'id non esiste o è già in corso.
bool sonde_power_cycle(const char* id);

int sonde_numero();
const Sonda& sonde_get(int i);
// Sonda persa da almeno ms (e vista valida almeno una volta dal boot): candidata al riavvio del nodo.
const Sonda* sonde_persa_da(unsigned long now, unsigned long ms);
// true se nessuna sonda vista dal boot è persa adesso.
bool sonde_tutte_ok();
