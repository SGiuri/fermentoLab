#pragma once
#include <Arduino.h>

// ============================================================
// Modulo display OLED SSD1306 128x64
//
// Libreria richiesta: U8g2  (Oliver Kraus, Arduino Library Manager)
//
// Schermate:
//  BOOT       — avvio / connessione WiFi-MQTT
//  OPERATIVE  — RUNNING / STANDBY / HOLDING  (temp, setpoint, step, tempi)
//  ALLARME    — barra invertita + countdown riavvio
//  AUTOTUNE   — autotuning in corso
//
// Uso:
//   display_init();              // in setup(), dopo Serial.begin()
//   display_aggiorna(dd);        // ogni DISPLAY_UPDATE_MS nel loop()
// ============================================================

// Struttura dati da popolare a ogni aggiornamento.
struct DisplayData {
  const char*   stato_fsm;               // "RUNNING","STANDBY","HOLDING","ALLARME","AUTOTUNE","WIFI","MQTT"
  float         temp_reale;              // °C — DS18B20
  bool          temp_valida;             // false → mostra "--.-" (sensore non utilizzabile)
  float         setpoint;               // °C — target corrente
  const char*   step_nome;              // nome step (RUNNING) o stringa vuota
  float         ore_step;               // ore trascorse nello step
  float         ore_totali;             // ore totali dall'avvio profilo
  float         progresso_pct;          // avanzamento profilo 0..100
  float         giorni_prossimo_step;   // giorni mancanti al prossimo step (-1 se N/A)
  const char*   profilo_nome;           // nome profilo o "nessuno"
  bool          riscaldatore_on;
  bool          refrigeratore_on;
  bool          wifi_offline;           // true = WiFi non connesso
  char          wifi_ssid[33];          // SSID della rete connessa
  unsigned long ts_ingresso_allarme_ms; // millis() ingresso in ALLARME (per countdown)
  bool          stima_attiva;           // relè guidati dalla regolazione stimata (sensore perso)
};

void display_init();
void display_aggiorna(const DisplayData& d);
const char* display_firmware_version();
