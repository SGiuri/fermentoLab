#include "controller.h"
#include "config.h"
#include "controller_onoff.h"
#include <math.h>
#include <stdio.h>

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void set_output_from_pid(ControllerState& state,
                                float output_pct,
                                float min_effective_pct,
                                float deadband_pct) {
  const float out = clampf(output_pct, -100.0f, 100.0f);
  const float deadband = (deadband_pct >= 0.0f) ? deadband_pct : 0.0f;
  const float abs_out = fabsf(out);

  state.heat_on = false;
  state.cool_on = false;
  state.heat_duty_pct = 0.0f;
  state.cool_duty_pct = 0.0f;

  if (abs_out <= deadband) {
    return;
  }

  if (out > 0.0f) {
    const float duty = (abs_out < min_effective_pct) ? min_effective_pct : abs_out;
    state.heat_on = true;
    state.heat_duty_pct = clampf(duty, 0.0f, 100.0f);
  } else {
    const float duty = (abs_out < min_effective_pct) ? min_effective_pct : abs_out;
    state.cool_on = true;
    state.cool_duty_pct = clampf(duty, 0.0f, 100.0f);
  }
}

static void apply_pid_family(ControllerState& state,
                             const ControllerConfig& config,
                             float temp_c,
                             float setpoint_c,
                             uint32_t now_ms,
                             bool enable_i,
                             bool enable_d) {
  // Chiamata a ogni nuova lettura del sensore: dt è l'intervallo tra due misure.
  float dt_s = (float)SENSOR_READ_INTERVAL_MS / 1000.0f;
  if (state.ts_calc_ms > 0 && now_ms > state.ts_calc_ms) {
    dt_s = (float)(now_ms - state.ts_calc_ms) / 1000.0f;
    if (dt_s < 0.01f) dt_s = 0.01f;
    if (dt_s > 60.0f) dt_s = 60.0f;  // dopo una pausa lunga l'integrale non deve saltare
  }

  const float errore = setpoint_c - temp_c;
  const bool heat_side = (errore >= 0.0f);

  const float kp = heat_side ? config.kp_heat : config.kp_cool;
  const float ki = heat_side ? config.ki_heat : config.ki_cool;
  const float kd = heat_side ? config.kd_heat : config.kd_cool;

  const float p_term = kp * errore;

  // Derivata sulla misura, filtrata. Sulla misura e non sull'errore: un cambio di setpoint
  // non produce il "calcio derivativo". Filtrata: la quantizzazione del DS18B20 (0,0625 °C)
  // altrimenti genera picchi a ogni lettura. Prima il PID girava ogni secondo su una lettura
  // aggiornata ogni 5 s: il termine D era zero per 4 cicli e poi un picco.
  if (state.misura_prec_valida && state.ts_calc_ms > 0) {
    const float derivata = (temp_c - state.temp_prec) / dt_s;
    const float alfa = expf(-dt_s / PID_D_TAU_S);
    state.derivata_filtrata = alfa * state.derivata_filtrata + (1.0f - alfa) * derivata;
  } else {
    state.derivata_filtrata = 0.0f;
  }
  const float d_term = enable_d ? -kd * state.derivata_filtrata : 0.0f;

  // Integrale con anti-windup:
  // - non integra quando l'uscita è già saturata nella direzione dell'errore;
  // - termine I limitato a ±PID_I_MAX_PCT.
  // Prima l'integrale era limitato a ±300 °C·s indipendentemente da Ki: con Ki piccoli (es. 0,001)
  // il termine I non superava lo 0,3 % e il PI non annullava l'errore a regime (simulazione 48 h
  // con i guadagni del Lab: 3 °C sopra il setpoint). Una banda di separazione (integrare solo
  // vicino al setpoint) è stata provata e scartata: con Kp basso l'errore non rientra mai nella
  // banda e il PI resta fermo allo stesso offset.
  if (!enable_i) {
    state.integrale = 0.0f;
  } else {
    const float uscita_prima = p_term + ki * state.integrale + d_term;
    const bool saturata = (uscita_prima >= 100.0f && errore > 0.0f) ||
                          (uscita_prima <= -100.0f && errore < 0.0f);
    if (!saturata) {
      state.integrale += errore * dt_s;
    }
    if (ki > 0.0f) {
      const float limite = PID_I_MAX_PCT / ki;
      state.integrale = clampf(state.integrale, -limite, limite);
    }
  }
  const float i_term = enable_i ? (ki * state.integrale) : 0.0f;

  const float output = p_term + i_term + d_term;
  set_output_from_pid(state,
                      output,
                      config.min_effective_output_pct,
                      config.output_deadband_pct);

  state.errore = errore;
  state.p_term = p_term;
  state.i_term = i_term;
  state.d_term = d_term;
  state.uscita_pct = clampf(output, -100.0f, 100.0f);
  state.errore_prec = errore;
  state.temp_prec = temp_c;
  state.misura_prec_valida = true;
  state.ts_calc_ms = now_ms;
}

void controller_init(ControllerState& state, const ControllerConfig& config) {
  (void)config;
  controller_reset_state(state);
}

