#pragma once
#include <Arduino.h>
#include "controller.h"

enum class AutotuneState : uint8_t {
  IDLE = 0,
  RUNNING = 1,
  COMPLETED = 2,
  ABORTED = 3
};

struct AutotuneResult {
  bool valid;
  char result_id[48];
  float quality_score;
  uint32_t duration_s;
  float kp_heat;
  float ki_heat;
  float kd_heat;
  float kp_cool;
  float ki_cool;
  float kd_cool;
};

struct AutotuneContext {
  AutotuneState state;
  uint32_t start_ms;
  uint32_t last_tick_ms;
  float min_temp_seen;
  float max_temp_seen;
  float baseline_setpoint;
  AutotuneResult result;
};

void autotune_init(AutotuneContext& ctx);
bool autotune_start(AutotuneContext& ctx,
                    const ControllerState& controller,
                    float setpoint_c,
                    char* reason_buf,
                    size_t reason_buf_size);
void autotune_tick(AutotuneContext& ctx, float temp_c, uint32_t now_ms);
void autotune_stop(AutotuneContext& ctx, const char* reason);
bool autotune_apply(const AutotuneContext& ctx,
                    ControllerState& controller,
                    ControllerConfig& config,
                    float min_quality_score,
                    char* reason_buf,
                    size_t reason_buf_size);
const char* autotune_state_to_string(AutotuneState state);
