#include "mqtt_handler.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient    _wifi_client;
static PubSubClient  _mqtt(_wifi_client);

static OnAvviaCallback _cb_avvia = nullptr;
static OnStopCallback  _cb_stop  = nullptr;

// ============================================================
// Callback interna PubSubClient
// ============================================================
static void _mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Copia in buffer terminato da null (PubSubClient non garantisce \0)
  char buf[4096];
  length = min(length, (unsigned int)(sizeof(buf) - 1));
  memcpy(buf, payload, length);
  buf[length] = '\0';

  if (strcmp(topic, TOPIC_AVVIA) == 0) {
    if (_cb_avvia) _cb_avvia(buf);
  } else if (strcmp(topic, TOPIC_STOP) == 0) {
    if (_cb_stop) _cb_stop();
  } else if (strcmp(topic, TOPIC_PING) == 0) {
    mqtt_pubblica_pong();
  }
}

// ============================================================
// API pubblica
// ============================================================
void mqtt_init(OnAvviaCallback cb_avvia, OnStopCallback cb_stop) {
  _cb_avvia = cb_avvia;
  _cb_stop  = cb_stop;
  _mqtt.setClient(_wifi_client);
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _mqtt.setCallback(_mqtt_callback);
  _mqtt.setBufferSize(4096);  // Buffer grande per profili con molti step
}

bool mqtt_connetti() {
  if (_mqtt.connected()) return true;

  // Last Will Testament: se ESP32 si disconnette senza avvisare,
  // il broker pubblica automaticamente questo messaggio su TOPIC_ALLARME.
  const char* lwt_payload = "{\"tipo\":\"esp32_offline\",\"durata_minuti\":0}";

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                       TOPIC_ALLARME, 1, false, lwt_payload);
  } else {
    ok = _mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr,
                       TOPIC_ALLARME, 1, false, lwt_payload);
  }

  if (ok) {
    _mqtt.subscribe(TOPIC_AVVIA, 1);
    _mqtt.subscribe(TOPIC_STOP,  1);
    _mqtt.subscribe(TOPIC_PING,  0);
  }
  return ok;
}

void mqtt_loop() {
  _mqtt.loop();
}

bool mqtt_connesso() {
  return _mqtt.connected();
}

void mqtt_pubblica_stato(float temp_reale, float setpoint,
                          const char* step_nome, int step_idx,
                          float ore_step, float ore_totali,
                          bool riscaldatore, bool refrigeratore,
                          bool allarme, const char* profilo_nome) {
  StaticJsonDocument<512> doc;
  doc["temp_reale"]    = serialized(String(temp_reale, 2));
  doc["setpoint"]      = serialized(String(setpoint,   2));
  doc["step_nome"]     = step_nome;
  doc["step_index"]    = step_idx;
  doc["ore_step"]      = serialized(String(ore_step,   2));
  doc["ore_totali"]    = serialized(String(ore_totali, 2));
  doc["riscaldatore"]  = riscaldatore;
  doc["refrigeratore"] = refrigeratore;
  doc["allarme"]       = allarme;
  doc["profilo"]       = profilo_nome;

  char buf[512];
  serializeJson(doc, buf);
  // retain=true: HA riceve lo stato corrente anche dopo un riavvio del broker
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
  doc["tipo"]           = tipo;
  doc["durata_minuti"]  = durata_minuti;
  doc["uptime_s"]       = millis() / 1000;

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
