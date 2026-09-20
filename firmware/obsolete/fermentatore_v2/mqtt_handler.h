#pragma once
#include <Arduino.h>

// Callback MQTT
typedef void (*OnAvviaCallback)(const char* payload_json);
typedef void (*OnStopCallback)();

// Inizializza la connessione MQTT e registra le callback.
void mqtt_init(OnAvviaCallback cb_avvia, OnStopCallback cb_stop);

// Tenta la connessione/riconnessione al broker.
// Restituisce true se connesso.
bool mqtt_connetti();

// Da chiamare nel loop() per processare messaggi in arrivo.
void mqtt_loop();

// Pubblica lo stato periodico sul topic fermentazione/f1/stato.
void mqtt_pubblica_stato(float temp_reale, float setpoint,
                          const char* step_nome, int step_idx,
                          float ore_step, float ore_totali,
                          bool riscaldatore, bool refrigeratore,
                          bool allarme, const char* profilo_nome);

// Pubblica un messaggio di ACK su fermentazione/f1/ack.
void mqtt_pubblica_ack(const char* stato, const char* profilo);

// Pubblica un evento su fermentazione/f1/allarme.
void mqtt_pubblica_allarme(const char* tipo, int durata_minuti);

// Risponde al ping su fermentazione/f1/pong.
void mqtt_pubblica_pong();

// Restituisce true se il client MQTT è connesso.
bool mqtt_connesso();
