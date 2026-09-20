#pragma once
#include "controller.h"

void controller_apply_on_off(ControllerState& state, float temp_c, float setpoint_c);
void controller_apply_on_off_hyst_sym(ControllerState& state,
                                      float temp_c,
                                      float setpoint_c,
                                      float hysteresis_c);
void controller_apply_on_off_hyst_asym(ControllerState& state,
                                       float temp_c,
                                       float setpoint_c,
                                       float hysteresis_heat_c,
                                       float hysteresis_cool_c);
