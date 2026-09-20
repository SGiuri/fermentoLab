#include "termostato.h"
#include "config.h"

void termostato_init(StatoTermostato& stato) {
  stato.riscaldatore_on              = false;
  stato.refrigeratore_on             = false;
  stato.ultimo_off_refrigeratore_ms  = 0;

  pinMode(PIN_RISCALDATORE,  OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  digitalWrite(PIN_RISCALDATORE,  RELAY_OFF);
  digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
}

bool termostato_aggiorna(StatoTermostato& stato,
                          float temp_reale,
                          float setpoint,
                          float isteresi,
                          uint32_t min_cycle_minutes) {
  bool risc_target  = stato.riscaldatore_on;
  bool frigo_target = stato.refrigeratore_on;
  bool cambiato     = false;

  if (temp_reale < (setpoint - isteresi)) {
    // Troppo freddo → riscalda
    risc_target  = true;
    frigo_target = false;
  } else if (temp_reale > (setpoint + isteresi)) {
    // Troppo caldo → raffredda
    risc_target  = false;
    frigo_target = true;
  }
  // Zona neutra: nessuna variazione

  // Protezione compressore: non riaccendere il frigo prima di min_cycle_minutes
  if (frigo_target && !stato.refrigeratore_on) {
    unsigned long ms_da_spegnimento = millis() - stato.ultimo_off_refrigeratore_ms;
    unsigned long min_ms = (unsigned long)min_cycle_minutes * 60UL * 1000UL;
    if (stato.ultimo_off_refrigeratore_ms > 0 && ms_da_spegnimento < min_ms) {
      frigo_target = false;  // Troppo presto, aspetta ancora
    }
  }

  // Applica riscaldatore
  if (risc_target != stato.riscaldatore_on) {
    stato.riscaldatore_on = risc_target;
    digitalWrite(PIN_RISCALDATORE, risc_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  // Applica refrigeratore
  if (frigo_target != stato.refrigeratore_on) {
    if (!frigo_target) {
      stato.ultimo_off_refrigeratore_ms = millis();
    }
    stato.refrigeratore_on = frigo_target;
    digitalWrite(PIN_REFRIGERATORE, frigo_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  return cambiato;
}

void termostato_spegni_tutto(StatoTermostato& stato) {
  if (stato.riscaldatore_on) {
    stato.riscaldatore_on = false;
    digitalWrite(PIN_RISCALDATORE, RELAY_OFF);
  }
  if (stato.refrigeratore_on) {
    stato.ultimo_off_refrigeratore_ms = millis();
    stato.refrigeratore_on = false;
    digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
  }
}
