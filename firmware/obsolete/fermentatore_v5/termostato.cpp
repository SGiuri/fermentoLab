#include "termostato.h"
#include "config.h"
#include <math.h>

// ============================================================
// Helpers interni
// ============================================================

static void _relay_set(int pin, bool on) {
  digitalWrite(pin, on ? RELAY_ON : RELAY_OFF);
}

static float _clamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// ============================================================
// Calcolo PID
//
// Restituisce output ∈ [-100, +100]:
//   > 0  → riscaldamento necessario (duty riscaldatore)
//   < 0  → raffreddamento necessario (duty pompa glicole)
//   = 0  → nessun attuatore
//
// Gain selezionati in base al segno dell'errore:
//   e > 0  → temp sotto setpoint → serve caldo → gain riscaldatore
//   e ≤ 0  → temp sopra setpoint → serve freddo → gain refrigeratore
// ============================================================
static float _pid_compute(StatoPID& pid, float temp_reale, float setpoint) {
  unsigned long ora = millis();

  // dt in secondi (usando tempo reale, non il periodo nominale)
  float dt;
  if (pid.t_calc_ms == 0) {
    dt = (float)DEFAULT_PWM_PERIOD_S;  // primo calcolo assoluto: usa periodo nominale
  } else {
    dt = (float)(ora - pid.t_calc_ms) / 1000.0f;
  }
  if (dt <= 0.0f) dt = 1.0f;  // guardia contro dt nullo o negativo

  float e = setpoint - temp_reale;  // errore: positivo = troppo freddo, negativo = troppo caldo

  // Seleziona gain in base al segno dell'errore
  float kp = (e > 0.0f) ? pid.kp_risc  : pid.kp_frigo;
  float ki = (e > 0.0f) ? pid.ki_risc  : pid.ki_frigo;
  float kd = (e > 0.0f) ? pid.kd_risc  : pid.kd_frigo;

  // Termine proporzionale
  float p_term = kp * e;

  // Termine derivativo (zero al primo calcolo per evitare spike)
  float d_term = 0.0f;
  if (!pid.primo_calcolo && kd > 0.0f) {
    d_term = kd * (e - pid.errore_prec) / dt;
  }

  // Anti-windup: calcola output tentativo con integrale attuale
  float output_tent = p_term + ki * pid.integrale + d_term;

  // Integra solo se l'output non è già saturato nella stessa direzione dell'errore
  // (conditional integration anti-windup)
  bool saturato_pos = (output_tent >= 100.0f) && (e > 0.0f);
  bool saturato_neg = (output_tent <= -100.0f) && (e < 0.0f);
  if (!saturato_pos && !saturato_neg) {
    pid.integrale += e * dt;
  }

  // Clamp hard sull'integrale come seconda linea di difesa
  pid.integrale = _clamp(pid.integrale, -PID_INTEGRAL_MAX, PID_INTEGRAL_MAX);

  // Output finale
  float output = p_term + ki * pid.integrale + d_term;
  output = _clamp(output, -100.0f, 100.0f);

  // Aggiorna stato PID
  pid.errore_prec   = e;
  pid.t_calc_ms     = ora;
  pid.primo_calcolo = false;

  return output;
}

// ============================================================
// API pubblica
// ============================================================

