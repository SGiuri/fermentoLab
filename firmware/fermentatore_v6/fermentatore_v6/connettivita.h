#pragma once
#include <Arduino.h>
#include "controller.h"
#include "autotune.h"
#include "sensor_watchdog.h"

// ============================================================
// Callback comandi MQTT
// La callback PubSubClient interna chiama questa funzione con copie di topic+payload.
// ============================================================
typedef void (*MqttCommandHandlerFn)(const char* topic, const char* payload);

// ============================================================
// Payload heartbeat esp/state (HASS_V6_SPEC §2.1)
// ============================================================
struct StatoHeartbeat {
  const char* stato_fsm;         // "RUNNING"|"STOPPED"|"HOLDING"|"AUTOTUNE"|"ALLARME_SENSORE"
  const char* control_mode;      // "on_off"|...|"P"|"PI"|"PD"|"PID"
  bool        temp_disponibile;  // false → temp_reale pubblicata come null (mai letta)
  float       temp_reale;        // ultima lettura valida
  float       setpoint;
  bool        riscaldatore;      // stato fisico del relè
  bool        refrigeratore;     // stato fisico del relè
  float       duty_riscaldatore;
  float       duty_refrigeratore;
  float       pid_output;        // positivo=caldo, negativo=freddo
  const char* step_nome;         // "" se nessun profilo attivo
  int         step_index;
  float       ore_step;
  float       ore_totali;
  const char* profilo_nome;      // "" se nessun profilo
  bool        allarme;
  // Diagnostica per monitoraggio remoto
  bool        sensore_ok;        // temperatura utilizzabile per il controllo
  int32_t     temp_eta_s;        // età ultima lettura valida, -1 se mai letta
  bool        ntp_sync;
  const char* reset_reason;
  int         rssi;
  uint32_t    commutazioni;
  SensorDiagnostica sensore_diag;  // letture del DS18B20 dal boot (firmware >= 6.4.1)
};

// ============================================================
// Payload esp/controllo: grandezze interne del controllore (didattica)
// ============================================================
struct StatoControllo {
  const char* control_mode;
  bool        attivo;            // controllo in RUNNING con temperatura utilizzabile
  float       temp;
  float       setpoint;
  float       errore;            // SP - T [°C]
  float       p;                 // [%]
  float       i;                 // [%]
  float       d;                 // [%]
  float       uscita;            // P+I+D limitata a ±100 [%] (on/off: +100/-100/0)
  float       integrale;         // [°C·s]
  float       duty_caldo;        // duty applicato [%]
  float       duty_freddo;
  bool        rele_caldo;        // stato fisico
  bool        rele_freddo;
  float       isteresi;
  float       isteresi_caldo;
  float       isteresi_freddo;
  float       kp_caldo, ki_caldo, kd_caldo;
  float       kp_freddo, ki_freddo, kd_freddo;
  float       zona_morta;        // [%]
  float       uscita_minima;     // [%]
};

// ============================================================
// Lifecycle WiFi/MQTT
// ============================================================
void connettivita_mqtt_init(MqttCommandHandlerFn handler);
bool connettivita_mqtt_connetti();
void connettivita_mqtt_loop();
bool connettivita_mqtt_connesso();
int  connettivita_mqtt_stato();
// Millisecondi dall'ultima SUBSCRIBE; i messaggi retained arrivano subito dopo.
unsigned long connettivita_ms_da_sottoscrizione();
// Pubblica "offline" retained prima di un riavvio volontario (il LWT scatterebbe in ritardo).
void connettivita_annuncia_offline();
// Nome profilo incluso negli ack (puntatore a buffer statico, letto a ogni ack).
void mqtt_imposta_nome_profilo(const char* nome);

// ============================================================
// Pubblicazioni MQTT
// ============================================================
void mqtt_pubblica_stato(const StatoHeartbeat& stato);
void mqtt_pubblica_controllo(const StatoControllo& c);
void mqtt_pubblica_ack(const char* request_id,
                       const char* command,
                       const char* status,
                       const char* reason_code);
void mqtt_pubblica_allarme(const char* tipo, uint32_t durata_minuti);
void mqtt_pubblica_autotune_state(const AutotuneContext& autotune, float temp_c);
void mqtt_pubblica_autotune_result(const AutotuneContext& autotune);
void mqtt_pubblica_log(const char* payload);
