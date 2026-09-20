#pragma once
#include <Arduino.h>

struct StatoTermostato {
  bool          riscaldatore_on;
  bool          refrigeratore_on;
  unsigned long ultimo_off_risc_ms;    // millis() quando riscaldatore si è spento l'ultima volta
  unsigned long ultimo_off_frigo_ms;   // millis() quando frigo si è spento l'ultima volta
};

// Imposta i pin OUTPUT e porta i relè in stato OFF sicuro.
void termostato_init(StatoTermostato& s);

// Aggiorna i relè in base a temp_reale vs setpoint con isteresi.
// Attiva ai bordi [setpoint +/- isteresi] e spegne al raggiungimento del setpoint,
// lasciando la zona centrale alla sola inerzia termica.
// anticip_frigo (default 0): il frigo si spegne a (setpoint + anticip_frigo) invece di setpoint,
// compensando l'inerzia termica dell'evaporatore che continua a raffreddare dopo lo spegnimento.
// Rispetta min_cycle_sec (secondi) come tempo minimo tra spegnimento e riaccensione
// e applica un lockout incrociato caldo<->freddo dopo lo spegnimento dell'altro attuatore.
// Ritorna true se c'è stato almeno un cambio di stato.
bool termostato_aggiorna(StatoTermostato& s,
                          float temp_reale, float setpoint,
                          float isteresi, uint32_t min_cycle_sec,
                          float anticip_frigo = 0.0f);

// Imposta esplicitamente i relè, mantenendo mutua esclusione, protezione tempi minimi
// e lockout temporale tra caldo e freddo.
// Se entrambi i target sono true, prevale il fail-safe e vengono spenti entrambi.
bool termostato_imposta(StatoTermostato& s,
                        bool risc_target, bool frigo_target,
                        uint32_t min_cycle_sec);

// Spegne entrambi i relè (fail-safe su allarme sensore o stop).
void termostato_spegni_tutto(StatoTermostato& s);
