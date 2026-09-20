// ============================================================
// fermentatore_v5_autotuning — Autotuner relay-feedback
//
// Test ONE-SHOT: non toccare durante la fermentazione attiva.
// Caricare sull'ESP32 con il fermentatore vuoto (solo acqua).
//
// SEQUENZA AUTOMATICA:
//   WIFI → MQTT → RISCALDAMENTO_TEST (max 24h, SP 25°C)
//        → PAUSA (5 min)
//        → RAFFREDDAMENTO_TEST (max 2h, SP 14°C)
//        → PUBBLICAZIONE → DONE
//
// RISULTATI:
//   Pubblicati su MQTT: autotuning/f1/risultati  (retain=true)
//   {"fase":"riscaldamento","kp":X,"ki":X,"kd":0.0,"Tu_s":X,"Au_C":X}
//   {"fase":"raffreddamento","kp":X,"ki":X,"kd":0.0,"Tu_s":X,"Au_C":X}
//
// DOPO IL TEST:
//   Leggere i risultati da MQTT, inserire manualmente kp/ki/kd nel
//   profilo JSON inviato a fermentatore_v5 (campi kp_risc/ki_risc,
//   kp_frigo/ki_frigo).
//
// SICUREZZA:
//   - Aborta riscaldamento se temp > AUTOTUNE_RISC_ABORT_TEMP (32°C)
//   - Aborta raffreddamento se temp < AUTOTUNE_FRIGO_ABORT_TEMP (8°C)
//   - Timeout 24h (riscaldamento) / 2h (raffreddamento)
//   - Sensore offline → spegni tutto → riposta allarme
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

#include "config.h"
#include "autotuner.h"
#include "sensor_watchdog.h"
#include "log.h"

// ============================================================
// MQTT
// ============================================================
static WiFiClientSecure _wifi;
static PubSubClient     _mqtt(_wifi);

static void mqtt_pubblica_log(const char* payload) {
  if (_mqtt.connected()) _mqtt.publish(TOPIC_LOG, payload, false);
}

static bool mqtt_connetti() {
  if (_mqtt.connected()) return true;
  _wifi.setInsecure();
  _mqtt.setClient(_wifi);
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _mqtt.setBufferSize(1024);
  return _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
}

static void mqtt_pubblica_risultato(const char* fase,
                                    float kp, float ki, float kd,
                                    float Tu, float Au) {
  StaticJsonDocument<256> doc;
  doc["fase"] = fase;
  doc["kp"]   = serialized(String(kp, 4));
  doc["ki"]   = serialized(String(ki, 6));
  doc["kd"]   = serialized(String(kd, 4));
  doc["Tu_s"] = serialized(String(Tu, 1));
  doc["Au_C"] = serialized(String(Au, 3));
  char buf[256];
  serializeJson(doc, buf);
  // topic separato per fase: HA mantiene entrambi i retain indipendenti
  const char* topic = (strcmp(fase, "riscaldamento") == 0)
                        ? TOPIC_AUTOTUNE_RISULTATI_RISC
                        : TOPIC_AUTOTUNE_RISULTATI_FRIGO;
  _mqtt.publish(topic, buf, true);  // retain
  LOG_I("RESULT", "Fase=%s  kp=%.4f  ki=%.6f  Tu=%.1fs  Au=%.3f°C", fase, kp, ki, Tu, Au);
}

static void mqtt_pubblica_status(const char* msg) {
  _mqtt.publish(TOPIC_AUTOTUNE_STATUS, msg, false);
}

// ============================================================
// Relay helpers
// ============================================================
static void relay_risc(bool on) { digitalWrite(PIN_RISCALDATORE,  on ? RELAY_ON : RELAY_OFF); }
static void relay_frigo(bool on){ digitalWrite(PIN_REFRIGERATORE, on ? RELAY_ON : RELAY_OFF); }

static void relays_off() {
  relay_risc(false);
  relay_frigo(false);
}

