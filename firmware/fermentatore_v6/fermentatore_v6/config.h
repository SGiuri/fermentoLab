#pragma once
#include "secrets.h"

// Pattern required by project policy: 6.MINOR.PATCH
#define FIRMWARE_VERSION "6.4.0"

#define SCHEMA_VERSION "1.0"

// Device identity (M4). Use unique values for two devices.
#define DEVICE_FERMENTER_ID  "f1"           // AUTO-CONFIG
#define DEVICE_HARDWARE_TARGET "esp32-wroom" // AUTO-CONFIG

// ============================================================
// WiFi — rete principale
// ============================================================
#define WIFI_SSID              "IoT"
#define WIFI_TIMEOUT_MS        20000UL
// HOTSPOT_PASS → secrets.h
#define HOTSPOT_SSID           "HotIoT"
// OTA_PASSWORD → secrets.h
#define OTA_HOSTNAME           "fermentatore-v6-" DEVICE_FERMENTER_ID

// ============================================================
// MQTT Broker (HiveMQ Cloud con TLS su porta 8883)
// MQTT_SERVER, MQTT_USER, MQTT_PASS → secrets.h
// ============================================================
#define MQTT_PORT               8883
#define MQTT_CLIENT_ID          "esp32_v6_" DEVICE_FERMENTER_ID
#define MQTT_CONNECT_TIMEOUT_MS 15000UL   // intervallo tra tentativi di connessione
#define MQTT_BUFFER_SIZE        4096
#define MQTT_KEEPALIVE_S        30        // il broker pubblica il LWT dopo ~1.5x keepalive
#define MQTT_SOCKET_TIMEOUT_S   5         // attesa CONNACK/letture (default libreria: 15 s)
#define MQTT_TLS_HANDSHAKE_S    10        // default core ESP32: 120 s, bloccava il loop
#define MQTT_TCP_CONNECT_MS     5000UL

// ============================================================
// NTP
// ============================================================
#define NTP_SERVER   "pool.ntp.org"
#define NTP_OFFSET   3600    // UTC+1 ora solare
#define NTP_DST      3600    // +1h ora legale (usa 0 in inverno)
#define EPOCH_MINIMO_VALIDO 1700000000UL  // epoch inferiori = orologio non sincronizzato

// ============================================================
// NVS namespace
// ============================================================
#define NVS_NS_FERM  "ferm_v6"
#define NVS_SAVE_INTERVAL_MS 600000UL   // salvataggio periodico ore/epoch (10 min: meno usura flash)

// ============================================================
// Timing
// ============================================================
#define STATUS_INTERVAL_MS            30000UL   // heartbeat MQTT ogni 30 s
#define STATUS_MIN_INTERVAL_MS         5000UL   // pubblicazione anticipata su cambio relè/FSM
#define STATUS_ATTESA_PRIMA_LETTURA_MS 60000UL  // al boot non pubblica stato prima della prima lettura valida
#define WIFI_RECONNECT_INTERVAL_MS    30000UL   // intervallo tentativi dopo il burst iniziale
#define HOTSPOT_RECONNECT_INTERVAL_MS  5000UL   // intervallo tentativi del burst iniziale
#define WIFI_TENTATIVO_TIMEOUT_MS     15000UL   // durata massima attesa di un tentativo in corso
#define HOTSPOT_TENTATIVI_MAX          6        // poi ripiega sulla rete principale

// Watchdog hardware sul loop: allo scadere panic + reboot (relè OFF al boot).
#define WDT_TIMEOUT_S 60

// MQTT V6 topic roots
#define TOPIC_PREFIX_HA   "fermentazione/v6/" DEVICE_FERMENTER_ID "/ha/cmd"
#define TOPIC_PREFIX_ESP  "fermentazione/v6/" DEVICE_FERMENTER_ID "/esp"

