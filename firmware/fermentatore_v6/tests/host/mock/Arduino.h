#pragma once
// Mock minimale del core Arduino-ESP32 per compilare il firmware v6 su PC (MSVC).
// Il tempo è simulato: millis() e delay() usano g_mock_ms, time() un epoch controllabile.

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cctype>
#include <cmath>
#include <ctime>
#include <climits>
#include <string>
#include <functional>

typedef uint8_t byte;
typedef bool boolean;

#define LOW    0x0
#define HIGH   0x1
#define INPUT  0x01
#define OUTPUT 0x03

#define RTC_NOINIT_ATTR

// ---------------- Tempo ----------------
extern unsigned long g_mock_ms;
unsigned long millis();
void delay(unsigned long ms);

// Epoch simulato. 0 = orologio non sincronizzato (time() restituisce solo l'uptime).
void mock_imposta_epoch(int64_t epoch_adesso);
void mock_desincronizza_orologio();
time_t mock_time(time_t* out);
#define time(x) mock_time(x)
inline void configTime(long, int, const char*) {}

// ---------------- GPIO ----------------
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  mock_livello_pin(uint8_t pin);

// ---------------- String ----------------
class String {
 public:
  String() {}
  String(const char* c) : s_(c ? c : "") {}
  String(const std::string& x) : s_(x) {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  void toUpperCase() {
    for (auto& ch : s_) ch = (char)toupper((unsigned char)ch);
  }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator==(const String& o) const { return s_ == o.s_; }
 private:
  std::string s_;
};

// ---------------- Serial ----------------
class MockSerial {
 public:
  void begin(unsigned long) {}
  size_t print(const char* s);
  size_t print(char c);
  size_t println(const char* s = "");
  size_t printf(const char* fmt, ...);
  int available();
  int read();
};
extern MockSerial Serial;
extern bool g_mock_verbose;

// ---------------- ESP ----------------
struct RiavvioSimulato {};  // lanciata da ESP.restart()
class MockEsp {
 public:
  void restart();
  uint32_t getFreeHeap() { return 200000; }
};
extern MockEsp ESP;
extern int g_mock_riavvii;
