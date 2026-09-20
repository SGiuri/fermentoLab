#pragma once
#include "secrets.h"

// ============================================================
// HARDWARE WIRING - FERMENTATORE V3 (ESP32)
// ============================================================
// Reference board: ESP32 DevKit v1 (WROOM32 class).
//
// 1) Power distribution
//    - ESP32 powered by USB 5V or VIN 5V.
//    - Sensors/display must use 3.3V logic.
//    - Relay module power can be 5V, but GPIO control remains 3.3V from ESP32.
//    - Common GND is mandatory between ESP32, relays, sensor and display.
//
// 2) Pin map (firmware <-> hardware)
//    - GPIO33 : DS18B20 data (OneWire bus)
//    - GPIO16 : Relay heat command (riscaldatore)
//    - GPIO17 : Relay cool command (refrigeratore)
//    - GPIO21 : I2C SDA -> OLED SDA
//    - GPIO22 : I2C SCL -> OLED SCL
//
// 3) DS18B20 wiring
//    - DS18B20 VDD -> 3V3
//    - DS18B20 GND -> GND
//    - DS18B20 DQ  -> GPIO33
//    - Pull-up resistor 4.7k between DQ and 3V3
//
// 4) Relay module wiring (2 channels)
//    - IN1 -> GPIO16 (heat)
//    - IN2 -> GPIO17 (cool)
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
//
// 6) Important boot/upload note
//    - DS18B20 is configured on GPIO33 (safe non-strapping pin), recommended
//      to avoid wrong boot mode upload issues seen with strapping pins.
// ============================================================

// ============================================================
// WiFi
// ============================================================
#define WIFI_SSID           "IoT"
// Le password sono gestite per MAC address in wifi_connection.ino

#define WIFI_TIMEOUT_MS     20000UL

// ============================================================
// MQTT Broker (HiveMQ Cloud con TLS su porta 8883)
// ============================================================
// MQTT_SERVER, MQTT_USER, MQTT_PASS → secrets.h
#define MQTT_PORT                8883
#define MQTT_CLIENT_ID           "esp32_f1_v3"
#define MQTT_CONNECT_TIMEOUT_MS  15000UL

// ============================================================
// Topic MQTT — ESP32 → HA
// ============================================================
#define TOPIC_RICHIEDI_PROFILO  "fermentazione/f1/richiedi_profilo"
#define TOPIC_ACK               "fermentazione/f1/ack"
#define TOPIC_STATO             "fermentazione/f1/stato"
#define TOPIC_ALLARME           "fermentazione/f1/allarme"
#define TOPIC_PONG              "fermentazione/f1/pong"

// ============================================================
// Topic MQTT — HA → ESP32
// ============================================================
// HA risponde a richiedi_profilo su questo topic con:
//   - payload profilo completo (se fermentazione attiva)
//   - {"stato":"nessun_profilo","temp_standby":18.0} (se nessuna attiva)
#define TOPIC_CMD_PROFILO       "fermentazione/f1/cmd/profilo"
// Ferma il processo e porta in STANDBY
#define TOPIC_CMD_STOP          "fermentazione/f1/cmd/stop"
// Aggiorna la temperatura di standby al volo: {"temp": 18.0}
#define TOPIC_CMD_TEMP_STANDBY  "fermentazione/f1/cmd/temp_standby"
// Ping di diagnostica: ESP32 risponde su TOPIC_PONG
#define TOPIC_CMD_PING          "fermentazione/f1/cmd/ping"
// Riavvio remoto esplicito (comando manuale da HA)
#define TOPIC_CMD_REBOOT        "fermentazione/f1/cmd/reboot"
// Test offline: ESP32 riavvia usando password WiFi sbagliata → fallback duty cycle
#define TOPIC_CMD_TEST_NO_WIFI  "fermentazione/f1/cmd/test_no_wifi"
// Namespace NVS condiviso con profilo.cpp ("ferm_v3")
#define NVS_NS_FERM             "ferm_v3"
// Chiave NVS per il flag di test offline (si auto-cancella dopo un boot)
#define NVS_KEY_TEST_NO_WIFI    "test_no_wifi"