void termostato_init(StatoTermostato& s,
                     float kp_risc,  float ki_risc,  float kd_risc,
                     float kp_frigo, float ki_frigo, float kd_frigo,
                     uint32_t pwm_period_s) {
  // Pin in stato sicuro prima di tutto
  pinMode(PIN_RISCALDATORE,  OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  _relay_set(PIN_RISCALDATORE,  false);
  _relay_set(PIN_REFRIGERATORE, false);

  // Stato relay
  s.riscaldatore_on  = false;
  s.refrigeratore_on = false;

  // Output e duty
  s.pid_output         = 0.0f;
  s.duty_riscaldatore  = 0.0f;
  s.duty_refrigeratore = 0.0f;

  // PWM: ciclo inizia subito (al primo aggiorna() scatterà il primo calcolo)
  s.pwm_cycle_start_ms = millis();
  s.pwm_on_end_ms      = 0;
  s.pwm_in_on_phase    = false;

  // PID
  termostato_pid_init(s, kp_risc, ki_risc, kd_risc, kp_frigo, ki_frigo, kd_frigo);
}

void termostato_pid_init(StatoTermostato& s,
                         float kp_risc,  float ki_risc,  float kd_risc,
                         float kp_frigo, float ki_frigo, float kd_frigo) {
  s.pid.kp_risc  = kp_risc;
  s.pid.ki_risc  = ki_risc;
  s.pid.kd_risc  = kd_risc;
  s.pid.kp_frigo = kp_frigo;
  s.pid.ki_frigo = ki_frigo;
  s.pid.kd_frigo = kd_frigo;

  // Azzera stato integrale e derivativo
  s.pid.integrale     = 0.0f;
  s.pid.errore_prec   = 0.0f;
  s.pid.t_calc_ms     = 0;
  s.pid.primo_calcolo = true;
}

bool termostato_aggiorna(StatoTermostato& s,
                         float temp_reale, float setpoint,
                         bool  temp_valida,
                         uint32_t min_pompa_on_s,
                         uint32_t pwm_period_s) {
  unsigned long ora        = millis();
  unsigned long period_ms  = (unsigned long)pwm_period_s * 1000UL;
  bool          cambiato   = false;

  // ── Fase ON in corso: controlla se è ora di spegnere ─────────────────────
  if (s.pwm_in_on_phase) {
    if (ora >= s.pwm_on_end_ms) {
      // Fine fase ON: spegni il relay attivo
      if (s.riscaldatore_on) {
        s.riscaldatore_on = false;
        _relay_set(PIN_RISCALDATORE, false);
        cambiato = true;
      }
      if (s.refrigeratore_on) {
        s.refrigeratore_on = false;
        _relay_set(PIN_REFRIGERATORE, false);
        cambiato = true;
      }
      s.pwm_in_on_phase = false;
    }
    // Relay ancora in ON: non fare nulla finché non scade
    return cambiato;
  }

  // ── Fase OFF: controlla se è ora di iniziare un nuovo ciclo ──────────────
  if (ora - s.pwm_cycle_start_ms < period_ms) {
    // Ciclo non ancora scaduto: resta in OFF
    return cambiato;
  }

  // ── Inizio nuovo ciclo ────────────────────────────────────────────────────
  s.pwm_cycle_start_ms = ora;

  // Se il sensore non è valido non avviamo nessun nuovo ciclo.
  // Il PID non viene calcolato per non corrompere l'integrale con dati inaffidabili.
  if (!temp_valida) {
    s.pid_output         = 0.0f;
    s.duty_riscaldatore  = 0.0f;
    s.duty_refrigeratore = 0.0f;
    return cambiato;
  }

  // Calcola PID
  float output = _pid_compute(s.pid, temp_reale, setpoint);
  s.pid_output = output;

  // Determina quale relay attivare e per quanto
  float duty_pct    = fabsf(output);           // 0-100 %
  unsigned long on_ms = (unsigned long)(duty_pct * (float)period_ms / 100.0f);

  bool usa_risc  = (output > 0.0f);
  bool usa_frigo = (output < 0.0f);

  // Guard: impulso minimo pompa
  // Se il tempo ON calcolato per il raffreddamento è troppo breve → skip.
  // Evita micro-impulsi che non consegnano calore significativo.
  if (usa_frigo) {
    unsigned long min_pompa_ms = (unsigned long)min_pompa_on_s * 1000UL;
    if (on_ms < min_pompa_ms) {
      usa_frigo = false;
      on_ms     = 0;
    }
  }

  // Aggiorna duty cycle pubblicato su MQTT
  if (usa_risc) {
    s.duty_riscaldatore  = duty_pct;
    s.duty_refrigeratore = 0.0f;
  } else if (usa_frigo) {
    s.duty_riscaldatore  = 0.0f;
    s.duty_refrigeratore = duty_pct;
  } else {
    s.duty_riscaldatore  = 0.0f;
    s.duty_refrigeratore = 0.0f;
  }

  // Avvia fase ON se il tempo ON è > 0
  if (on_ms > 0 && (usa_risc || usa_frigo)) {
    s.pwm_on_end_ms   = ora + on_ms;
    s.pwm_in_on_phase = true;

    if (usa_risc && !s.riscaldatore_on) {
      s.riscaldatore_on = true;
      _relay_set(PIN_RISCALDATORE, true);
      cambiato = true;
    }
    if (usa_frigo && !s.refrigeratore_on) {
      s.refrigeratore_on = true;
      _relay_set(PIN_REFRIGERATORE, true);
      cambiato = true;
    }
  }

  return cambiato;
}

void termostato_spegni_tutto(StatoTermostato& s) {
  if (s.riscaldatore_on) {
    _relay_set(PIN_RISCALDATORE, false);
    s.riscaldatore_on = false;
  }
  if (s.refrigeratore_on) {
    _relay_set(PIN_REFRIGERATORE, false);
    s.refrigeratore_on = false;
  }
  // Azzera anche lo stato PWM: nessuna fase ON pendente
  s.pwm_in_on_phase    = false;
  s.pwm_on_end_ms      = 0;
  s.pwm_cycle_start_ms = millis();  // riparti da un ciclo pulito al ripristino
  s.duty_riscaldatore  = 0.0f;
  s.duty_refrigeratore = 0.0f;
  s.pid_output         = 0.0f;
}
