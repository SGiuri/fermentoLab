// Questo file gestisce la selezione della password WiFi in base al MAC address.
// La rete "IoT" usa password diverse per ogni dispositivo.
// Le coppie MAC → password sono definite in secrets.h (WIFI_MAC_PASSWORD_MAP).
#include "config.h"

// Ritorna la password WiFi corretta per questo ESP32 in base al MAC address.
// Se il MAC non è in lista, ritorna stringa vuota (connessione fallirà → timeout FSM).
String getWifiPassword() {
  String macAddress = WiFi.macAddress();
  macAddress.toUpperCase();

  WIFI_MAC_PASSWORD_MAP

  return ""; // Ritorna una stringa vuota se il MAC non è in lista
}
