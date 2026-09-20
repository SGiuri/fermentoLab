#pragma once
#include <Arduino.h>

// ============================================================
// Termostato V5 — controllo PID bidirezionale su PWM
//
// Differenze rispetto a V4 (bang-bang con isteresi):
//   - PID con output continuo [-100, +100]:
//       output > 0  → riscaldatore attivo  (duty = output %)
//       output < 0  → pompa glicole attiva (duty = |output| %)
//   - Gain asimmetrici: kp_risc/ki_risc/kd_risc per la cinghia 30W (lenta),
//     kp_frigo/ki_frigo/kd_frigo per la pompa glicole (veloce/potente).
//     I gain si selezionano in base al segno dell'errore:
//       e > 0 (temp < setpoint → serve caldo) → gain riscaldatore
//       e ≤ 0 (temp ≥ setpoint → serve freddo) → gain refrigeratore
//   - PWM: ciclo configurabile (default 60s).
//     All'inizio di ogni ciclo: calcola PID → determina duty → accende relay.
//     A metà ciclo: spegne relay al raggiungimento del tempo ON calcolato.
//   - Impulso minimo pompa: se il duty cooling dà ON < min_pompa_on_s → skip.
//     Evita micro-impulsi inutili; la pompa non è un compressore, non c'è
//     min_cycle tra spegnimento e riaccensione.
//   - Anti-windup: integrale clamped a ±PID_INTEGRAL_MAX; freezing condizionale
//     se l'output è saturato nella direzione dell'errore.
//   - Failsafe sensore: se temp_valida=false → nessun nuovo ciclo PWM avviato;
//     il relay si spegne al termine naturale della fase ON corrente.
// ============================================================

// ============================================================
// Stato PID interno
// ============================================================
struct StatoPID {
  // Gain riscaldatore (cinghia 30W)
  float kp_risc;
  float ki_risc;
  float kd_risc;
  // Gain refrigeratore (pompa glicole)
  float kp_frigo;
  float ki_frigo;
  float kd_frigo;
  // Stato integrale e derivativo
  float         integrale;      // accumulatore integrale [unità: °C·s]
  float         errore_prec;    // errore al passo precedente [°C]
  unsigned long t_calc_ms;      // timestamp ultimo calcolo PID [ms]
  bool          primo_calcolo;  // true = non usare il termine D al primo passo
};

// ============================================================
// Stato completo termostato
// ============================================================
struct StatoTermostato {
  bool  riscaldatore_on;
  bool  refrigeratore_on;

  // Output PID e duty cycle correnti (aggiornati ogni ciclo)
  float pid_output;          // output grezzo PID [-100, +100]
  float duty_riscaldatore;   // % on-time riscaldatore nel ciclo corrente [0-100]
  float duty_refrigeratore;  // % on-time pompa nel ciclo corrente       [0-100]

  // PWM timing
  unsigned long pwm_cycle_start_ms;  // inizio del ciclo corrente [ms]
  unsigned long pwm_on_end_ms;       // fine prevista fase ON [ms]
  bool          pwm_in_on_phase;     // true durante la fase ON

  StatoPID pid;
};

// ============================================================
// API pubblica
// ============================================================

// Inizializza pin, porta relè in OFF, imposta i gain PID iniziali.
// Chiamare in setup() prima di qualsiasi altra funzione del modulo.
void termostato_init(StatoTermostato& s,
                     float kp_risc,  float ki_risc,  float kd_risc,
                     float kp_frigo, float ki_frigo, float kd_frigo,
                     uint32_t pwm_period_s);

// Aggiorna i gain e azzera l'integrale PID.
// Chiamare quando arriva un nuovo profilo con gain diversi.
void termostato_pid_init(StatoTermostato& s,
                         float kp_risc,  float ki_risc,  float kd_risc,
                         float kp_frigo, float ki_frigo, float kd_frigo);

// Macchina a stati PWM + PID: chiamare ogni loop().
// - Se temp_valida=false: non avvia nuovi cicli; il relay si spegne a fine ON.
// - Restituisce true se c'è stato un cambio di stato relay.
bool termostato_aggiorna(StatoTermostato& s,
                         float temp_reale, float setpoint,
                         bool  temp_valida,
                         uint32_t min_pompa_on_s,
                         uint32_t pwm_period_s);

// Spegne entrambi i relè immediatamente (fail-safe su allarme sensore).
// Azzera anche lo stato PWM (nessuna fase ON pendente).
void termostato_spegni_tutto(StatoTermostato& s);