// ============================================================
// Pin Hardware
// ============================================================
#define PIN_DS18B20       33    // OneWire DS18B20
#define PIN_RISCALDATORE  16    // Relè riscaldatore
#define PIN_REFRIGERATORE 17    // Relè refrigeratore

// Logica relè: adatta a moduli relay attivi-basso (Active LOW)
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ============================================================
// Timing
// ============================================================
#define STATUS_INTERVAL_MS          60000UL   // Heartbeat MQTT ogni 60s
#define SENSOR_READ_INTERVAL_MS      5000UL   // Lettura DS18B20 ogni 5s
#define PROFILO_REQUEST_TIMEOUT_MS  15000UL   // Attesa risposta da HA
#define ALLARME_REBOOT_DELAY_MS     30000UL   // Attesa prima del riavvio su allarme sensore
#define DUTY_CYCLE_WINDOW_MS      3600000UL   // Storico rolling di 1 ora
#define DUTY_CYCLE_BUCKET_MS        60000UL   // Bucket da 1 minuto per stimare il duty cycle
#define DUTY_CYCLE_APPLY_WINDOW_MS 600000UL   // Finestra di applicazione fallback (10 min)
#define DUTY_CYCLE_ESTIMATE_MIN_MS  60000UL   // Minimo storico per usare una stima (1 min)
// Blackout WiFi: se il WiFi rimane assente per questo tempo durante RUNNING/STANDBY/HOLDING
// l'ESP32 si riavvia da solo (tenta di recuperare l'associazione AP/DHCP).
// Il fallback duty cycle copre il periodo senza connessione; il riavvio è il last-resort.
#define WIFI_BLACKOUT_REBOOT_MS  1200000UL   // 20 minuti

// Filtro anti-oscillazione sensore:
// rileva pattern incoerenti tipo +delta/-delta ad alta frequenza
// senza bloccare i transitori monotoni plausibili (es. 30C -> 0C inserimento a caldo).
#define SENSOR_SPIKE_DELTA_C            7.0f   // salto minimo (grC) per considerare uno spike
#define SENSOR_OSC_CONSECUTIVE_LIMIT       3   // numero spike incoerenti consecutivi prima di avviare timeout allarme

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
// Valori di default
// ============================================================
#define DEFAULT_STANDBY_TEMP    18.0f   // temperatura standby senza profilo attivo
#define DEFAULT_ISTERESI         0.3f   // isteresi relè (°C)
#define DEFAULT_MIN_CYCLE_MIN       5   // protezione compressore (minuti)

// ============================================================
// Display OLED SSD1306 128x64 — I2C
// ============================================================
#define PIN_OLED_SDA      21     // I2C SDA (default ESP32)
#define PIN_OLED_SCL      22     // I2C SCL (default ESP32)
#define OLED_I2C_ADDR     0x3C   // indirizzo 7-bit (0x3D se JP1 soldato)
#define DISPLAY_UPDATE_MS 1000UL // refresh display ogni 1s

// ============================================================
// Logging
// Livelli: LOG_NONE=0  LOG_ERROR=1  LOG_WARN=2  LOG_INFO=3  LOG_DEBUG=4
//
// Per disabilitare completamente un canale a sviluppo terminato:
//   #define LOG_SERIAL_LEVEL  LOG_NONE
//   #define LOG_MQTT_LEVEL    LOG_NONE
// Quando entrambi sono LOG_NONE il compilatore elimina tutte le macro.
// ============================================================
#define LOG_NONE   0
#define LOG_ERROR  1
#define LOG_WARN   2
#define LOG_INFO   3
#define LOG_DEBUG  4

#define LOG_SERIAL_LEVEL  LOG_DEBUG  // ← LOG_NONE per silenziare Serial
#define LOG_MQTT_LEVEL    LOG_INFO   // ← LOG_NONE per eliminare traffico log MQTT

#define TOPIC_LOG  "fermentazione/f1/log"
