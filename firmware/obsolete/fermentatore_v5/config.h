#pragma once
#include "secrets.h"

// ============================================================
// FIRMWARE VERSION
// Aggiornare ad ogni nuova voce nel CHANGELOG.md
// Pattern: 5.MINOR.PATCH (major = numero versione progetto)
// ============================================================
#define FIRMWARE_VERSION "5.3.0"

// ============================================================
// HARDWARE WIRING - FERMENTATORE V5 (ESP32)
// ============================================================
// Reference board: ESP32 DevKit v1 (WROOM32 class).
//
// Hardware identico a V4. Cambia solo il controllo termostato:
// PID bidirezionale su PWM invece di bang-bang con isteresi.
//
// Attuatori:
//   - Riscaldatore : cinghia riscaldante 30W (lenta, debole)
//   - Raffreddatore: pompa da presepi con glicole -3/+5°C
//                    dal chiller autonomo 1/5 HP (veloce, potente)
//   => gain PID asimmetrici: kp_frigo < kp_risc
//
// 1) Power distribution
//    - ESP32 powered by USB 5V or VIN 5V.
//    - Sensors/display must use 3.3V logic.
//    - Relay module power can be 5V, but GPIO control remains 3.3V from ESP32.
//    - Common GND is mandatory between ESP32, relays, sensor and display.
//
// 2) Pin map (firmware <-> hardware)
//    - GPIO33 : DS18B20 data (OneWire bus)
//    - GPIO25 : DS18B20 VDD switch (PIN_SENSOR_PWR) — power-cycling via firmware
//    - GPIO16 : Relay heat command (riscaldatore)
//    - GPIO17 : Relay cool command (refrigeratore/pompa glicole)
//    - GPIO21 : I2C SDA -> OLED SDA
//    - GPIO22 : I2C SCL -> OLED SCL
//
// 3) DS18B20 wiring
//    - DS18B20 VDD -> GPIO25 (PIN_SENSOR_PWR)  [NON 3V3 diretto]
//    - DS18B20 GND -> GND
//    - DS18B20 DQ  -> GPIO33
//    - Pull-up 4.7k: tra DQ e 3V3 (fisso su 3V3, indipendente da PIN_SENSOR_PWR)
//
// 4) Relay module wiring (2 channels)
//    - IN1 -> GPIO16 (heat)
//    - IN2 -> GPIO17 (cool/pump)
//    - VCC -> module supply (usually 5V)
//    - GND -> ESP32 GND (shared)
//    - Logic is ACTIVE LOW in this project:
//      RELAY_ON = LOW, RELAY_OFF = HIGH
//
// 5) OLED SSD1306 128x64 I2C wiring
//    - OLED VCC -> 3V3 (or 5V only if module explicitly supports it)
//    - OLED GND -> GND
//    - OLED SDA -> GPIO21
//    - OLED SCL -> GPIO22
//    - I2C address expected: 0x3C (or 0x3D on some boards)
// ============================================================

// ============================================================
// WiFi — rete principale
// ============================================================
#define WIFI_SSID           "IoT"
#define WIFI_TIMEOUT_MS     20000UL

// ============================================================
// Hotspot OTA
// ============================================================
#define HOTSPOT_SSID        "HotIoT"
// HOTSPOT_PASS → secrets.h

// ============================================================
// OTA
// ============================================================
#define OTA_HOSTNAME        "fermentatore-v5"
// OTA_PASSWORD → secrets.h

// ============================================================
// MQTT Broker (HiveMQ Cloud con TLS)
// ============================================================
// MQTT_SERVER, MQTT_USER, MQTT_PASS → secrets.h
#define MQTT_PORT                8883
#define MQTT_CLIENT_ID           "esp32_f1_v5"
#define MQTT_CONNECT_TIMEOUT_MS  15000UL

// ============================================================
// Topic MQTT — ESP32 → HA
// Prefisso: fermentazione/v5/f1/esp/  (wildcard: fermentazione/v5/f1/esp/#)
// Distinto da V4 ("fermentazione/f1/") per coesistenza sul medesimo broker.
// ============================================================
#define TOPIC_RICHIEDI_PROFILO  "fermentazione/v5/f1/esp/richiedi_profilo"
#define TOPIC_ACK               "fermentazione/v5/f1/esp/ack"
#define TOPIC_STATO             "fermentazione/v5/f1/esp/stato"
#define TOPIC_ALLARME           "fermentazione/v5/f1/esp/allarme"
#define TOPIC_PONG              "fermentazione/v5/f1/esp/pong"

// ============================================================
// Topic MQTT — HA → ESP32
// Prefisso: fermentazione/v5/f1/ha/  (wildcard: fermentazione/v5/f1/ha/#)
// ============================================================
#define TOPIC_CMD_PROFILO       "fermentazione/v5/f1/ha/profilo"
#define TOPIC_CMD_STOP          "fermentazione/v5/f1/ha/stop"
#define TOPIC_CMD_TEMP_STANDBY  "fermentazione/v5/f1/ha/temp_standby"
#define TOPIC_CMD_PING          "fermentazione/v5/f1/ha/ping"
#define TOPIC_CMD_REBOOT        "fermentazione/v5/f1/ha/reboot"
#define TOPIC_CMD_TEST_NO_WIFI  "fermentazione/v5/f1/ha/test_no_wifi"

