#pragma once
#include <Arduino.h>
#include "config.h"   // LOG_SERIAL_LEVEL, LOG_MQTT_LEVEL, livelli LOG_*

// ============================================================
// Modulo di logging dual-channel: Serial + MQTT
//
// Canali configurabili a compile-time in config.h:
//   LOG_SERIAL_LEVEL  — quanto è verboso il Serial Monitor
//   LOG_MQTT_LEVEL    — quanto è verboso il topic fermentazione/f1/log
//
// Quando entrambi i livelli sono LOG_NONE le macro vengono ELIMINATE
// dal preprocessore: zero overhead a runtime, zero flash occupata.
//
// Uso tipico:
//   // in setup(), DOPO Serial.begin():
//   log_init(mqtt_pubblica_log);
//
//   // ovunque nel codice:
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

// Tipo della funzione di publish MQTT passata a log_init().
typedef void (*LogMqttFn)(const char* payload);

// Registra la callback MQTT per il logging.
// Chiamare in setup() subito dopo Serial.begin().
// Può essere chiamata con nullptr per disabilitare solo il canale MQTT.
// Il canale Serial è sempre attivo se LOG_SERIAL_LEVEL > LOG_NONE.
void log_init(LogMqttFn fn);

// Funzione interna — NON chiamare direttamente.
// Usa sempre le macro LOG_E / LOG_W / LOG_I / LOG_D.
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