// ============================================================
// Variabili globali
// ============================================================
static SensorState g_sensore = {};
static float       g_temp    = 20.0f;
static bool        g_temp_ok = true;

static unsigned long g_ts_ultima_lettura = 0;
static unsigned long g_ts_ultima_status  = 0;
static bool          g_ota_pronto        = false;  // true dopo WiFi connesso e OTA inizializzato

// Risultati
static float g_risc_kp = 0.0f, g_risc_ki = 0.0f, g_risc_kd = 0.0f;
static float g_risc_Tu = 0.0f, g_risc_Au = 0.0f;
static bool  g_risc_ok = false;
static float g_frigo_kp = 0.0f, g_frigo_ki = 0.0f, g_frigo_kd = 0.0f;
static float g_frigo_Tu = 0.0f, g_frigo_Au = 0.0f;
static bool  g_frigo_ok = false;

// ============================================================
// FSM
// ============================================================
enum class Stato : uint8_t {
  WIFI = 0, MQTT_CONN,
  RISCALDAMENTO_TEST, PAUSA,
  RAFFREDDAMENTO_TEST,
  PUBBLICAZIONE, DONE, ERRORE
};

static Stato         g_stato           = Stato::WIFI;
static unsigned long g_ts_stato_inizio = 0;

static void entra(Stato s) {
  g_stato           = s;
  g_ts_stato_inizio = millis();
  relays_off();
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  log_init(mqtt_pubblica_log);

  pinMode(PIN_RISCALDATORE,  OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  relays_off();

  sensor_init(g_sensore);

  LOG_I("BOOT", "=== Autotuner V5 avvio ===");
  LOG_I("BOOT", "Relay-feedback: risc SP=%.1f°C  frigo SP=%.1f°C",
        AUTOTUNE_RISC_SETPOINT, AUTOTUNE_FRIGO_SETPOINT);
}

// ============================================================
// loop()
// ============================================================
void loop() {
  unsigned long ora = millis();

  // ---- OTA handle (solo quando WiFi connesso) ----
  if (g_ota_pronto && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  // ---- Lettura sensore ----
  if (ora - g_ts_ultima_lettura >= SENSOR_READ_INTERVAL_MS) {
    g_ts_ultima_lettura = ora;
    g_temp_ok = sensor_leggi(g_sensore, g_temp, 5);

    if (sensor_allarme_nuovo(g_sensore)) {
      LOG_E("SENSORE", "Allarme: DS18B20 offline!");
      relays_off();
      entra(Stato::ERRORE);
    }
  }

  // ---- MQTT loop ----
  if (_mqtt.connected()) _mqtt.loop();

  // ---- Status heartbeat ogni 10s ----
  if (ora - g_ts_ultima_status >= 10000UL && _mqtt.connected()) {
    g_ts_ultima_status = ora;
    char buf[64];
    snprintf(buf, sizeof(buf), "T=%.2fC stato=%d", g_temp, (int)g_stato);
    mqtt_pubblica_status(buf);
  }

  // ---- FSM ----
  switch (g_stato) {

    case Stato::WIFI:
      if (WiFi.status() != WL_CONNECTED && ora - g_ts_stato_inizio < 100) {
        WiFi.mode(WIFI_STA);
        String pwd = getWifiPassword();
        WiFi.begin(WIFI_SSID, pwd.c_str());
        LOG_I("WiFi", "Connessione a %s...", WIFI_SSID);
      }
      if (WiFi.status() == WL_CONNECTED) {
        configTime(NTP_OFFSET, NTP_DST, NTP_SERVER);
        LOG_I("WiFi", "Connesso: %s", WiFi.localIP().toString().c_str());
        // Setup OTA
        ArduinoOTA.setHostname(OTA_HOSTNAME);
        ArduinoOTA.setPassword(OTA_PASSWORD);
        ArduinoOTA.begin();
        g_ota_pronto = true;
        LOG_I("OTA", "Pronto su hostname '%s'.", OTA_HOSTNAME);
        entra(Stato::MQTT_CONN);
      } else if (ora - g_ts_stato_inizio >= WIFI_TIMEOUT_MS) {
        LOG_W("WiFi", "Timeout - continuo senza rete.");
        // L'autotuner funziona anche senza rete (solo Serial)
        entra(Stato::RISCALDAMENTO_TEST);
      }
      break;

    case Stato::MQTT_CONN:
      if (mqtt_connetti()) {
        LOG_I("MQTT", "Connesso al broker.");
        entra(Stato::RISCALDAMENTO_TEST);
      } else if (ora - g_ts_stato_inizio >= MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W("MQTT", "Timeout - continuo senza MQTT.");
        entra(Stato::RISCALDAMENTO_TEST);
      }
      break;

    case Stato::RISCALDAMENTO_TEST: {
      static bool _inizializzato = false;
      if (!_inizializzato) {
        _inizializzato = true;
        autotuner_init(AUTOTUNE_RISC_SETPOINT, AUTOTUNE_RISC_RELAY_AMP);
        LOG_I("RISC", "Test riscaldamento avviato. SP=%.1f°C max=%dh",
              AUTOTUNE_RISC_SETPOINT, (int)AUTOTUNE_RISC_TIMEOUT_H);
        mqtt_pubblica_status("RISCALDAMENTO_TEST");
      }

      // Aborta per temperatura limite di sicurezza
      if (g_temp_ok && g_temp > AUTOTUNE_RISC_ABORT_TEMP) {
        LOG_E("RISC", "ABORT: temperatura %.1f°C > limite %.1f°C",
              g_temp, AUTOTUNE_RISC_ABORT_TEMP);
        relay_risc(false);
        entra(Stato::ERRORE);
        break;
      }

      // Timeout
      float ore = (ora - g_ts_stato_inizio) / 3600000.0f;
      if (ore >= AUTOTUNE_RISC_TIMEOUT_H) {
        LOG_W("RISC", "Timeout %.0fh - nessuna oscillazione convergente.", AUTOTUNE_RISC_TIMEOUT_H);
        relay_risc(false);
        entra(Stato::PAUSA);
        break;
      }

      // Esegui autotuner (solo se sensore valido)
      if (g_temp_ok) {
        bool relay_on;
        bool done = autotuner_aggiorna(g_temp, relay_on);
        relay_risc(relay_on);

        if (done) {
          relay_risc(false);
          autotuner_get_gains(g_risc_kp, g_risc_ki, g_risc_kd);
          g_risc_Tu = autotuner_get_Tu();
          g_risc_Au = autotuner_get_Au();
          g_risc_ok = true;
          LOG_I("RISC", "Completato. kp=%.4f ki=%.6f Tu=%.1fs Au=%.3f°C",
                g_risc_kp, g_risc_ki, g_risc_Tu, g_risc_Au);
          _inizializzato = false;
          entra(Stato::PAUSA);
        }
      } else {
        relay_risc(false);  // sensore non valido: spegni per sicurezza
      }
      break;
    }

    case Stato::PAUSA:
      if (ora - g_ts_stato_inizio >= (unsigned long)AUTOTUNE_PAUSA_S * 1000UL) {
        LOG_I("PAUSA", "Pausa completata. Avvio test raffreddamento.");
        entra(Stato::RAFFREDDAMENTO_TEST);
      }
      break;

    case Stato::RAFFREDDAMENTO_TEST: {
      static bool _inizializzato_f = false;
      if (!_inizializzato_f) {
        _inizializzato_f = true;
        autotuner_init(AUTOTUNE_FRIGO_SETPOINT, AUTOTUNE_FRIGO_RELAY_AMP);
        LOG_I("FRIGO", "Test raffreddamento avviato. SP=%.1f°C max=%dh",
              AUTOTUNE_FRIGO_SETPOINT, (int)AUTOTUNE_FRIGO_TIMEOUT_H);
        mqtt_pubblica_status("RAFFREDDAMENTO_TEST");
      }

      // Aborta per temperatura limite di sicurezza
      if (g_temp_ok && g_temp < AUTOTUNE_FRIGO_ABORT_TEMP) {
        LOG_E("FRIGO", "ABORT: temperatura %.1f°C < limite %.1f°C",
              g_temp, AUTOTUNE_FRIGO_ABORT_TEMP);
        relay_frigo(false);
        entra(Stato::ERRORE);
        break;
      }

      // Timeout
      float ore = (ora - g_ts_stato_inizio) / 3600000.0f;
      if (ore >= AUTOTUNE_FRIGO_TIMEOUT_H) {
        LOG_W("FRIGO", "Timeout %.0fh - nessuna oscillazione convergente.", AUTOTUNE_FRIGO_TIMEOUT_H);
        relay_frigo(false);
        entra(Stato::PUBBLICAZIONE);
        break;
      }

      if (g_temp_ok) {
        bool relay_on;
        bool done = autotuner_aggiorna(g_temp, relay_on);
        relay_frigo(relay_on);

        if (done) {
          relay_frigo(false);
          autotuner_get_gains(g_frigo_kp, g_frigo_ki, g_frigo_kd);
          g_frigo_Tu = autotuner_get_Tu();
          g_frigo_Au = autotuner_get_Au();
          g_frigo_ok = true;
          LOG_I("FRIGO", "Completato. kp=%.4f ki=%.6f Tu=%.1fs Au=%.3f°C",
                g_frigo_kp, g_frigo_ki, g_frigo_Tu, g_frigo_Au);
          _inizializzato_f = false;
          entra(Stato::PUBBLICAZIONE);
        }
      } else {
        relay_frigo(false);
      }
      break;
    }

    case Stato::PUBBLICAZIONE:
      relays_off();
      if (!_mqtt.connected()) mqtt_connetti();
      if (_mqtt.connected()) {
        if (g_risc_ok) {
          mqtt_pubblica_risultato("riscaldamento",
            g_risc_kp, g_risc_ki, g_risc_kd, g_risc_Tu, g_risc_Au);
        } else {
          LOG_W("PUB", "Risultato riscaldamento non disponibile (timeout/abort).");
          mqtt_pubblica_status("RISC_TIMEOUT_NO_RESULT");
        }
        delay(200);
        if (g_frigo_ok) {
          mqtt_pubblica_risultato("raffreddamento",
            g_frigo_kp, g_frigo_ki, g_frigo_kd, g_frigo_Tu, g_frigo_Au);
        } else {
          LOG_W("PUB", "Risultato raffreddamento non disponibile (timeout/abort).");
          mqtt_pubblica_status("FRIGO_TIMEOUT_NO_RESULT");
        }
        delay(200);
        mqtt_pubblica_status("DONE");
        _mqtt.loop();
        delay(500);  // flush
      }
      LOG_I("DONE", "Autotuning completato. Risultati su MQTT: " TOPIC_AUTOTUNE_RISULTATI);
      LOG_I("DONE", "Ora copia kp/ki in profilo JSON di fermentatore_v5:");
      if (g_risc_ok)  LOG_I("DONE", "  kp_risc=%.4f  ki_risc=%.6f",  g_risc_kp,  g_risc_ki);
      if (g_frigo_ok) LOG_I("DONE", "  kp_frigo=%.4f ki_frigo=%.6f", g_frigo_kp, g_frigo_ki);
      entra(Stato::DONE);
      break;

    case Stato::DONE:
      // Idle: nessuna operazione. Riavviare l'ESP32 per un nuovo test.
      if (_mqtt.connected()) _mqtt.loop();
      break;

    case Stato::ERRORE:
      relays_off();
      if (_mqtt.connected()) {
        mqtt_pubblica_status("ERRORE_SENSORE_O_TEMPERATURA");
        _mqtt.loop();
      }
      LOG_E("ERRORE", "Test abortito. Riavviare per riprovare.");
      // Rimane in ERRORE (nessuna operazione sugli attuatori)
      break;
  }
}
