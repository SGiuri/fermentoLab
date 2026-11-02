#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <string>
#include <vector>

struct PubblicazioneMock {
  std::string topic;
  std::string payload;
  bool retain;
  unsigned long ms;
};

// Stato del broker simulato, controllato dai test
extern bool g_mock_broker_raggiungibile;
extern bool g_mock_mqtt_connesso;
extern std::vector<PubblicazioneMock> g_mock_pubblicazioni;
extern std::string g_mock_lwt_topic;
extern std::string g_mock_lwt_payload;
extern bool g_mock_lwt_retain;
extern std::string g_mock_sottoscrizione;

// Consegna un messaggio al firmware come farebbe il broker.
void mock_mqtt_consegna(const std::string& topic, const std::string& payload);
// Simula la caduta della connessione (il broker pubblicherebbe il LWT).
void mock_mqtt_disconnetti();

class PubSubClient {
 public:
  typedef void (*Callback)(char*, uint8_t*, unsigned int);
  explicit PubSubClient(WiFiClientSecure&) {}
  PubSubClient& setClient(WiFiClientSecure&) { return *this; }
  PubSubClient& setServer(const char*, uint16_t) { return *this; }
  PubSubClient& setCallback(Callback cb);
  bool setBufferSize(uint16_t) { return true; }
  PubSubClient& setKeepAlive(uint16_t) { return *this; }
  PubSubClient& setSocketTimeout(uint16_t) { return *this; }
  bool connect(const char* id, const char* user, const char* pass,
               const char* will_topic, uint8_t will_qos, bool will_retain, const char* will_msg);
  bool connected();
  bool publish(const char* topic, const char* payload, bool retained);
  bool subscribe(const char* topic, uint8_t qos);
  bool loop() { return connected(); }
  int state() { return connected() ? 0 : -2; }
};
