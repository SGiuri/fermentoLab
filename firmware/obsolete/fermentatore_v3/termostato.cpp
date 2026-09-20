#include "termostato.h"
#include "config.h"

bool termostato_imposta(StatoTermostato& s,
                        bool risc_target, bool frigo_target,
                        uint32_t min_cycle_min) {
  if (risc_target && frigo_target) {
    risc_target = false;
    frigo_target = false;
  }

  // Protezione compressore: non riaccendere il frigo troppo presto
  if (frigo_target && !s.refrigeratore_on && s.ultimo_off_frigo_ms > 0) {
    unsigned long ms_off = millis() - s.ultimo_off_frigo_ms;
    unsigned long min_ms = (unsigned long)min_cycle_min * 60UL * 1000UL;
    if (ms_off < min_ms) {
      frigo_target = false;
    }
  }

  bool cambiato = false;

  if (risc_target != s.riscaldatore_on) {
    s.riscaldatore_on = risc_target;
    digitalWrite(PIN_RISCALDATORE, risc_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  if (frigo_target != s.refrigeratore_on) {
    if (!frigo_target) {
      s.ultimo_off_frigo_ms = millis();
    }
    s.refrigeratore_on = frigo_target;
    digitalWrite(PIN_REFRIGERATORE, frigo_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  return cambiato;
}

void termostato_init(StatoTermostato& s) {
  s.riscaldatore_on  = false;
  s.refrigeratore_on = false;
  s.ultimo_off_frigo_ms = 0;

  // Inizializza pin e porta relè in OFF prima di qualsiasi altra operazione
  pinMode(PIN_RISCALDATORE,  OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  digitalWrite(PIN_RISCALDATORE,  RELAY_OFF);
  digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
}

bool termostato_aggiorna(StatoTermostato& s,
                          float temp_reale, float setpoint,
                          float isteresi, uint32_t min_cycle_min) {
  // Partenza da stato attuale (zona neutra: nessun cambio)
  bool risc_target  = s.riscaldatore_on;
  bool frigo_target = s.refrigeratore_on;

  // Logica isteresi: due soglie simmetriche attorno al setpoint
  if (temp_reale < (setpoint - isteresi)) {
    // Troppo freddo → riscalda, frigo off
    risc_target  = true;
    frigo_target = false;
  } else if (temp_reale > (setpoint + isteresi)) {
    // Troppo caldo → frigo, riscaldatore off
    risc_target  = false;
    frigo_target = true;
  }
  // Zona neutra [setpoint±isteresi]: nessuna variazione (evita chattering)

  return termostato_imposta(s, risc_target, frigo_target, min_cycle_min);
}

void termostato_spegni_tutto(StatoTermostato& s) {
  if (s.riscaldatore_on) {
    s.riscaldatore_on = false;
    digitalWrite(PIN_RISCALDATORE, RELAY_OFF);
  }
  if (s.refrigeratore_on) {
    s.ultimo_off_frigo_ms = millis();
    s.refrigeratore_on    = false;
    digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
  }
}
