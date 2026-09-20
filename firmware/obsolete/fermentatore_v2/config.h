#pragma once
#include "secrets.h"

// ============================================================
// MQTT Broker (Mosquitto addon su Home Assistant)
// ============================================================
// MQTT_SERVER, MQTT_USER, MQTT_PASSWORD → secrets.h
#define MQTT_PORT         1883
#define MQTT_CLIENT_ID    "esp32_fermentatore_f1"

// Topic MQTT
#define TOPIC_AVVIA       "fermentazione/f1/cmd/avvia"
#define TOPIC_STOP        "fermentazione/f1/cmd/stop"
#define TOPIC_PING        "fermentazione/f1/cmd/ping"
#define TOPIC_ACK         "fermentazione/f1/ack"
#define TOPIC_STATO       "fermentazione/f1/stato"
#define TOPIC_ALLARME     "fermentazione/f1/allarme"
#define TOPIC_PONG        "fermentazione/f1/pong"

// ============================================================
// Pin Hardware
// ============================================================
#define PIN_DS18B20        4   // Sensore DS18B20 (OneWire)
#define PIN_RISCALDATORE  16   // Relè riscaldatore
#define PIN_REFRIGERATORE 17   // Relè refrigeratore

// Logica relè: cambia in HIGH/LOW secondo il modulo usato
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ============================================================
// Parametri temporali
// ============================================================
#define STATUS_INTERVAL_MS       60000UL   // Pubblicazione stato ogni 60s
#define SENSOR_READ_INTERVAL_MS   5000UL   // Lettura DS18B20 ogni 5s
#define MQTT_RECONNECT_DELAY_MS   5000UL

// ============================================================
// NTP
// ============================================================
#define NTP_SERVER   "pool.ntp.org"
#define NTP_OFFSET   3600    // UTC+1 (ora solare). Usa 7200 per ora legale.
#define NTP_DST      3600    // Daylight Saving Time offset (3600 = ora legale attiva)

// ============================================================
// Limiti profilo
// ============================================================
#define MAX_STEPS  20