// Namespace NVS (cambiato da v4 per evitare collisioni struttura dati)
#define NVS_NS_FERM             "ferm_v5"
#define NVS_KEY_TEST_NO_WIFI    "test_no_wifi"

// ============================================================
// Pin Hardware
// ============================================================
#define PIN_DS18B20       33    // OneWire DS18B20 (dati)
#define PIN_SENSOR_PWR    25    // VDD DS18B20 — power-cycling via GPIO (GPIO25 è HiZ al boot)
#define PIN_RISCALDATORE  16    // Relè riscaldatore (cinghia 30W)
#define PIN_REFRIGERATORE 17    // Relè pompa glicole

// Logica relè: Active LOW
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ============================================================
// Timing
// ============================================================
#define STATUS_INTERVAL_MS          60000UL   // Heartbeat MQTT ogni 60s
#define SENSOR_READ_INTERVAL_MS      5000UL   // Lettura DS18B20 ogni 5s
#define PROFILO_REQUEST_TIMEOUT_MS  15000UL   // Attesa risposta da HA
#define ALLARME_REBOOT_DELAY_MS     30000UL   // Attesa prima del riavvio su allarme sensore
#define WIFI_RECONNECT_INTERVAL_MS      30000UL  // Intervallo tra tentativi nella serie, rete standard (30 s)
#define WIFI_RECONNECT_CYCLE_MS       1200000UL  // Attesa tra una serie e la successiva, rete standard (20 min)
#define HOTSPOT_RECONNECT_INTERVAL_MS    5000UL  // Intervallo tra i 3 tentativi rapidi su hotspot (5 s)

// Filtro anti-oscillazione sensore
#define SENSOR_SPIKE_DELTA_C            7.0f
#define SENSOR_OSC_CONSECUTIVE_LIMIT       3
// Debounce uscita dal fallback: richiede N letture valide consecutive prima di fidarsi del sensore.
// Con SENSOR_READ_INTERVAL_MS=5000 → 5 letture = 25s di stabilità.
#define SENSOR_VALID_STREAK_MIN            5
// Power cycle del sensore: se rimane instabile per TIMEOUT_MS, taglia e ripristina
// l'alimentazione tramite PIN_SENSOR_PWR (funzione bloccante ~1.45s, max N tentativi).
#define SENSOR_POWER_CYCLE_TIMEOUT_MS  120000UL  // instabilità > 2 min → tenta power cycle
#define SENSOR_POWER_CYCLE_MAX_ATTEMPTS      3   // tentativi prima di lasciare escalare ad ALLARME_SENSORE

// ============================================================
// NTP
// ============================================================
#define NTP_SERVER   "pool.ntp.org"
#define NTP_OFFSET   3600     // UTC+1 ora solare
#define NTP_DST      3600     // +1h ora legale (usa 0 in inverno)

// ============================================================
// Profilo — limiti
// ============================================================
#define MAX_STEPS  20

// ============================================================
// PID defaults — riscaldatore (cinghia 30W, lenta)
// ============================================================
#define DEFAULT_PID_KP_RISC   5.0f    // 1°C errore → 5% duty riscaldatore
#define DEFAULT_PID_KI_RISC   0.005f  // [1/s] — integratore lento
#define DEFAULT_PID_KD_RISC   0.0f    // derivativo off: rumore DS18B20

// ============================================================
// PID defaults — refrigeratore (pompa glicole, veloce/potente)
// Gain molto più bassi per evitare overcooling
// ============================================================
#define DEFAULT_PID_KP_FRIGO  1.0f    // gain conservativo: glicole è molto più potente
#define DEFAULT_PID_KI_FRIGO  0.001f  // [1/s]
#define DEFAULT_PID_KD_FRIGO  0.0f

// ============================================================
// PWM defaults
// ============================================================
#define DEFAULT_PWM_PERIOD_S       60   // ciclo PWM 1 minuto
#define DEFAULT_MIN_POMPA_ON_S      5   // impulso minimo pompa [secondi] — sotto questa soglia si salta
#define PID_INTEGRAL_MAX        200.0f  // clamp anti-windup integrale

// ============================================================
// Valori di default standby/allarme
// ============================================================
#define DEFAULT_STANDBY_TEMP    18.0f   // temperatura standby senza profilo attivo

// ============================================================
// Display OLED SSD1306 128x64 — I2C
// ============================================================
#define PIN_OLED_SDA      21
#define PIN_OLED_SCL      22
#define OLED_I2C_ADDR     0x3C
#define DISPLAY_UPDATE_MS 1000UL

// ============================================================
// Logging
// ============================================================
#define LOG_NONE   0
#define LOG_ERROR  1
#define LOG_WARN   2
#define LOG_INFO   3
#define LOG_DEBUG  4

#define LOG_SERIAL_LEVEL  LOG_DEBUG
#define LOG_MQTT_LEVEL    LOG_INFO

#define TOPIC_LOG  "fermentazione/v5/f1/esp/log"
