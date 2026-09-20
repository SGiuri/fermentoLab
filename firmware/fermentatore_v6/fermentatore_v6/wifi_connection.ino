// Selezione password WiFi in base al MAC address del dispositivo.
// Le coppie MAC → password sono definite in secrets.h (WIFI_MAC_PASSWORD_MAP).
#include "config.h"
#include <WiFi.h>

// Ritorna la password WiFi corretta per questo ESP32 in base al MAC address.
// Se il MAC non è presente in lista, ritorna stringa vuota → la connessione fallirà
// e il firmware si avvierà in modalità offline (connettivita_mqtt_connesso() = false).
String getWifiPassword() {
  String macAddress = WiFi.macAddress();
  macAddress.toUpperCase();

  WIFI_MAC_PASSWORD_MAP

  return "";
}
