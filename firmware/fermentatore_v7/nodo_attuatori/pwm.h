#pragma once
#include <Arduino.h>

// PWM configuration for relay duty cycle control
// Used when controller is in P/PI/PD/PID mode

#define PWM_FREQUENCY_HZ      1000    // 1 kHz
#define PWM_RESOLUTION_BITS   8       // 0-255 range
#define PWM_CHANNEL_HEAT      0       // LEDC channel for heat relay
#define PWM_CHANNEL_COOL      1       // LEDC channel for cool relay

/**
 * @brief Initialize PWM on heat and cool relay pins
 * @param pin_heat GPIO pin for heat relay (PIN_RISCALDATORE)
 * @param pin_cool GPIO pin for cool relay (PIN_REFRIGERATORE)
 */
void pwm_init(uint8_t pin_heat, uint8_t pin_cool);

/**
 * @brief Set heat relay duty cycle
 * @param duty_pct 0.0 to 100.0 representing percentage output
 *                 0.0 = OFF, 100.0 = fully ON
 */
void pwm_set_heat_duty(float duty_pct);

/**
 * @brief Set cool relay duty cycle
 * @param duty_pct 0.0 to 100.0 representing percentage output
 *                 0.0 = OFF, 100.0 = fully ON
 */
void pwm_set_cool_duty(float duty_pct);

/**
 * @brief Stop all PWM output (both channels to 0)
 */
void pwm_stop_all();
