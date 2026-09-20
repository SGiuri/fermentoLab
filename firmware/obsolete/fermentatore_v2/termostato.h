#pragma once
#include <Arduino.h>

struct StatoTermostato {
  bool          riscaldatore_on;
  bool          refrigeratore_on;
  unsigned long ultimo_off_refrigeratore_ms;  // millis() quando il frigo si è spento
};

// Inizializza i pin e porta i relè in stato OFF sicuro.
void termostato_init(StatoTermostato& stato);

// Aggiorna i relè in base a temp_reale vs setpoint con isteresi.
// Rispetta il min_cycle_minutes per la protezione compressore.
// Restituisce true se c'è stato un cambio di stato.
bool termostato_aggiorna(StatoTermostato& stato,
                          float temp_reale,
                          float setpoint,
                          float isteresi,
                          uint32_t min_cycle_minutes);

// Spegne entrambi i relè (fail-safe in caso di allarme sensore).
void termostato_spegni_tutto(StatoTermostato& stato);
