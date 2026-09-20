#pragma once
#include <Arduino.h>
#include <string>

typedef enum {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6
} wl_status_t;

#define WIFI_STA 1

struct IPAddress {
  String toString() const { return String("192.168.1.50"); }
};

class MockWiFi {
 public:
  wl_status_t status() { return stato; }
  void mode(int) {}
  bool disconnect(bool = false, bool = false) { return true; }
  void begin(const char* ssid, const char*) {
    tentativi++;
    ultimo_ssid = ssid ? ssid : "";
    if (rete_disponibile && ultimo_ssid == rete_ok) stato = WL_CONNECTED;
  }
  int scanNetworks() { return hotspot_visibile ? 1 : 0; }
  String SSID() { return String(ultimo_ssid); }
  String SSID(int) { return String(hotspot_visibile ? "HotIoT" : ""); }
  void scanDelete() {}
  int RSSI() { return -61; }
  IPAddress localIP() { return IPAddress(); }
  String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }

  // Controllo dai test
  wl_status_t stato = WL_DISCONNECTED;
  bool rete_disponibile = true;
  bool hotspot_visibile = false;
  std::string rete_ok = "IoT";
  std::string ultimo_ssid;
  int tentativi = 0;
};
extern MockWiFi WiFi;

String getWifiPassword();
