#pragma once
#include <Arduino.h>
#include "config.h"
#include "controller.h"

// ============================================================
// Strutture dati profilo di fermentazione (v6)
// ATTENZIONE: il layout è salvato in NVS come blob. Modificarlo fa perdere
// il profilo attivo al primo boot dopo l'aggiornamento firmware.
// ============================================================

struct ProfiloStep {
  char  nome[48];
  float temp;            // temperatura target (o finale per una rampa)
  float duration_hours;  // durata a temperatura costante (0 se rampa)
  float ramp_hours;      // durata della rampa (0 se costante)
  bool  is_ramp;
};

struct ProfiloFermentazione {
  char        nome[48];
  uint32_t    avvio_epoch;         // timestamp Unix di riferimento, solo informativo (display "avviato il");
                                   // non usato per calcolare la posizione nella curva, vedi secondi_trascorsi
  uint32_t    secondi_trascorsi;   // avanzamento reale del profilo: cresce solo mentre il controllo è
                                   // davvero in corso (RUNNING e sensore utilizzabile). Un blackout, un
                                   // riavvio o un guasto del sensore mettono la curva in pausa invece di
                                   // farla saltare avanti quando l'orologio torna disponibile (era il
                                   // comportamento fino alla 6.3.0, basato su un epoch assoluto — vedi
                                   // docs/11_TIMING_PROFILO.md). Persistito a parte, non nel blob intero.
  ProfiloStep steps[MAX_STEPS];
  int         num_steps;
  bool        valido;
};

// ============================================================
// Funzioni
// ============================================================

// Calcola il setpoint corrente [°C] dal tempo trascorso [s].
// Quando il profilo è terminato ritorna la temp dell'ultimo step.
// Ritorna -999.0 se il profilo non è valido.
float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi);

// Ritorna la durata totale del profilo in ore (0 se non valido).
float calcolaDurataTotaleOre(const ProfiloFermentazione& p);

// Deserializza e valida il profilo dal payload JSON di un comando profile_set.
// Rifiuta (con motivo in reason_buf) step fuori range, durate non valide,
// troppi step, avvio_epoch incoerente. Non tronca mai silenziosamente.
bool parsaProfiloJSON(const char* json_str,
                      ProfiloFermentazione& p,
                      char* reason_buf,
                      size_t reason_buf_size);

// Persistenza NVS (Preferences, namespace NVS_NS_FERM)
void salvaProfilo(const ProfiloFermentazione& p);
bool caricaProfilo(ProfiloFermentazione& p);
void cancellaProfilo();

// Salva l'avanzamento del profilo in una chiave dedicata (non riscrive il blob intero: meno
// usura flash per un checkpoint periodico). caricaProfilo() lo ricarica da solo dentro p.secondi_trascorsi.
void salvaProgresso(uint32_t secondi_trascorsi);

// Persistenza stato FSM per recovery autonomo al boot (§8 firmware brief).
// Salva last_fsm ("RUNNING"|"HOLDING"|"STOPPED"), step_index, ore_trascorse.
void salvaStatoFsm(const char* fsm_str, int step_index, float ore_trascorse);
bool caricaStatoFsm(char* fsm_buf, size_t buf_size, int& step_index, float& ore_trascorse);
void cancellaStatoFsm();

// Persistenza modo di controllo e parametri: dopo un reboot l'ESP riprende
// con lo stesso algoritmo invece di tornare al default.
void salvaConfigControllo(const ControllerConfig& config);
bool caricaConfigControllo(ControllerConfig& config);