// ============================================================
// Topic MQTT — HA → ESP32 comandi
// ============================================================
#define TOPIC_CMD_START          TOPIC_PREFIX_HA "/start"
#define TOPIC_CMD_STOP           TOPIC_PREFIX_HA "/stop"
#define TOPIC_CMD_REBOOT         TOPIC_PREFIX_HA "/reboot"
#define TOPIC_CMD_PROFILE_SET    TOPIC_PREFIX_HA "/profile_set"
#define TOPIC_CMD_CONTROL_SET    TOPIC_PREFIX_HA "/control_set"
#define TOPIC_CMD_AUTOTUNE_START TOPIC_PREFIX_HA "/autotune_start"
#define TOPIC_CMD_AUTOTUNE_STOP  TOPIC_PREFIX_HA "/autotune_stop"
#define TOPIC_CMD_AUTOTUNE_APPLY TOPIC_PREFIX_HA "/autotune_apply"

// Protezione comandi: retained riconsegnati, duplicati, comandi vecchi
#define CMD_FINESTRA_RETAINED_MS  10000UL  // ricevuti entro questo tempo dalla SUBSCRIBE = probabili retained
#define CMD_MAX_ETA_S             600      // ts più vecchio di così viene rifiutato (se orologio valido)
#define CMD_DEDUP_SLOTS           8        // ultimi request_id+ts ricordati in RAM

// ============================================================
// Topic MQTT — ESP32 → HA
// ============================================================
#define TOPIC_STATE           TOPIC_PREFIX_ESP "/state"
#define TOPIC_ACK             TOPIC_PREFIX_ESP "/ack"
#define TOPIC_ALARM           TOPIC_PREFIX_ESP "/alarm"
#define TOPIC_AVAILABILITY    TOPIC_PREFIX_ESP "/availability"   // retained: online | offline (LWT)
#define TOPIC_CONTROLLO       TOPIC_PREFIX_ESP "/controllo"      // retained: grandezze interne del controllore (didattica)
#define TOPIC_AUTOTUNE_STATE  TOPIC_PREFIX_ESP "/autotune_state"
#define TOPIC_AUTOTUNE_RESULT TOPIC_PREFIX_ESP "/autotune_result"
#define TOPIC_LOG             TOPIC_PREFIX_ESP "/log"

#define PAYLOAD_ONLINE  "online"
#define PAYLOAD_OFFLINE "offline"

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Pin map (AUTO-CONFIG): defaults for WROOM profile.
#define PIN_DS18B20       33 // AUTO-CONFIG
#define PIN_SENSOR_PWR    25 // AUTO-CONFIG
#define PIN_RISCALDATORE  16 // AUTO-CONFIG
#define PIN_REFRIGERATORE 17 // AUTO-CONFIG
#define PIN_OLED_SDA      21 // AUTO-CONFIG
#define PIN_OLED_SCL      22 // AUTO-CONFIG

#define DEFAULT_HYSTERESIS_C 0.50f
#define ISTERESI_MIN_C       0.10f   // limiti per l'isteresi impostata da HA (control_set)
#define ISTERESI_MAX_C       3.00f
#define PID_D_TAU_S         300.0f   // filtro sulla derivata [s]: il DS18B20 misura a gradini di 0,0625 °C
#define PID_I_MAX_PCT       50.0f    // limite del termine integrale [%]
#define CONTROL_INTERVAL_MS  1000UL

// ============================================================
// Stadio uscite relè (uscite_rele.cpp)
// Valgono per tutti i modi; gli spegnimenti di sicurezza non attendono i tempi minimi.
// Con questi valori il duty PID effettivo è 0%, 5..90% oppure 100% e ogni relè
// fa al massimo 6 cicli/ora nei modi PID (simulazione 48 h in tests/host).
// ============================================================
#define PID_DUTY_WINDOW_MS         600000UL  // finestra time-proportional P/PI/PD/PID (era 10 s)
#define RELE_IMPULSO_MIN_MS         30000UL  // impulsi più brevi vengono saltati
#define RELE_MIN_ON_MS              30000UL  // tempo minimo acceso prima di spegnere
#define RELE_MIN_OFF_MS             60000UL  // tempo minimo spento prima di riaccendere
#define RELE_CAMBIO_DIREZIONE_MS    60000UL  // attesa tra spegnimento caldo e accensione freddo (e viceversa)

