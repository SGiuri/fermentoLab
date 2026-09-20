#pragma once
#include <Arduino.h>

// ============================================================
// EventiMQTT
// Struttura di mailbox: la callback MQTT la popola,
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
  bool  test_no_wifi_ricevuto;     // cmd/test_no_wifi
};

// ============================================================
// API connettivita
// ============================================================

// Inizializza il client MQTT e registra il puntatore alla mailbox eventi.
// Chiamare dopo WiFi.begin() e prima di connettivita_mqtt_connetti().
void connettivita_mqtt_init(EventiMQTT* eventi);

// Tenta la connessione al broker (non bloccante: ritorna subito true/false).
// Chiama in loop finché non si connette o si va in timeout.
bool connettivita_mqtt_connetti();

// Da chiamare nel loop() per processare messaggi in arrivo.
void connettivita_mqtt_loop();

// true se il client è attualmente connesso al broker.
bool connettivita_mqtt_connesso();

// ============================================================
// Funzioni di pubblicazione MQTT
// ============================================================

// Invia richiesta profilo attivo a HA (topic richiedi_profilo).
void mqtt_richiedi_profilo_attivo();

// Heartbeat periodico con stato completo del sistema.
// stato_fsm: stringa dello stato corrente della FSM.
void mqtt_pubblica_stato(float temp, float setpoint,
                          const char* step_nome, int step_idx,
                          float ore_step, float ore_totali,
                          bool risc, bool frigo, bool allarme,
                          const char* profilo_nome, const char* stato_fsm);

// Conferma ricezione comandi / eventi di ciclo vita
void mqtt_pubblica_ack(const char* stato, const char* profilo);

// Allarme o ripristino sensore
void mqtt_pubblica_allarme(const char* tipo, int durata_minuti);

// Risposta al ping di diagnostica
void mqtt_pubblica_pong();

// Pubblica un messaggio di log su TOPIC_LOG (chiamata dal modulo log).
// QoS 0, no retain. Silenziosa se il broker non è raggiungibile.
void mqtt_pubblica_log(const char* payload);
