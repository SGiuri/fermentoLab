#pragma once
#include <Arduino.h>

// ============================================================
// Stadio fisico delle uscite relè — unico punto che scrive i pin relè.
//
// - finestra time-proportional per i modi P/PI/PD/PID (PID_DUTY_WINDOW_MS)
// - impulso minimo, tempo minimo ON/OFF, attesa al cambio direzione
// - interlock: caldo e freddo mai accesi insieme
// - divieti di sicurezza sulla temperatura, indipendenti dal modo di controllo
//
// Gli spegnimenti di sicurezza (forza_spegnimento, divieti, interlock) sono
// immediati e non rispettano il tempo minimo ON.
// ============================================================

struct RichiestaUscite {
  bool  heat_on;         // richiesta booleana (modi on/off)
  bool  cool_on;
  float heat_duty_pct;   // > 0 → modo duty (P/PI/PD/PID)
  float cool_duty_pct;
};

// Da chiamare come prima istruzione di setup(): relè OFF e timer inizializzati.
void uscite_init();

// Da chiamare a ogni loop.
// forza_spegnimento: controllo fermo, sensore non utilizzabile, OTA, riavvio.
void uscite_aggiorna(const RichiestaUscite& richiesta,
                     bool forza_spegnimento,
                     float temp_c,
                     float setpoint_c,
                     unsigned long now_ms);

bool uscite_riscaldatore_acceso();
bool uscite_refrigeratore_acceso();
uint32_t uscite_commutazioni();  // commutazioni relè dal boot (diagnostica usura)
