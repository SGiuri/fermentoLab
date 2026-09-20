// Implementazione dei mock usati dai test host del firmware v6.
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "mock_misc.h"
#include "display.h"

#include <fstream>

// ---------------- Tempo ----------------
unsigned long g_mock_ms = 0;
static int64_t g_epoch_base = 0;  // epoch corrispondente a g_mock_ms = 0 (0 = non sincronizzato)

unsigned long millis() { return g_mock_ms; }

void delay(unsigned long ms) { g_mock_ms += ms; }

void mock_imposta_epoch(int64_t epoch_adesso) {
  g_epoch_base = epoch_adesso - (int64_t)(g_mock_ms / 1000UL);
}

void mock_desincronizza_orologio() { g_epoch_base = 0; }

time_t mock_time(time_t* out) {
  const time_t t = (g_epoch_base != 0)
                   ? (time_t)(g_epoch_base + (int64_t)(g_mock_ms / 1000UL))
                   : (time_t)(g_mock_ms / 1000UL);
  if (out) *out = t;
  return t;
}

// ---------------- GPIO ----------------
static int g_pin[64] = {};
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < 64) g_pin[pin] = val;
}
int mock_livello_pin(uint8_t pin) { return pin < 64 ? g_pin[pin] : -1; }

// ---------------- Serial ----------------
MockSerial Serial;
bool g_mock_verbose = false;

size_t MockSerial::print(const char* s) {
  if (g_mock_verbose) fputs(s, stdout);
  return strlen(s);
}
size_t MockSerial::print(char c) {
  if (g_mock_verbose) fputc(c, stdout);
  return 1;
}
size_t MockSerial::println(const char* s) {
  if (g_mock_verbose) printf("%s\n", s);
  return strlen(s) + 1;
}
size_t MockSerial::printf(const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (g_mock_verbose) fputs(buf, stdout);
  return n > 0 ? (size_t)n : 0;
}
int MockSerial::available() { return 0; }
int MockSerial::read() { return -1; }

// ---------------- ESP ----------------
MockEsp ESP;
int g_mock_riavvii = 0;
void MockEsp::restart() {
  g_mock_riavvii++;
  throw RiavvioSimulato{};
}

esp_reset_reason_t g_mock_reset_reason = ESP_RST_POWERON;

// ---------------- Task watchdog ----------------
uint32_t g_mock_wdt_timeout_ms = 5000;
bool g_mock_wdt_loop_iscritto = false;
unsigned long g_mock_wdt_gap_max_ms = 0;
static unsigned long g_wdt_ultimo_reset = 0;

esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* cfg) {
  g_mock_wdt_timeout_ms = cfg->timeout_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* cfg) {
  g_mock_wdt_timeout_ms = cfg->timeout_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_add(void*) {
  g_mock_wdt_loop_iscritto = true;
  g_wdt_ultimo_reset = g_mock_ms;
  return ESP_OK;
}
esp_err_t esp_task_wdt_reset() {
  const unsigned long gap = g_mock_ms - g_wdt_ultimo_reset;
  if (gap > g_mock_wdt_gap_max_ms) g_mock_wdt_gap_max_ms = gap;
  g_wdt_ultimo_reset = g_mock_ms;
  return ESP_OK;
}

// ---------------- ArduinoOTA ----------------
ArduinoOTAClass ArduinoOTA;

// ---------------- WiFi ----------------
MockWiFi WiFi;
String getWifiPassword() { return String("test"); }

// ---------------- MQTT ----------------
bool g_mock_broker_raggiungibile = true;
bool g_mock_mqtt_connesso = false;
std::vector<PubblicazioneMock> g_mock_pubblicazioni;
std::string g_mock_lwt_topic;
std::string g_mock_lwt_payload;
bool g_mock_lwt_retain = false;
std::string g_mock_sottoscrizione;
static PubSubClient::Callback g_callback = nullptr;

PubSubClient& PubSubClient::setCallback(Callback cb) {
  g_callback = cb;
  return *this;
}

bool PubSubClient::connect(const char*, const char*, const char*,
                           const char* will_topic, uint8_t, bool will_retain, const char* will_msg) {
  if (!g_mock_broker_raggiungibile || WiFi.status() != WL_CONNECTED) return false;
  g_mock_lwt_topic = will_topic ? will_topic : "";
  g_mock_lwt_payload = will_msg ? will_msg : "";
  g_mock_lwt_retain = will_retain;
  g_mock_mqtt_connesso = true;
  return true;
}

bool PubSubClient::connected() {
  return g_mock_mqtt_connesso && WiFi.status() == WL_CONNECTED;
}

bool PubSubClient::publish(const char* topic, const char* payload, bool retained) {
  if (!connected()) return false;
  g_mock_pubblicazioni.push_back({topic, payload, retained, g_mock_ms});
  return true;
}

bool PubSubClient::subscribe(const char* topic, uint8_t) {
  g_mock_sottoscrizione = topic;
  return true;
}

void mock_mqtt_consegna(const std::string& topic, const std::string& payload) {
  if (!g_callback || !g_mock_mqtt_connesso) return;
  std::vector<char> t(topic.begin(), topic.end());
  t.push_back('\0');
  std::vector<uint8_t> p(payload.begin(), payload.end());
  g_callback(t.data(), p.data(), (unsigned int)p.size());
}

void mock_mqtt_disconnetti() {
  if (g_mock_mqtt_connesso) {
    g_mock_pubblicazioni.push_back({g_mock_lwt_topic, g_mock_lwt_payload, g_mock_lwt_retain, g_mock_ms});
  }
  g_mock_mqtt_connesso = false;
}

// ---------------- Preferences ----------------
std::map<std::string, std::vector<uint8_t>> g_mock_nvs;
int g_mock_nvs_scritture = 0;

bool Preferences::begin(const char* ns, bool) {
  ns_ = ns;
  return true;
}
bool Preferences::clear() {
  for (auto it = g_mock_nvs.begin(); it != g_mock_nvs.end();) {
    if (it->first.rfind(ns_ + "/", 0) == 0) {
      it = g_mock_nvs.erase(it);
    } else {
      ++it;
    }
  }
  return true;
}
bool Preferences::remove(const char* key) { return g_mock_nvs.erase(k(key)) > 0; }
size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
  const auto* b = static_cast<const uint8_t*>(value);
  std::vector<uint8_t> v(b, b + len);
  auto& slot = g_mock_nvs[k(key)];
  if (slot != v) {  // come NVS reale: nessuna scrittura se il valore non cambia
    slot = v;
    g_mock_nvs_scritture++;
  }
  return len;
}
size_t Preferences::getBytes(const char* key, void* buf, size_t max_len) {
  auto it = g_mock_nvs.find(k(key));
  if (it == g_mock_nvs.end() || it->second.size() > max_len) return 0;
  memcpy(buf, it->second.data(), it->second.size());
  return it->second.size();
}
size_t Preferences::putString(const char* key, const char* value) {
  return putBytes(key, value, strlen(value) + 1);
}
String Preferences::getString(const char* key, const char* def) {
  auto it = g_mock_nvs.find(k(key));
  if (it == g_mock_nvs.end()) return String(def);
  return String(std::string((const char*)it->second.data()));
}
size_t Preferences::putInt(const char* key, int32_t value) { return putBytes(key, &value, sizeof(value)); }
int32_t Preferences::getInt(const char* key, int32_t def) {
  int32_t v = def;
  return getBytes(key, &v, sizeof(v)) == sizeof(v) ? v : def;
}
size_t Preferences::putUInt(const char* key, uint32_t value) { return putBytes(key, &value, sizeof(value)); }
uint32_t Preferences::getUInt(const char* key, uint32_t def) {
  uint32_t v = def;
  return getBytes(key, &v, sizeof(v)) == sizeof(v) ? v : def;
}
size_t Preferences::putFloat(const char* key, float value) { return putBytes(key, &value, sizeof(value)); }
float Preferences::getFloat(const char* key, float def) {
  float v = def;
  return getBytes(key, &v, sizeof(v)) == sizeof(v) ? v : def;
}

void mock_nvs_salva_su_file(const char* path) {
  std::ofstream f(path, std::ios::binary);
  for (const auto& kv : g_mock_nvs) {
    const uint32_t kl = (uint32_t)kv.first.size();
    const uint32_t vl = (uint32_t)kv.second.size();
    f.write((const char*)&kl, 4);
    f.write(kv.first.data(), kl);
    f.write((const char*)&vl, 4);
    f.write((const char*)kv.second.data(), vl);
  }
}

void mock_nvs_carica_da_file(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return;
  g_mock_nvs.clear();
  while (true) {
    uint32_t kl = 0;
    if (!f.read((char*)&kl, 4)) break;
    std::string key(kl, '\0');
    f.read(&key[0], kl);
    uint32_t vl = 0;
    f.read((char*)&vl, 4);
    std::vector<uint8_t> v(vl);
    f.read((char*)v.data(), vl);
    g_mock_nvs[key] = v;
  }
}

// ---------------- DS18B20 ----------------
float g_mock_ds_temp = 20.0f;
int g_mock_ds_richieste = 0;

// ---------------- Display (display.cpp richiede U8g2: sostituito da stub) ----------------
DisplayData g_mock_display = {};
void display_init() {}
void display_aggiorna(const DisplayData& d) { g_mock_display = d; }
const char* display_firmware_version() { return "test"; }
