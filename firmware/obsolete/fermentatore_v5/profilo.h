#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================
// Strutture dati profilo
// ============================================================

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

  // Parametri legacy (parsati per compatibilità ma non usati dal PID)
  float       isteresi;               // non usato in v5 (PID sostituisce bang-bang)
  uint32_t    min_cycle_minutes;      // non usato in v5 (pompa non è compressore)

  uint32_t    allarme_timeout_minuti; // timeout watchdog sensore [min]

  // Parametri PID — riscaldatore (cinghia 30W, lenta)
  float       kp_risc;
  float       ki_risc;
  float       kd_risc;

  // Parametri PID — refrigeratore (pompa glicole, veloce/potente)
  float       kp_frigo;
  float       ki_frigo;
  float       kd_frigo;

  // Parametri PWM
  uint32_t    pwm_period_seconds;    // durata ciclo PWM [s] (default 60)
  uint32_t    min_pompa_on_seconds;  // impulso minimo pompa [s] (default 5)

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
// Nuovi campi v5: kp_risc/ki_risc/kd_risc, kp_frigo/ki_frigo/kd_frigo,
//                 pwm_period_seconds, min_pompa_on_seconds.
// Campi mancanti vengono rimpiazzati dai default in config.h (compatibilità v4).
bool parsaProfiloJSON(const char* json_str, ProfiloFermentazione& p);

// Persistenza NVS
void salvaProfilo(const ProfiloFermentazione& p);
bool caricaProfilo(ProfiloFermentazione& p);
void cancellaProfilo();

// Aggiorna in NVS solo il campo ultimo_epoch_noto (chiamato ogni heartbeat).
void salvaEpochNoto(uint32_t epoch);
