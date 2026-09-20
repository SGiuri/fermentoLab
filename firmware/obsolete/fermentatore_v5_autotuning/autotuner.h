#pragma once
#include <Arduino.h>

// ============================================================
// Autotuner — Relay-feedback method (Åström-Hägglund)
//
// Principio:
//   Invece del PID si usa un controllo ON/OFF (relay) con ampiezza fissa.
//   Il sistema oscilla attorno al setpoint. Quando si osservano ≥3 cicli
//   completi di oscillazione, si calcolano:
//     - Au  = ampiezza media delle oscillazioni temperatura [°C]
//     - Tu  = periodo medio di oscillazione [s]
//     - Ku  = 4 * relay_amplitude_pct / (π * Au)  (guadagno critico %)
//
//   Con lo schema Ziegler-Nichols PI:
//     kp = 0.45 * Ku
//     ki = 0.54 * Ku / Tu   [1/s]
//     kd = 0.0  (non usiamo il derivativo: rumore DS18B20)
//
// Utilizzo:
//   1. autotuner_init(setpoint, relay_amplitude_pct)
//   2. Loop: done = autotuner_aggiorna(temp, &relay_on)
//      → imposta il relay in base all'output del relay-feedback
//   3. Se done: autotuner_get_gains(&kp, &ki, &kd)
//
// Sicurezza:
//   L'abort per temperatura limite e il timeout sono responsabilità
//   del chiamante (FSM in fermentatore_v5_autotuning.ino).
// ============================================================

void  autotuner_init(float setpoint, float relay_amplitude_pct);

// Aggiorna lo stato interno.
// relay_on_out: true = accendi l'attuatore in test; false = spegnilo.
// Ritorna true quando ≥3 cicli puliti sono stati osservati e i gain sono pronti.
bool  autotuner_aggiorna(float temp_reale, bool& relay_on_out);

// Risultati (validi solo dopo autotuner_aggiorna() == true)
void  autotuner_get_gains(float& kp_out, float& ki_out, float& kd_out);
float autotuner_get_Tu();   // periodo di oscillazione [s]
float autotuner_get_Au();   // ampiezza temperatura [°C]
