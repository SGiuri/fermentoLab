#pragma once
// ============================================================
// FERMENTATORE V7 — NODO SENSORI (ESP32-S3 Super Mini)
// Legge le tre sonde (F1, chiller, F2) e le pubblica via MQTT. Non comanda nessun relè:
// gli attuatori sono sul nodo attuatori (ESP32 WROOM con la scheda relè), che regola F1 con la
// temperatura pubblicata qui.
// Compilare con: esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled,USBMode=hwcdc,FlashSize=4M,PartitionScheme=default
// ============================================================

#define FIRMWARE_VERSION        "7.0.0"

// ── Rete ──────────────────────────────────────────────────────
// Rete IoT con una chiave per dispositivo: la password si sceglie dal MAC (WIFI_MAC_PASSWORD_MAP in secrets.h).
#define WIFI_SSID               "IoT"
#define WIFI_HOSTNAME           "fermentatore-v7-sensori"
#define WIFI_RIPROVA_MS         15000UL   // nuovo WiFi.begin() se non connesso da tanto
#define WIFI_LOG_MS             10000UL   // ogni quanto ripetere MAC e stato mentre non è connesso
// Hotspot per l'OTA (come la V6): cercato solo all'avvio; se c'è, il nodo si collega a quello e il PC,
// sullo stesso hotspot, lo vede come porta di rete in Arduino IDE. HOTSPOT_PASS e OTA_PASSWORD in secrets.h.
#define HOTSPOT_SSID            "HotIoT"
#define HOTSPOT_TENTATIVI_MAX   6         // tentativi falliti sull'hotspot, poi la rete IoT
#define OTA_HOSTNAME            "fermentatore-v7-sensori"

// ── Orologio (ts nelle letture: il nodo attuatori scarta quelle vecchie) ──
#define NTP_SERVER              "pool.ntp.org"
#define EPOCH_MINIMO_VALIDO     1700000000UL

// ── MQTT (HiveMQ Cloud, TLS sulla 8883, lo stesso broker della V6) ──
#define MQTT_PORT               8883
#define MQTT_KEEPALIVE_S        30        // il broker pubblica il LWT dopo ~1,5 × keepalive
#define MQTT_BUFFER_SIZE        1024
#define MQTT_RIPROVA_MS         5000UL

#define TOPIC_BASE              "fermentazione/v7/sensori"
#define TOPIC_STATO             TOPIC_BASE "/stato"      // retained: online | offline (LWT)
#define TOPIC_TEMP              TOPIC_BASE "/temp/"      // + id sonda: {"seq","t","ts"} o {"seq","t":null,"err","ts"}
#define TOPIC_PING              TOPIC_BASE "/ping"       // stato del nodo e delle sonde, ogni 30 s
#define TOPIC_EVENTO            TOPIC_BASE "/evento"     // power-cycle, riavvii, comandi
#define TOPIC_CMD               TOPIC_BASE "/cmd/"       // + ping | riavvia | power_cycle
#define TOPIC_PONG              TOPIC_BASE "/pong"       // risposta a cmd/ping con lo stesso token

#define PING_INTERVALLO_MS      30000UL
#define SERIALE_RIEPILOGO_MS    10000UL   // riepilogo delle sonde sulla seriale

// ── Sonde DS18B20 ─────────────────────────────────────────────
// Un bus per sonda: un guasto su un cavo non blocca le altre due. VDD da un GPIO per il
// power-cycle software, e pull-up 4,7 kΩ tra DQ e lo STESSO GPIO di alimentazione (non il 3V3
// fisso), così spegnendo il GPIO la sonda non resta alimentata in parassita dalla linea dati.
// Cablaggio reale della millefori (27/10/2026), numerazione delle sonde dell'utente:
//   sensore 5: DQ GPIO3, VDD GPIO4    sensore 6: DQ GPIO5, VDD GPIO1    sensore 7: DQ GPIO6, VDD GPIO7
// Assegnazione alle vasche (utente, 27/10/2026): F1 = sensore 7, chiller = sensore 6, F2 = sensore 5.
// GPIO3 è un pin di strapping (sorgente del JTAG), ma conta solo con l'eFuse STRAP_JTAG_SEL bruciato, che
// di fabbrica non lo è: come linea dati con il pull-up verso la sua VDD va bene.
#define PIN_SONDA_F1_DQ         6   // sensore 7
#define PIN_SONDA_F1_VDD        7
#define PIN_SONDA_CHILLER_DQ    5   // sensore 6
#define PIN_SONDA_CHILLER_VDD   1
#define PIN_SONDA_F2_DQ         3   // sensore 5
#define PIN_SONDA_F2_VDD        4
// GPIO8 e GPIO9 restano liberi per un OLED I2C (stessa scelta della pin map S3 della V6).

#define SONDA_INTERVALLO_MS        5000UL    // una lettura per sonda ogni 5 s (come la V6)
#define SONDA_CONVERSIONE_MS        750UL    // conversione a 12 bit
#define SONDA_ACCENSIONE_MS         250UL    // dopo l'accensione, prima della prima conversione
#define SONDA_FERMENTATORE_MIN_C    -10.0f   // letture fuori intervallo scartate (come la V6)
#define SONDA_FERMENTATORE_MAX_C     40.0f
#define SONDA_CHILLER_MIN_C         -25.0f   // il glicole può scendere sotto zero
#define SONDA_CHILLER_MAX_C          40.0f

// Recupero di una sonda persa (nessuna lettura valida da SONDA_PERSA_MS), come la 6.5.1:
// power-cycle (VDD e DQ a massa per SONDA_POWER_CYCLE_OFF_MS) dopo 2 min, poi ogni 2 min per i primi
// tre, poi ogni 10 min senza limite.
#define SONDA_PERSA_MS              20000UL
#define SONDA_POWER_CYCLE_PRIMO_MS 120000UL
#define SONDA_POWER_CYCLE_MS       120000UL
#define SONDA_POWER_CYCLE_RAPIDI    3
#define SONDA_POWER_CYCLE_LENTO_MS 600000UL
#define SONDA_POWER_CYCLE_OFF_MS     3000UL
// Riavvio del nodo: una sonda che funzionava ed è persa da NODO_RIAVVIO_DOPO_MS fa riavviare la S3 (un
// riavvio toglie corrente a tutte le sonde). Al massimo NODO_RIAVVII_MAX riavvii di fila (contati in
// memoria RTC), poi solo power-cycle. Il contatore si azzera all'accensione e dopo NODO_RIAVVII_RESET_MS
// con tutte le sonde a posto. Il nodo sensori non comanda nulla: riavviarlo costa pochi secondi di dati.
#define NODO_RIAVVIO_DOPO_MS       600000UL
#define NODO_RIAVVII_MAX            3
#define NODO_RIAVVII_RESET_MS     1800000UL

// Watchdog sul loop: allo scadere panic e riavvio.
#define WDT_TIMEOUT_S               60

// ── LED RGB della scheda (WS2812 sul GPIO48 nella maggior parte delle Super Mini) ──
// Blu: WiFi in connessione. Giallo: WiFi ok, MQTT in connessione. Verde breve: ping inviato.
// Rosso fisso tenue: almeno una sonda persa.
#define PIN_LED_RGB             48
#define LED_LUMINOSITA          16
