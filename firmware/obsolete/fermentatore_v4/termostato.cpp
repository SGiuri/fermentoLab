#include "termostato.h"
#include "config.h"

static bool lockout_attivo(unsigned long ora,
                          unsigned long ultimo_off_ms,
                          unsigned long lockout_ms) {
  if (ultimo_off_ms == 0 || lockout_ms == 0) return false;
  return (ora - ultimo_off_ms) < lockout_ms;
}

bool termostato_imposta(StatoTermostato& s,
                        bool risc_target, bool frigo_target,
                        uint32_t min_cycle_sec) {
  if (risc_target && frigo_target) {
    risc_target = false;
    frigo_target = false;
  }

  unsigned long ora    = millis();
  unsigned long min_ms = (unsigned long)min_cycle_sec * 1000UL;
  unsigned long switchover_ms = (unsigned long)DEFAULT_SWITCHOVER_LOCKOUT_SEC * 1000UL;

  // Protezione min OFF: non riaccendere prima di min_cycle_sec dall'ultimo spegnimento
  if (risc_target && !s.riscaldatore_on && s.ultimo_off_risc_ms > 0) {
    if (ora - s.ultimo_off_risc_ms < min_ms) risc_target = false;
  }
  if (frigo_target && !s.refrigeratore_on && s.ultimo_off_frigo_ms > 0) {
    if (ora - s.ultimo_off_frigo_ms < min_ms) frigo_target = false;
  }

  // Lockout incrociato: evita di passare troppo rapidamente da caldo a freddo
  // o viceversa dopo lo spegnimento dell'attuatore opposto.
  if (risc_target && !s.riscaldatore_on) {
    if (lockout_attivo(ora, s.ultimo_off_frigo_ms, switchover_ms)) risc_target = false;
  }
  if (frigo_target && !s.refrigeratore_on) {
    if (lockout_attivo(ora, s.ultimo_off_risc_ms, switchover_ms)) frigo_target = false;
  }

  bool cambiato = false;

  if (risc_target != s.riscaldatore_on) {
    if (!risc_target) s.ultimo_off_risc_ms = ora;
    s.riscaldatore_on = risc_target;
    digitalWrite(PIN_RISCALDATORE, risc_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  if (frigo_target != s.refrigeratore_on) {
    if (!frigo_target) s.ultimo_off_frigo_ms = ora;
    s.refrigeratore_on = frigo_target;
    digitalWrite(PIN_REFRIGERATORE, frigo_target ? RELAY_ON : RELAY_OFF);
    cambiato = true;
  }

  return cambiato;
}

void termostato_init(StatoTermostato& s) {
  s.riscaldatore_on    = false;
  s.refrigeratore_on   = false;
  s.ultimo_off_risc_ms  = 0;
  s.ultimo_off_frigo_ms = 0;

  // Inizializza pin e porta relè in OFF prima di qualsiasi altra operazione
  pinMode(PIN_RISCALDATORE,  OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  digitalWrite(PIN_RISCALDATORE,  RELAY_OFF);
  digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
}

bool termostato_aggiorna(StatoTermostato& s,
                          float temp_reale, float setpoint,
                          float isteresi, uint32_t min_cycle_sec,
                          float anticip_frigo) {
  // Partenza da stato attuale (banda morta: nessun cambio immediato)
  bool risc_target  = s.riscaldatore_on;
  bool frigo_target = s.refrigeratore_on;

  // Attivazione ai bordi della banda di isteresi.
  if (temp_reale <= (setpoint - isteresi)) {
    // Troppo freddo: entra in riscaldamento.
    risc_target  = true;
    frigo_target = false;
  } else if (temp_reale >= (setpoint + isteresi)) {
    // Troppo caldo: entra in raffreddamento.
    risc_target  = false;
    frigo_target = true;
  } else {
    // Dentro la banda: lascia lavorare l'inerzia termica.
    // Il frigo si spegne a (setpoint + anticip_frigo) per compensare l'inerzia
    // dell'evaporatore; il riscaldatore si spegne al raggiungimento del setpoint.
    if (s.refrigeratore_on && temp_reale <= (setpoint + anticip_frigo)) {
      frigo_target = false;
    }
    if (s.riscaldatore_on && temp_reale >= setpoint) {
      risc_target = false;
    }
  }

  return termostato_imposta(s, risc_target, frigo_target, min_cycle_sec);
}

void termostato_spegni_tutto(StatoTermostato& s) {
  if (s.riscaldatore_on) {
    s.ultimo_off_risc_ms = millis();
    s.riscaldatore_on = false;
    digitalWrite(PIN_RISCALDATORE, RELAY_OFF);
  }
  if (s.refrigeratore_on) {
    s.ultimo_off_frigo_ms = millis();
    s.refrigeratore_on    = false;
    digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);
  }
}
