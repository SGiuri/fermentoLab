#pragma once
#include <Arduino.h>
#include "config.h"   // LOG_SERIAL_LEVEL, LOG_MQTT_LEVEL, livelli LOG_*

// ============================================================
// Modulo di logging dual-channel: Serial + MQTT
//
// Canali configurabili a compile-time in config.h:
//   LOG_SERIAL_LEVEL  — verbosità Serial Monitor
//   LOG_MQTT_LEVEL    — verbosità topic TOPIC_LOG
//
// Quando entrambi i livelli sono LOG_NONE le macro vengono ELIMINATE
// dal preprocessore: zero overhead a runtime, zero flash occupata.
//
// Uso:
//   // in setup(), DOPO Serial.begin():
//   log_init(mqtt_pubblica_log);
//
//   LOG_I("FSM",    "Stato -> %s", stato_str(nuovo));
//   LOG_E("SENSORE","DS18B20 offline da %d min.", minuti);
//   LOG_D("NTP",    "epoch = %lu", (unsigned long)t);
//
// Formato Serial:
//   [  1234567ms] [INFO ] [FSM     ] Stato -> RUNNING
//
// Formato MQTT (topic TOPIC_LOG, QoS 0, no retain):
//   {"t":1234567,"liv":"INFO","mod":"FSM","msg":"Stato -> RUNNING"}
// ============================================================

typedef void (*LogMqttFn)(const char* payload);

void log_init(LogMqttFn fn);

// Funzione interna — usare le macro LOG_E/W/I/D.
void _log_write(uint8_t livello, const char* modulo, const char* fmt, ...);

// ============================================================
// Macro pubbliche — eliminazione compile-time per livello
// ============================================================

#if (LOG_SERIAL_LEVEL >= LOG_ERROR || LOG_MQTT_LEVEL >= LOG_ERROR)
  #define LOG_E(mod, fmt, ...) _log_write(LOG_ERROR, (mod), (fmt), ##__VA_ARGS__)
#else
  #define LOG_E(mod, fmt, ...) ((void)0)
#endif

#if (LOG_SERIAL_LEVEL >= LOG_WARN || LOG_MQTT_LEVEL >= LOG_WARN)
  #define LOG_W(mod, fmt, ...) _log_write(LOG_WARN,  (mod), (fmt), ##__VA_ARGS__)
#else
  #define LOG_W(mod, fmt, ...) ((void)0)
#endif

#if (LOG_SERIAL_LEVEL >= LOG_INFO || LOG_MQTT_LEVEL >= LOG_INFO)
  #define LOG_I(mod, fmt, ...) _log_write(LOG_INFO,  (mod), (fmt), ##__VA_ARGS__)
#else
  #define LOG_I(mod, fmt, ...) ((void)0)
#endif

#if (LOG_SERIAL_LEVEL >= LOG_DEBUG || LOG_MQTT_LEVEL >= LOG_DEBUG)
  #define LOG_D(mod, fmt, ...) _log_write(LOG_DEBUG, (mod), (fmt), ##__VA_ARGS__)
#else
  #define LOG_D(mod, fmt, ...) ((void)0)
#endif
