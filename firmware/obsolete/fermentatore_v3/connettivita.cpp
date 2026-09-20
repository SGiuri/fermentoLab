#include "connettivita.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClientSecure _wifi;
static PubSubClient     _mqtt(_wifi);
static EventiMQTT*  _eventi = nullptr;

// ============================================================
// Callback interna PubSubClient
// ============================================================
static void _on_messaggio(char* topic, byte* payload, unsigned int length) {
  // Buffer null-terminato (PubSubClient non garantisce \0)
  char buf[4096];
  length = min(length, (unsigned int)(sizeof(buf) - 1));
  memcpy(buf, payload, length);
  buf[length] = '\0';

  // --- cmd/profilo: risposta HA alla nostra richiesta profilo ---
  if (strcmp(topic, TOPIC_CMD_PROFILO) == 0 && _eventi) {
    // Distingue subito il tipo di risposta con una ricerca rapida
    if (strstr(buf, "\"steps\"") != nullptr) {
      // Profilo completo
      _eventi->profilo_ricevuto = true;
      strncpy(_eventi->profilo_json, buf, sizeof(_eventi->profilo_json) - 1);
      _eventi->profilo_json[sizeof(_eventi->profilo_json) - 1] = '\0';
    } else {
      // Risposta "nessun_profilo" con temp_standby opzionale
      StaticJsonDocument<128> doc;
      if (!deserializeJson(doc, buf) &&
          strcmp(doc["stato"] | "", "nessun_profilo") == 0) {
        _eventi->nessun_profilo = true;
        if (doc.containsKey("temp_standby")) {
          _eventi->temp_standby_ricevuta = doc["temp_standby"].as<float>();
          _eventi->temp_standby_valida   = true;
        }
      }
    }
    return;
  }

  // --- cmd/stop ---
  if (strcmp(topic, TOPIC_CMD_STOP) == 0 && _eventi) {
    _eventi->stop_ricevuto = true;
    return;
  }

  // --- cmd/temp_standby: {"temp": 18.0} ---
  if (strcmp(topic, TOPIC_CMD_TEMP_STANDBY) == 0 && _eventi) {
    StaticJsonDocument<64> doc;
    if (!deserializeJson(doc, buf) && doc.containsKey("temp")) {
      _eventi->temp_standby_ricevuta = doc["temp"].as<float>();
      _eventi->temp_standby_valida   = true;
    }
    return;
  }

  // --- cmd/ping: rispondi con pong ---
  if (strcmp(topic, TOPIC_CMD_PING) == 0) {
    if (_eventi) _eventi->ping_ricevuto = true;
    mqtt_pubblica_pong();
    return;
  }

  // --- cmd/reboot ---
  if (strcmp(topic, TOPIC_CMD_REBOOT) == 0 && _eventi) {
    _eventi->reboot_ricevuto = true;
    return;
  }

  // --- cmd/test_no_wifi ---
  if (strcmp(topic, TOPIC_CMD_TEST_NO_WIFI) == 0 && _eventi) {
    _eventi->test_no_wifi_ricevuto = true;
    return;
  }
}

// ============================================================
// API pubblica
// ============================================================
void connettivita_mqtt_init(EventiMQTT* eventi) {
  _eventi = eventi;
  _wifi.setInsecure();          // TLS senza verifica certificato (HiveMQ cloud porta 8883)
  _mqtt.setClient(_wifi);
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _mqtt.setCallback(_on_messaggio);
  _mqtt.setBufferSize(4096);  // profili con molti step possono superare 256 byte
}

bool connettivita_mqtt_connetti() {
  if (_mqtt.connected()) return true;

  // Last Will Testament: se l'ESP32 cade senza disconnettersi,
  // il broker pubblica automaticamente questo messaggio su TOPIC_ALLARME.
  const char* lwt = "{\"tipo\":\"esp32_offline\",\"durata_minuti\":0}";

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                       TOPIC_ALLARME, 1, false, lwt);
  } else {
    ok = _mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr,
                       TOPIC_ALLARME, 1, false, lwt);
  }

  if (ok) {
    _mqtt.subscribe(TOPIC_CMD_PROFILO,      1);
    _mqtt.subscribe(TOPIC_CMD_STOP,         1);
    _mqtt.subscribe(TOPIC_CMD_TEMP_STANDBY, 0);
    _mqtt.subscribe(TOPIC_CMD_PING,         0);
    _mqtt.subscribe(TOPIC_CMD_REBOOT,       1);
    _mqtt.subscribe(TOPIC_CMD_TEST_NO_WIFI, 1);
  }
  return ok;
}

void connettivita_mqtt_loop() {
  _mqtt.loop();
}

bool connettivita_mqtt_connesso() {
  return _mqtt.connected();
}

// ============================================================
// Pubblicazioni MQTT
// ============================================================
void mqtt_richiedi_profilo_attivo() {
  time_t ora;
  time(&ora);
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"ts\":%ld,\"client\":\"%s\"}",
           (long)ora, MQTT_CLIENT_ID);
  _mqtt.publish(TOPIC_RICHIEDI_PROFILO, buf, false);
}

void mqtt_pubblica_stato(float temp, float setpoint,
                          const char* step_nome, int step_idx,
                          float ore_step, float ore_totali,
                          bool risc, bool frigo, bool allarme,
                          const char* profilo_nome, const char* stato_fsm) {
  StaticJsonDocument<512> doc;
  doc["temp_reale"]    = serialized(String(temp,     2));
  doc["setpoint"]      = serialized(String(setpoint, 2));
  doc["step_nome"]     = step_nome;
  doc["step_index"]    = step_idx;
  doc["ore_step"]      = serialized(String(ore_step,   1));
  doc["ore_totali"]    = serialized(String(ore_totali, 1));
  doc["riscaldatore"]  = risc;
  doc["refrigeratore"] = frigo;
  doc["allarme"]       = allarme;
  doc["profilo"]       = profilo_nome;
  doc["stato_fsm"]     = stato_fsm;

  char buf[512];
  serializeJson(doc, buf);
  // retain=true: HA riceve lo stato corrente anche dopo riavvio del broker
  _mqtt.publish(TOPIC_STATO, buf, true);
}

void mqtt_pubblica_ack(const char* stato, const char* profilo) {
  StaticJsonDocument<256> doc;
  doc["stato"]    = stato;
  doc["profilo"]  = profilo;
  doc["uptime_s"] = millis() / 1000;

  char buf[256];
  serializeJson(doc, buf);
  _mqtt.publish(TOPIC_ACK, buf, false);
}

void mqtt_pubblica_allarme(const char* tipo, int durata_minuti) {
  StaticJsonDocument<128> doc;
  doc["tipo"]          = tipo;
  doc["durata_minuti"] = durata_minuti;
  doc["uptime_s"]      = millis() / 1000;

  char buf[128];
  serializeJson(doc, buf);
  _mqtt.publish(TOPIC_ALLARME, buf, false);
}

void mqtt_pubblica_pong() {
  StaticJsonDocument<128> doc;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_s"]  = millis() / 1000;
  doc["wifi_rssi"] = WiFi.RSSI();

  char buf[128];
  serializeJson(doc, buf);
  _mqtt.publish(TOPIC_PONG, buf, false);
}

void mqtt_pubblica_log(const char* payload) {
  if (_mqtt.connected()) {
    // QoS 0, no retain: log fire-and-forget, non blocca la FSM
    _mqtt.publish(TOPIC_LOG, payload, false);
  }
}
