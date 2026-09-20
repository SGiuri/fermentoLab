#pragma once
#include <Arduino.h>

struct StatoTermostato {
  bool          riscaldatore_on;
  bool          refrigeratore_on;
  unsigned long ultimo_off_frigo_ms;   // millis() quando frigo si è spento l'ultima volta
};

// Imposta i pin OUTPUT e porta i relè in stato OFF sicuro.
void termostato_init(StatoTermostato& s);

// Aggiorna i relè in base a temp_reale vs setpoint con isteresi.
// Rispetta min_cycle_min per proteggere il compressore.
// Ritorna true se c'è stato almeno un cambio di stato.
bool termostato_aggiorna(StatoTermostato& s,
                          float temp_reale, float setpoint,
                          float isteresi, uint32_t min_cycle_min);

// Imposta esplicitamente i relè, mantenendo mutua esclusione e protezione compressore.
// Se entrambi i target sono true, prevale il fail-safe e vengono spenti entrambi.
bool termostato_imposta(StatoTermostato& s,
                        bool risc_target, bool frigo_target,
                        uint32_t min_cycle_min);

// Spegne entrambi i relè (fail-safe su allarme sensore o stop).
void termostato_spegni_tutto(StatoTermostato& s);
