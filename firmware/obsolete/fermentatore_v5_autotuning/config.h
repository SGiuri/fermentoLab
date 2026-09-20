#pragma once
#include "secrets.h"
// ============================================================
// Config autotuning — pin, MQTT, timing.
// Hardware identico a fermentatore_v5.
// ============================================================

// WiFi
#define WIFI_SSID           "IoT"
#define WIFI_TIMEOUT_MS     20000UL

#define HOTSPOT_SSID        "HotIoT"
// HOTSPOT_PASS → secrets.h

// OTA
#define OTA_HOSTNAME  "fermentatore-v5-autotune"
// OTA_PASSWORD → secrets.h

// MQTT
// MQTT_SERVER, MQTT_USER, MQTT_PASS → secrets.h
#define MQTT_PORT                8883
#define MQTT_CLIENT_ID           "esp32_f1_autotune"
#define MQTT_CONNECT_TIMEOUT_MS  15000UL

// Topic output:
//   risultati separati per fase (retain=true) → HA legge entrambi come sensori indipendenti
#define TOPIC_AUTOTUNE_RISULTATI_RISC   "autotuning/f1/risultati/riscaldamento"
#define TOPIC_AUTOTUNE_RISULTATI_FRIGO  "autotuning/f1/risultati/raffreddamento"
#define TOPIC_AUTOTUNE_STATUS           "autotuning/f1/status"
#define TOPIC_LOG                       "autotuning/f1/log"

// Pin hardware (identici a v5)
#define PIN_DS18B20       33
#define PIN_RISCALDATORE  16
#define PIN_REFRIGERATORE 17
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// I2C OLED
#define PIN_OLED_SDA  21
#define PIN_OLED_SCL  22

// NTP
#define NTP_SERVER "pool.ntp.org"
#define NTP_OFFSET 3600
#define NTP_DST    3600

// Sensore
#define SENSOR_READ_INTERVAL_MS     5000UL
#define SENSOR_SPIKE_DELTA_C        7.0f
#define SENSOR_OSC_CONSECUTIVE_LIMIT   3

// ── Parametri test riscaldamento ────────────────────────────
#define AUTOTUNE_RISC_SETPOINT       25.0f    // °C
#define AUTOTUNE_RISC_RELAY_AMP      80.0f    // % potenza relay riscaldatore
#define AUTOTUNE_RISC_TIMEOUT_H      24.0f    // ore massime
#define AUTOTUNE_RISC_ABORT_TEMP     32.0f    // °C: aborta se supera questa temp

// ── Parametri test raffreddamento ───────────────────────────
#define AUTOTUNE_FRIGO_SETPOINT      14.0f    // °C
#define AUTOTUNE_FRIGO_RELAY_AMP     80.0f    // % potenza relay refrigeratore
#define AUTOTUNE_FRIGO_TIMEOUT_H      2.0f    // ore massime
#define AUTOTUNE_FRIGO_ABORT_TEMP     8.0f    // °C: aborta se scende sotto

// ── Pausa tra i due test ─────────────────────────────────────
#define AUTOTUNE_PAUSA_S              300     // 5 minuti: stabilizzazione

// Logging
#define LOG_NONE  0
#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_DEBUG 4

#define LOG_SERIAL_LEVEL  LOG_DEBUG
#define LOG_MQTT_LEVEL    LOG_INFO