void controller_reset_state(ControllerState& state) {
  state.heat_on = false;
  state.cool_on = false;
  state.heat_duty_pct = 0.0f;
  state.cool_duty_pct = 0.0f;
  state.integrale = 0.0f;
  state.errore_prec = 0.0f;
  state.ts_calc_ms = 0;
  state.temp_prec = 0.0f;
  state.misura_prec_valida = false;
  state.derivata_filtrata = 0.0f;
  state.errore = 0.0f;
  state.p_term = 0.0f;
  state.i_term = 0.0f;
  state.d_term = 0.0f;
  state.uscita_pct = 0.0f;
  state.runtime_state = RuntimeState::STOPPED;
  state.last_temp_c = 0.0f;
  state.last_setpoint_c = 0.0f;
  state.last_apply_ms = 0;
}

void controller_stop_outputs(ControllerState& state) {
  state.heat_on = false;
  state.cool_on = false;
  state.heat_duty_pct = 0.0f;
  state.cool_duty_pct = 0.0f;
  state.p_term = 0.0f;
  state.i_term = 0.0f;
  state.d_term = 0.0f;
  state.uscita_pct = 0.0f;
}

bool controller_set_mode(ControllerState& state,
                         ControllerConfig& config,
                         ControlMode requested_mode,
                         char* reason_buf,
                         size_t reason_buf_size) {
  if (state.runtime_state != RuntimeState::STOPPED) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf,
               reason_buf_size,
               "mode_change_requires_stopped_state");
    }
    return false;
  }

  config.mode = requested_mode;
  state.integrale = 0.0f;
  state.errore_prec = 0.0f;
  state.ts_calc_ms = 0;

  if (reason_buf && reason_buf_size > 0) {
    snprintf(reason_buf, reason_buf_size, "ok");
  }
  return true;
}

void controller_apply_inputs(ControllerState& state,
                             const ControllerConfig& config,
                             float temp_c,
                             float setpoint_c,
                             uint32_t now_ms) {
  if (state.runtime_state != RuntimeState::RUNNING) {
    controller_stop_outputs(state);
    state.errore = setpoint_c - temp_c;
    state.last_temp_c = temp_c;
    state.last_setpoint_c = setpoint_c;
    state.last_apply_ms = now_ms;
    return;
  }

  // Setpoint limitato per tutti i modi: prima il clamp esisteva solo nei modi on/off
  // e P/PI/PD/PID ricevevano il valore del profilo senza alcun limite.
  if (!isfinite(setpoint_c)) {
    controller_stop_outputs(state);
    return;
  }
  setpoint_c = clampf(setpoint_c, DEFAULT_MIN_SETPOINT_C, DEFAULT_MAX_SETPOINT_C);

  // Modi on/off: nessun termine P/I/D; l'uscita è la richiesta del relè (+100 caldo, -100 freddo).
  // I modi PID sovrascrivono questi valori in apply_pid_family().
  state.errore = setpoint_c - temp_c;
  state.p_term = 0.0f;
  state.i_term = 0.0f;
  state.d_term = 0.0f;

  switch (config.mode) {
    case ControlMode::ON_OFF:
      controller_apply_on_off(state, temp_c, setpoint_c);
      break;

    case ControlMode::ON_OFF_HYST_SYM:
      controller_apply_on_off_hyst_sym(state, temp_c, setpoint_c, config.hysteresis_sym_c);
      break;

    case ControlMode::ON_OFF_HYST_ASYM:
      controller_apply_on_off_hyst_asym(state,
                                        temp_c,
                                        setpoint_c,
                                        config.hysteresis_heat_c,
                                        config.hysteresis_cool_c);
      break;

    case ControlMode::P:
      apply_pid_family(state, config, temp_c, setpoint_c, now_ms, false, false);
      break;

    case ControlMode::PI_CTRL:
      apply_pid_family(state, config, temp_c, setpoint_c, now_ms, true, false);
      break;

    case ControlMode::PD_CTRL:
      apply_pid_family(state, config, temp_c, setpoint_c, now_ms, false, true);
      break;

    case ControlMode::PID_CTRL:
      apply_pid_family(state, config, temp_c, setpoint_c, now_ms, true, true);
      break;

    default:
      controller_stop_outputs(state);
      break;
  }

  if (config.mode == ControlMode::ON_OFF || config.mode == ControlMode::ON_OFF_HYST_SYM ||
      config.mode == ControlMode::ON_OFF_HYST_ASYM) {
    state.uscita_pct = state.heat_on ? 100.0f : (state.cool_on ? -100.0f : 0.0f);
  }

  state.last_temp_c = temp_c;
  state.last_setpoint_c = setpoint_c;
  state.last_apply_ms = now_ms;
}

const char* controller_runtime_state_to_string(RuntimeState state) {
  switch (state) {
    case RuntimeState::STOPPED:
      return "stopped";
    case RuntimeState::RUNNING:
      return "running";
    case RuntimeState::AUTOTUNING:
      return "autotuning";
    default:
      return "unknown";
  }
}

const char* controller_mode_to_string(ControlMode mode) {
  switch (mode) {
    case ControlMode::ON_OFF:
      return "on_off";
    case ControlMode::ON_OFF_HYST_SYM:
      return "on_off_hyst_sym";
    case ControlMode::ON_OFF_HYST_ASYM:
      return "on_off_hyst_asym";
    case ControlMode::P:
      return "P";
    case ControlMode::PI_CTRL:
      return "PI";
    case ControlMode::PD_CTRL:
      return "PD";
    case ControlMode::PID_CTRL:
      return "PID";
    default:
      return "unknown";
  }
}
