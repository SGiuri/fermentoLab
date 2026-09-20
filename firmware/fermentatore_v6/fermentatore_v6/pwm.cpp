#include "pwm.h"
#include <math.h>

static uint8_t g_pin_heat = 0;
static uint8_t g_pin_cool = 0;

static inline uint32_t pwm_max_duty() {
  return (1UL << PWM_RESOLUTION_BITS) - 1UL;
}

static inline uint8_t clampf_u8(float v) {
  if (v < 0.0f) return 0;
  if (v > 255.0f) return 255;
  return (uint8_t)(v + 0.5f);
}

void pwm_init(uint8_t pin_heat, uint8_t pin_cool) {
  g_pin_heat = pin_heat;
  g_pin_cool = pin_cool;

  // ESP32 core 3.x API: attach PWM directly by pin
  ledcAttach(g_pin_heat, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttach(g_pin_cool, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);

  // Start with outputs off
  pwm_stop_all();
}

void pwm_set_heat_duty(float duty_pct) {
  // duty_pct: 0.0 to 100.0, converted to configured LEDC resolution
  const float normalized = duty_pct / 100.0f * (float)pwm_max_duty();
  const uint8_t duty_val = clampf_u8(normalized);
  ledcWrite(g_pin_heat, duty_val);
}

void pwm_set_cool_duty(float duty_pct) {
  // duty_pct: 0.0 to 100.0, converted to configured LEDC resolution
  const float normalized = duty_pct / 100.0f * (float)pwm_max_duty();
  const uint8_t duty_val = clampf_u8(normalized);
  ledcWrite(g_pin_cool, duty_val);
}

void pwm_stop_all() {
  ledcWrite(g_pin_heat, 0);
  ledcWrite(g_pin_cool, 0);
}
