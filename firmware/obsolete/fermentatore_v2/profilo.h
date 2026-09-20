#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// Un singolo step del profilo di fermentazione.
// Può essere di tipo "duration" (temp costante) o "ramp" (rampa lineare).
struct ProfiloStep {
  char  nome[48];
  float temp;            // Temperatura target (o temp finale per rampa)
  float duration_hours;  // Durata in ore (0 se è una rampa)
  float ramp_hours;      // Durata rampa in ore (0 se non è una rampa)
  bool  is_ramp;
};

struct ProfiloFermentazione {
  char         nome[32];
  uint32_t     avvio_epoch;           // Timestamp Unix avvio
  float        isteresi;
  uint32_t     min_cycle_minutes;
  uint32_t     allarme_timeout_minuti;
  ProfiloStep  steps[MAX_STEPS];
  int          num_steps;
  bool         valido;
};

// Calcola il setpoint corrente in base al tempo trascorso dall'avvio (in secondi).
// Restituisce -999.0 se il profilo non è valido.
float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi);

// Restituisce l'indice dello step corrente, le ore trascorse in quel step
// e le ore totali dall'avvio.
void stepCorrente(const ProfiloFermentazione& p, uint32_t secondi_trascorsi,
                  int& idx_out, float& ore_step_out, float& ore_totali_out);

// Deserializza il profilo dal payload JSON MQTT.
bool parsaProfiloJSON(const char* json_str, ProfiloFermentazione& p);

// Salva/carica/cancella il profilo in NVS (sopravvive al reset dell'ESP32).
void salvaProfilo(const ProfiloFermentazione& p);
bool caricaProfilo(ProfiloFermentazione& p);
void cancellaProfilo();
