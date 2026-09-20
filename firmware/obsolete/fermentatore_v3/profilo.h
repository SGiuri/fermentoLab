#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================
// Strutture dati profilo
// ============================================================

// Un singolo step: costante (duration_hours) o rampa (ramp_hours).
struct ProfiloStep {
  char  nome[48];
  float temp;            // temperatura target (o finale per una rampa)
  float duration_hours;  // durata a temperatura costante (0 se rampa)
  float ramp_hours;      // durata della rampa (0 se costante)
  bool  is_ramp;
};

struct ProfiloFermentazione {
  char        nome[32];
  uint32_t    avvio_epoch;            // timestamp Unix avvio (da HA)
  uint32_t    ultimo_epoch_noto;      // ultimo epoch NTP salvato (fallback offline)
  float       isteresi;               // isteresi relè [°C]
  uint32_t    min_cycle_minutes;      // protezione compressore [min]
  uint32_t    allarme_timeout_minuti; // timeout watchdog sensore [min]
  ProfiloStep steps[MAX_STEPS];
  int         num_steps;
  bool        valido;
};

// ============================================================
// Funzioni
// ============================================================

// Calcola il setpoint corrente [°C] dal tempo trascorso [s].
// Ritorna -999.0 se il profilo non è valido.
float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi);

// Popola idx, ore_step e ore_totali in base al tempo trascorso.
// idx == -1 se profilo non valido.
void stepCorrente(const ProfiloFermentazione& p, uint32_t secondi_trascorsi,
                  int& idx_out, float& ore_step_out, float& ore_totali_out);

// Deserializza il profilo dal payload JSON MQTT.
// Formato atteso: {"profilo":"nome","avvio_epoch":...,"steps":[...],...}
bool parsaProfiloJSON(const char* json_str, ProfiloFermentazione& p);

// Persistenza NVS (sopravvive al reset e al power-off)
void salvaProfilo(const ProfiloFermentazione& p);
bool caricaProfilo(ProfiloFermentazione& p);
void cancellaProfilo();

// Aggiorna in NVS solo il campo ultimo_epoch_noto (chiamato ogni heartbeat).
// Permette di stimare il tempo trascorso se al riavvio NTP non è disponibile.
void salvaEpochNoto(uint32_t epoch);
