#include "controller_onoff.h"
#include "config.h"

static float clamp_setpoint(float setpoint_c) {
  if (setpoint_c < DEFAULT_MIN_SETPOINT_C) return DEFAULT_MIN_SETPOINT_C;
  if (setpoint_c > DEFAULT_MAX_SETPOINT_C) return DEFAULT_MAX_SETPOINT_C;
  return setpoint_c;
}

void controller_apply_on_off(ControllerState& state, float temp_c, float setpoint_c) {
  const float sp = clamp_setpoint(setpoint_c);

  if (temp_c < sp) {
    state.heat_on = true;
    state.cool_on = false;
  } else if (temp_c > sp) {
    state.heat_on = false;
    state.cool_on = true;
  } else {
    state.heat_on = false;
    state.cool_on = false;
  }
}

void controller_apply_on_off_hyst_sym(ControllerState& state,
                                      float temp_c,
                                      float setpoint_c,
                                      float hysteresis_c) {
  const float h = (hysteresis_c > 0.0f) ? hysteresis_c : DEFAULT_HYSTERESIS_C;
  controller_apply_on_off_hyst_asym(state, temp_c, setpoint_c, h, h);
}

// Isteresi con memoria: l'uscita si accende a SP -/+ isteresi e si spegne solo quando
// la temperatura torna al setpoint. Prima si spegneva appena rientrata nella banda e
// si riaccendeva subito dopo: commutazioni continue sulla soglia (usura relè e pompa).
// La memoria è state.heat_on/cool_on, azzerata da controller_stop_outputs().
void controller_apply_on_off_hyst_asym(ControllerState& state,
                                       float temp_c,
                                       float setpoint_c,
                                       float hysteresis_heat_c,
                                       float hysteresis_cool_c) {
  const float sp = clamp_setpoint(setpoint_c);
  const float h_heat = (hysteresis_heat_c > 0.0f) ? hysteresis_heat_c : DEFAULT_HYSTERESIS_C;
  const float h_cool = (hysteresis_cool_c > 0.0f) ? hysteresis_cool_c : DEFAULT_HYSTERESIS_C;

  bool heat = state.heat_on;
  bool cool = state.cool_on;

  if (heat) {
    if (temp_c >= sp) heat = false;
  } else if (temp_c <= (sp - h_heat)) {
    heat = true;
  }

  if (cool) {
    if (temp_c <= sp) cool = false;
  } else if (temp_c >= (sp + h_cool)) {
    cool = true;
  }

  if (heat && cool) {
    heat = false;
    cool = false;
  }
  state.heat_on = heat;
  state.cool_on = cool;
}
