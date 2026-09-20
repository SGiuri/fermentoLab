#pragma once
// Header di supporto: raccoglie i mock ESP-IDF e delle librerie secondarie.
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

// ---------------- esp_system ----------------
typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO
} esp_reset_reason_t;
extern esp_reset_reason_t g_mock_reset_reason;
inline esp_reset_reason_t esp_reset_reason() { return g_mock_reset_reason; }

// ---------------- esp_task_wdt ----------------
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
typedef struct {
  uint32_t timeout_ms;
  uint32_t idle_core_mask;
  bool trigger_panic;
} esp_task_wdt_config_t;
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* cfg);
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* cfg);
esp_err_t esp_task_wdt_add(void* task);
esp_err_t esp_task_wdt_reset();
extern uint32_t g_mock_wdt_timeout_ms;
extern bool g_mock_wdt_loop_iscritto;
extern unsigned long g_mock_wdt_gap_max_ms;

// ---------------- esp_log ----------------
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
inline void esp_log_level_set(const char*, esp_log_level_t) {}

// ---------------- ArduinoOTA ----------------
class ArduinoOTAClass {
 public:
  void setHostname(const char*) {}
  void setPassword(const char*) {}
  void onStart(std::function<void()> f) { on_start = f; }
  void onProgress(std::function<void(unsigned int, unsigned int)> f) { on_progress = f; }
  void begin() { avviato++; }
  void handle() {}
  std::function<void()> on_start;
  std::function<void(unsigned int, unsigned int)> on_progress;
  int avviato = 0;
};
extern ArduinoOTAClass ArduinoOTA;

// ---------------- WiFiClientSecure ----------------
class WiFiClientSecure {
 public:
  void setInsecure() {}
  void setHandshakeTimeout(unsigned long) {}
  void setConnectionTimeout(uint32_t) {}
};

// ---------------- Preferences (NVS in memoria, persistibile su file) ----------------
extern std::map<std::string, std::vector<uint8_t>> g_mock_nvs;
extern int g_mock_nvs_scritture;
void mock_nvs_carica_da_file(const char* path);
void mock_nvs_salva_su_file(const char* path);

class Preferences {
 public:
  bool begin(const char* ns, bool read_only = false);
  void end() {}
  bool clear();
  bool remove(const char* key);
  size_t putBytes(const char* key, const void* value, size_t len);
  size_t getBytes(const char* key, void* buf, size_t max_len);
  size_t putString(const char* key, const char* value);
  String getString(const char* key, const char* def = "");
  size_t putInt(const char* key, int32_t value);
  int32_t getInt(const char* key, int32_t def = 0);
  size_t putUInt(const char* key, uint32_t value);
  uint32_t getUInt(const char* key, uint32_t def = 0);
  size_t putFloat(const char* key, float value);
  float getFloat(const char* key, float def = 0.0f);
 private:
  std::string k(const char* key) const { return ns_ + "/" + key; }
  std::string ns_;
};

// ---------------- DS18B20 ----------------
#define DEVICE_DISCONNECTED_C -127
extern float g_mock_ds_temp;       // valore restituito dal sensore
extern int   g_mock_ds_richieste;  // conversioni avviate
class OneWire {
 public:
  explicit OneWire(uint8_t) {}
};
class DallasTemperature {
 public:
  explicit DallasTemperature(OneWire*) {}
  void begin() {}
  void setWaitForConversion(bool) {}
  void setResolution(uint8_t) {}
  void requestTemperatures() { g_mock_ds_richieste++; }
  float getTempCByIndex(uint8_t) { return g_mock_ds_temp; }
};
