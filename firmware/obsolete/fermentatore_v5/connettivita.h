#pragma once
#include <Arduino.h>

// ============================================================
// EventiMQTT
// Struttura mailbox: la callback MQTT la popola,
// la macchina a stati nel loop() la consuma.
// ============================================================
struct EventiMQTT {
  bool  profilo_ricevuto;         // profilo JSON valido su cmd/profilo
  char  profilo_json[4096];       // payload grezzo (solo se profilo_ricevuto)

  bool  nessun_profilo;           // HA ha risposto "nessun_profilo"
  float temp_standby_ricevuta;    // temp standby da HA (con nessun_profilo o cmd/temp_standby)
  bool  temp_standby_valida;

  bool  stop_ricevuto;            // cmd/stop
  bool  ping_ricevuto;            // cmd/ping
  bool  reboot_ricevuto;          // cmd/reboot
  bool  test_no_wifi_ricevuto;    // cmd/test_no_wifi
};

// ============================================================
// API connettivita
// ============================================================

void connettivita_mqtt_init(EventiMQTT* eventi);
bool connettivita_mqtt_connetti();
void connettivita_mqtt_loop();
bool connettivita_mqtt_connesso();

// ============================================================
// Funzioni di pubblicazione MQTT
// ============================================================

void mqtt_richiedi_profilo_attivo();

// Heartbeat periodico con stato completo del sistema (v5: include campi PID).
void mqtt_pubblica_stato(float temp, float setpoint,
                          const char* step_nome, int step_idx,
                          float ore_step, float ore_totali,
                          bool risc, bool frigo, bool allarme,
                          const char* profilo_nome, const char* stato_fsm,
                          float pid_output, float duty_risc, float duty_frigo);

void mqtt_pubblica_ack(const char* stato, const char* profilo);
void mqtt_pubblica_allarme(const char* tipo, int durata_minuti);
void mqtt_pubblica_pong();
void mqtt_pubblica_log(const char* payload);
