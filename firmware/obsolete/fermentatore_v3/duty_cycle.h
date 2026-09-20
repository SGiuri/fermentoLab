#pragma once

#include <Arduino.h>
#include "termostato.h"

struct DutyCycleStats {
  float    duty_riscaldamento;
  float    duty_refrigerazione;
  uint32_t osservazione_ms;
  bool     sufficiente;
  bool     stimato;
};

// Inizializza lo storico rolling del duty cycle.
void duty_cycle_init();

// Aggiorna lo storico in base allo stato corrente dei relè.
void duty_cycle_track(unsigned long now_ms, bool risc_on, bool frigo_on);

// Ritorna il duty cycle medio osservato nell'ultima ora.
DutyCycleStats duty_cycle_stats(unsigned long now_ms);

// Applica il duty cycle stimato in fallback.
// prefer_risc / prefer_frigo guidano quale ramo usare quando il sensore non è affidabile.
bool duty_cycle_applica(StatoTermostato& s, unsigned long now_ms,
                        bool prefer_risc, bool prefer_frigo,
                        uint32_t min_cycle_min,
                        DutyCycleStats* stats_out = nullptr);