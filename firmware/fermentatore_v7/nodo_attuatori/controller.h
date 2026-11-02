#pragma once
#include <Arduino.h>

enum class ControlMode : uint8_t {
  ON_OFF = 0,
  ON_OFF_HYST_SYM = 1,
  ON_OFF_HYST_ASYM = 2,
  P = 3,
  PI_CTRL = 4,
  PD_CTRL = 5,
  PID_CTRL = 6
};

enum class RuntimeState : uint8_t {
  STOPPED = 0,
  RUNNING = 1,
  AUTOTUNING = 2
};

struct ControllerConfig {
  ControlMode mode;

  // On/off family
  float hysteresis_sym_c;
  float hysteresis_heat_c;
  float hysteresis_cool_c;

  // PID family
  float kp_heat;
  float ki_heat;
  float kd_heat;
  float kp_cool;
  float ki_cool;
  float kd_cool;
  float output_deadband_pct;
  float min_effective_output_pct;
};

struct ControllerState {
  bool heat_on;
  bool cool_on;
  float heat_duty_pct;
  float cool_duty_pct;

  // PID internals
  float integrale;
  float errore_prec;
  uint32_t ts_calc_ms;
  float temp_prec;            // misura del calcolo precedente (derivata sulla misura)
  bool misura_prec_valida;
  float derivata_filtrata;    // °C/s, filtro del primo ordine (PID_D_TAU_S)

  // Grandezze dell'ultimo calcolo, pubblicate su esp/controllo per la didattica
  float errore;               // SP - T [°C]
  float p_term;               // [%]
  float i_term;               // [%]
  float d_term;               // [%]
  float uscita_pct;           // P + I + D limitata a ±100, prima di zona morta e uscita minima

  RuntimeState runtime_state;
  float last_temp_c;
  float last_setpoint_c;
  uint32_t last_apply_ms;
};

void controller_init(ControllerState& state, const ControllerConfig& config);
void controller_reset_state(ControllerState& state);
void controller_stop_outputs(ControllerState& state);
bool controller_set_mode(ControllerState& state,
                         ControllerConfig& config,
                         ControlMode requested_mode,
                         char* reason_buf,
                         size_t reason_buf_size);
void controller_apply_inputs(ControllerState& state,
                             const ControllerConfig& config,
                             float temp_c,
                             float setpoint_c,
                             uint32_t now_ms);
const char* controller_runtime_state_to_string(RuntimeState state);
const char* controller_mode_to_string(ControlMode mode);
