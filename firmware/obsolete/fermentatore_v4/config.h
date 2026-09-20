#pragma once
#include "secrets.h"

// ============================================================
// FIRMWARE VERSION
// Aggiornare ad ogni nuova voce nel CHANGELOG.md
// Pattern: 4.MINOR.PATCH (major = numero versione progetto)
// ============================================================
#define FIRMWARE_VERSION "4.4.0"

// ============================================================
// HARDWARE WIRING - FERMENTATORE V4 (ESP32)
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
//    - GPIO25 : DS18B20 VDD switch (PIN_SENSOR_PWR) — power-cycling via firmware
//    - GPIO16 : Relay heat command (riscaldatore)
//    - GPIO17 : Relay cool command (refrigeratore)
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
// WiFi — rete principale (scuola / casa)
// ============================================================
#define WIFI_SSID           "IoT"
// Le password sono gestite per MAC address in wifi_connection.ino

#define WIFI_TIMEOUT_MS     20000UL

// ============================================================
// Hotspot OTA — rete personale per aggiornamenti via Arduino OTA
// ============================================================
// All'avvio l'ESP32 scansiona le reti disponibili.
// Se trova HOTSPOT_SSID si connette a questa rete (con password fissa)
// e avvia il servizio ArduinoOTA; il PC deve essere sulla stessa rete.
// Se HOTSPOT_SSID non è trovato, si connette normalmente a WIFI_SSID.
#define HOTSPOT_SSID        "HotIoT"      // <-- cambia con il nome del tuo hotspot
// HOTSPOT_PASS → secrets.h

// ============================================================
// OTA (ArduinoOTA, attivo solo quando connesso via hotspot)
// ============================================================
#define OTA_HOSTNAME        "fermentatore-v4"
// OTA_PASSWORD → secrets.h

// ============================================================
// MQTT Broker (HiveMQ Cloud con TLS su porta 8883)
// ============================================================
// MQTT_SERVER, MQTT_USER, MQTT_PASS → secrets.h
#define MQTT_PORT                8883
#define MQTT_CLIENT_ID           "esp32_f1_v4"
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
// Namespace NVS condiviso con profilo.cpp ("ferm_v4")
#define NVS_NS_FERM             "ferm_v4"
// Chiave NVS per il flag di test offline (si auto-cancella dopo un boot)
#define NVS_KEY_TEST_NO_WIFI    "test_no_wifi"

// ============================================================
// Pin Hardware
// ============================================================
#define PIN_DS18B20       33    // OneWire DS18B20 (dati)
#define PIN_SENSOR_PWR    25    // VDD DS18B20 — power-cycling via GPIO (GPIO25 è HiZ al boot)
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
#define DUTY_CYCLE_ESTIMATE_MIN_MS 600000UL   // Minimo storico per usare una stima (10 min) → ciclo 7min on / 3min off a 70%
// Blackout WiFi: in assenza di rete il termostato continua con controllo on/off standard (senza MQTT).
// Ogni WIFI_RECONNECT_CYCLE_MS viene tentata una serie di 3 riconnessioni,
// ognuna a distanza di WIFI_RECONNECT_INTERVAL_MS dall'altra.
#define WIFI_RECONNECT_INTERVAL_MS      30000UL  // Intervallo tra tentativi nella serie, rete standard (30 s)
#define WIFI_RECONNECT_CYCLE_MS       1200000UL  // Attesa tra una serie e la successiva, rete standard (20 min)
#define HOTSPOT_RECONNECT_INTERVAL_MS    5000UL  // Intervallo tra i 3 tentativi rapidi su hotspot (5 s)

// Filtro anti-oscillazione sensore:
// rileva pattern incoerenti tipo +delta/-delta ad alta frequenza
// senza bloccare i transitori monotoni plausibili (es. 30C -> 0C inserimento a caldo).
#define SENSOR_SPIKE_DELTA_C            7.0f   // salto minimo (grC) per considerare uno spike
#define SENSOR_OSC_CONSECUTIVE_LIMIT       3   // numero spike incoerenti consecutivi prima di avviare timeout allarme
// Debounce uscita dal fallback: richiede N letture valide consecutive prima di disattivarlo.
// Evita il rimbalzo fallback on/off quando il sensore restituisce valori numericamente
// plausibili ma fisicamente errati (es. DS18B20 che legge 8°C invece di 21°C) che
// superano temp_valida() ma non ancora oscillazione_incoerente().
// Con SENSOR_READ_INTERVAL_MS=5000 → 5 lettue = 25s di stabilità prima di uscire dal fallback.
#define SENSOR_VALID_STREAK_MIN            5   // letture valide consecutive per uscire dal fallback
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
// Valori di default
// ============================================================
#define DEFAULT_STANDBY_TEMP    18.0f   // temperatura standby senza profilo attivo
#define DEFAULT_ISTERESI         0.5f   // isteresi relè (°C) — default se non fornita dal profilo
#define DEFAULT_CUTOFF_ANTICIP_FRIGO 0.3f  // anticipo spegnimento frigo (°C): OFF a SP+anticip invece di SP
                                           // compensa l'inerzia termica dell'evaporatore e riduce l'undershoot
#define DEFAULT_MIN_CYCLE_SEC       5   // min tempo ON e OFF pompa/relè (secondi)
#define DEFAULT_CONTROL_FILTER_ALPHA 0.20f  // IIR per la sola temperatura usata dal controllo
#define DEFAULT_SWITCHOVER_LOCKOUT_SEC 600  // blocco caldo<->freddo dopo lo spegnimento dell'altro attuatore

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
