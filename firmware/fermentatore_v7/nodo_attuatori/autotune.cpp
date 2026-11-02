#include "autotune.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void autotune_init(AutotuneContext& ctx) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.state = AutotuneState::IDLE;
  ctx.result.valid = false;
}

bool autotune_start(AutotuneContext& ctx,
                    const ControllerState& controller,
                    float setpoint_c,
                    char* reason_buf,
                    size_t reason_buf_size) {
  if (controller.runtime_state != RuntimeState::STOPPED) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf, reason_buf_size, "autotune_requires_stopped_state");
    }
    return false;
  }

  if (ctx.state == AutotuneState::RUNNING) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf, reason_buf_size, "autotune_already_running");
    }
    return false;
  }

  ctx.state = AutotuneState::RUNNING;
  ctx.start_ms = millis();
  ctx.last_tick_ms = ctx.start_ms;
  ctx.min_temp_seen = 1000.0f;
  ctx.max_temp_seen = -1000.0f;
  ctx.baseline_setpoint = setpoint_c;
  ctx.result.valid = false;

  if (reason_buf && reason_buf_size > 0) {
    snprintf(reason_buf, reason_buf_size, "ok");
  }
  return true;
}

void autotune_tick(AutotuneContext& ctx, float temp_c, uint32_t now_ms) {
  if (ctx.state != AutotuneState::RUNNING) {
    return;
  }

  // Sicurezza: abort immediato se temperatura fuori range operativo
  if (temp_c < AUTOTUNE_TEMP_MIN_C || temp_c > AUTOTUNE_TEMP_MAX_C) {
    autotune_stop(ctx, "temperature_out_of_range");
    return;
  }

  ctx.last_tick_ms = now_ms;
  if (temp_c < ctx.min_temp_seen) ctx.min_temp_seen = temp_c;
  if (temp_c > ctx.max_temp_seen) ctx.max_temp_seen = temp_c;

  const uint32_t elapsed_s = (now_ms - ctx.start_ms) / 1000UL;
  if (elapsed_s < AUTOTUNE_MIN_RUNTIME_S) {
    return;
  }

  if (elapsed_s >= AUTOTUNE_MAX_DURATION_S) {
    autotune_stop(ctx, "timeout");
    return;
  }

  // Simplified estimator for bootstrap phase.
  const float amp = clampf((ctx.max_temp_seen - ctx.min_temp_seen) * 0.5f, 0.1f, 5.0f);
  const float q = clampf(1.0f - (amp / 5.0f), 0.0f, 1.0f);

  ctx.result.valid = true;
  ctx.result.quality_score = q;
  ctx.result.duration_s = elapsed_s;
  snprintf(ctx.result.result_id, sizeof(ctx.result.result_id), "%s-%lu", DEVICE_FERMENTER_ID, (unsigned long)(now_ms / 1000UL));

  // Conservative baseline gains.
  ctx.result.kp_heat = clampf(25.0f + amp * 3.0f, 5.0f, 60.0f);
  ctx.result.ki_heat = 0.02f;
  ctx.result.kd_heat = 0.0f;
  ctx.result.kp_cool = clampf(22.0f + amp * 2.0f, 5.0f, 60.0f);
  ctx.result.ki_cool = 0.02f;
  ctx.result.kd_cool = 0.0f;

  ctx.state = AutotuneState::COMPLETED;
}

void autotune_stop(AutotuneContext& ctx, const char* reason) {
  (void)reason;
  if (ctx.state == AutotuneState::RUNNING) {
    ctx.state = AutotuneState::ABORTED;
  }
}

bool autotune_apply(const AutotuneContext& ctx,
                    ControllerState& controller,
                    ControllerConfig& config,
                    float min_quality_score,
                    char* reason_buf,
                    size_t reason_buf_size) {
  if (controller.runtime_state != RuntimeState::STOPPED) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf, reason_buf_size, "autotune_apply_requires_stopped_state");
    }
    return false;
  }

  if (ctx.state != AutotuneState::COMPLETED || !ctx.result.valid) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf, reason_buf_size, "autotune_result_not_ready");
    }
    return false;
  }

  if (ctx.result.quality_score < min_quality_score) {
    if (reason_buf && reason_buf_size > 0) {
      snprintf(reason_buf, reason_buf_size, "autotune_quality_below_threshold");
    }
    return false;
  }

  config.kp_heat = ctx.result.kp_heat;
  config.ki_heat = ctx.result.ki_heat;
  config.kd_heat = ctx.result.kd_heat;
  config.kp_cool = ctx.result.kp_cool;
  config.ki_cool = ctx.result.ki_cool;
  config.kd_cool = ctx.result.kd_cool;

  controller.integrale = 0.0f;
  controller.errore_prec = 0.0f;
  controller.ts_calc_ms = 0;

  if (reason_buf && reason_buf_size > 0) {
    snprintf(reason_buf, reason_buf_size, "ok");
  }
  return true;
}

const char* autotune_state_to_string(AutotuneState state) {
  switch (state) {
    case AutotuneState::IDLE:
      return "idle";
    case AutotuneState::RUNNING:
      return "running";
    case AutotuneState::COMPLETED:
      return "completed";
    case AutotuneState::ABORTED:
      return "aborted";
    default:
      return "unknown";
  }
}