// ============================================================
// Limiti di sicurezza temperatura — indipendenti da modo e profilo
// ============================================================
#define DEFAULT_MIN_SETPOINT_C 0.0f     // setpoint sempre limitato a questo intervallo
#define DEFAULT_MAX_SETPOINT_C 35.0f
#define SICUREZZA_TEMP_MAX_C   36.0f    // sopra: riscaldatore vietato
#define SICUREZZA_TEMP_MIN_C   -1.0f    // sotto: refrigeratore vietato
#define SICUREZZA_BANDA_C       1.0f    // riscaldatore vietato se T > SP+banda, refrigeratore se T < SP-banda

// ============================================================
// Display OLED SSD1306 128×64 (I2C)
// ============================================================
#define OLED_I2C_ADDR    0x3C
#define DISPLAY_UPDATE_MS 500UL

// ============================================================
// Sensore DS18B20
// ============================================================
#define SENSOR_READ_INTERVAL_MS 5000UL
#define SENSOR_CONVERSION_MS     750UL      // conversione a 12 bit
#define SENSOR_MAX_ETA_MS      20000UL      // ultima lettura valida più vecchia: relè forzati OFF
#define SENSOR_ALARM_TIMEOUT_MINUTES 5UL
#define SENSOR_VALID_STREAK_MIN 3           // letture valide consecutive per riprendere il controllo
#define SENSOR_SPIKE_DELTA_C           7.0f  // soglia ampiezza spike singolo (°C)
#define SENSOR_OSC_CONSECUTIVE_LIMIT   3     // oscillazioni consecutive per errore
#define SENSOR_POWER_CYCLE_TIMEOUT_MS 120000UL
#define SENSOR_POWER_CYCLE_MAX_ATTEMPTS 3
#define SENSOR_REBOOT_DELAY_MS 30000UL
#define SENSOR_REBOOT_MAX      3            // riavvii consecutivi per sensore, poi resta in allarme

// PID-family defaults
#define DEFAULT_KP_HEAT 25.0f
#define DEFAULT_KI_HEAT 0.03f
#define DEFAULT_KD_HEAT 0.0f
#define DEFAULT_KP_COOL 25.0f
#define DEFAULT_KI_COOL 0.03f
#define DEFAULT_KD_COOL 0.0f
#define DEFAULT_OUTPUT_DEADBAND_PCT 2.5f
#define DEFAULT_MIN_EFFECTIVE_OUTPUT_PCT 5.0f

// Autotuning: l'implementazione attuale non misura il processo (non pilota i relè
// e ricava i guadagni da una formula fissa). Disabilitato finché non esiste un vero
// relay-feedback test. Impostare a 1 solo per sviluppo.
#define AUTOTUNE_ABILITATO 0
#define AUTOTUNE_MAX_DURATION_S 1800UL
#define AUTOTUNE_MIN_RUNTIME_S  60UL
#define AUTOTUNE_REQUIRED_QUALITY_SCORE 0.75f
#define AUTOTUNE_TEMP_MIN_C   -5.0f   // abort se temp scende sotto questa soglia
#define AUTOTUNE_TEMP_MAX_C   45.0f   // abort se temp supera questa soglia

// Profilo fermentazione
#define MAX_STEPS              20
#define DEFAULT_STANDBY_TEMP_C 20.0f
#define PROFILO_STEP_MAX_ORE   8760.0f  // durata massima di uno step (1 anno)

// Logging dual-channel Serial + MQTT
// Livelli: LOG_NONE=0  LOG_ERROR=1  LOG_WARN=2  LOG_INFO=3  LOG_DEBUG=4
// Quando entrambi sono LOG_NONE il compilatore elimina tutte le macro.
#define LOG_NONE   0
#define LOG_ERROR  1
#define LOG_WARN   2
#define LOG_INFO   3
#define LOG_DEBUG  4
#define LOG_SERIAL_LEVEL  LOG_DEBUG  // ← LOG_NONE per silenziare Serial
#define LOG_MQTT_LEVEL    LOG_INFO   // ← LOG_NONE per eliminare traffico log MQTT
