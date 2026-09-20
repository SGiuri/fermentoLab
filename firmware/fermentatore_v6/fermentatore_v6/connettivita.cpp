#include "connettivita.h"
#include "config.h"
#include "controller.h"
#include "autotune.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

static WiFiClientSecure     _wifi;
static PubSubClient         _mqtt(_wifi);
static MqttCommandHandlerFn _cmd_handler = nullptr;
static unsigned long        _ts_sottoscrizione_ms = 0;
static const char*          _nome_profilo = "";

// ============================================================
// Callback interna PubSubClient
// ============================================================
static void _on_messaggio(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length >= (unsigned int)MQTT_BUFFER_SIZE) return;
  // static: evita overflow dello stack loopTask. Anche il topic va copiato: punta al
  // buffer interno di PubSubClient, che viene sovrascritto dal primo publish (ack).
  static char buf[MQTT_BUFFER_SIZE];
  static char topic_buf[128];
  memcpy(buf, payload, length);
  buf[length] = '\0';
  strncpy(topic_buf, topic, sizeof(topic_buf) - 1);
  topic_buf[sizeof(topic_buf) - 1] = '\0';
  if (_cmd_handler) {
    _cmd_handler(topic_buf, buf);
  }
}

// ============================================================
// Lifecycle
// ============================================================
void connettivita_mqtt_init(MqttCommandHandlerFn handler) {
  _cmd_handler = handler;
  _wifi.setInsecure();   // TLS senza verifica certificato (HiveMQ Cloud porta 8883)
  _wifi.setHandshakeTimeout(MQTT_TLS_HANDSHAKE_S);
  _wifi.setConnectionTimeout(MQTT_TCP_CONNECT_MS);
  _mqtt.setClient(_wifi);
  _mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  _mqtt.setCallback(_on_messaggio);
  _mqtt.setBufferSize(MQTT_BUFFER_SIZE);
  _mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  _mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
}

bool connettivita_mqtt_connetti() {
  if (_mqtt.connected()) return true;

  // LWT su topic di disponibilità dedicato, retained: HA vede "offline" anche se
  // si riavvia dopo la caduta dell'ESP (prima era su esp/alarm senza retain).
  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = _mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                       TOPIC_AVAILABILITY, 1, true, PAYLOAD_OFFLINE);
  } else {
    ok = _mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr,
                       TOPIC_AVAILABILITY, 1, true, PAYLOAD_OFFLINE);
  }

  if (ok) {
    _mqtt.publish(TOPIC_AVAILABILITY, PAYLOAD_ONLINE, true);
    // Sottoscrivi tutti i comandi con wildcard QoS 1
    _mqtt.subscribe(TOPIC_PREFIX_HA "/#", 1);
    _ts_sottoscrizione_ms = millis();
  }
  return ok;
}

void connettivita_mqtt_loop() {
  _mqtt.loop();
}

bool connettivita_mqtt_connesso() {
  return _mqtt.connected();
}

int connettivita_mqtt_stato() {
  return _mqtt.state();
}

unsigned long connettivita_ms_da_sottoscrizione() {
  return millis() - _ts_sottoscrizione_ms;
}

void connettivita_annuncia_offline() {
  if (_mqtt.connected()) {
    _mqtt.publish(TOPIC_AVAILABILITY, PAYLOAD_OFFLINE, true);
  }
}

void mqtt_imposta_nome_profilo(const char* nome) {
  _nome_profilo = nome ? nome : "";
}

// ============================================================
// Helper interno: pubblica su MQTT + log seriale (fallback offline)
// ============================================================
static void _pubblica(const char* topic, const char* payload, bool retain) {
  if (_mqtt.connected()) {
    _mqtt.publish(topic, payload, retain);
  }
  // Fallback seriale sempre attivo per debug
  Serial.print(topic);
  Serial.print(' ');
  Serial.println(payload);
}

// ============================================================
// Pubblicazioni MQTT
// ============================================================
void mqtt_pubblica_stato(const StatoHeartbeat& s) {
  char temp_buf[16];
  if (s.temp_disponibile) {
    snprintf(temp_buf, sizeof(temp_buf), "%.2f", s.temp_reale);
  } else {
    strcpy(temp_buf, "null");
  }

  char payload[1024];
  const int n = snprintf(payload, sizeof(payload),
           "{\"temp_reale\":%s,\"setpoint\":%.2f,"
           "\"control_mode\":\"%s\","
           "\"step_nome\":\"%s\",\"step_index\":%d,"
           "\"ore_step\":%.1f,\"ore_totali\":%.1f,"
           "\"riscaldatore\":%s,\"refrigeratore\":%s,"
           "\"allarme\":%s,"
           "\"profilo\":\"%s\","
           "\"stato_fsm\":\"%s\","
           "\"pid_output\":%.1f,"
           "\"duty_riscaldatore\":%.1f,\"duty_refrigeratore\":%.1f,"
           "\"sensore_ok\":%s,\"temp_eta_s\":%ld,"
           "\"ntp_sync\":%s,\"uptime_s\":%lu,\"reset_reason\":\"%s\","
           "\"rssi\":%d,\"heap_libero\":%lu,\"commutazioni\":%lu,"
           "\"sensore_diag\":{\"letture\":%lu,\"non_valide\":%lu,\"scollegato\":%lu,"
           "\"poweron_85\":%lu,\"fuori_scala\":%lu,\"picco\":%lu,"
           "\"vicino_rele\":%lu,\"power_cycle\":%lu},"
           "\"firmware\":\"%s\","
           "\"hardware_target\":\"%s\","
           "\"fermenter_id\":\"%s\"}",
           temp_buf, s.setpoint,
           s.control_mode  ? s.control_mode  : "",
           s.step_nome     ? s.step_nome     : "",
           s.step_index,
           s.ore_step, s.ore_totali,
           s.riscaldatore  ? "true" : "false",
           s.refrigeratore ? "true" : "false",
           s.allarme       ? "true" : "false",
           s.profilo_nome  ? s.profilo_nome  : "",
           s.stato_fsm     ? s.stato_fsm     : "STOPPED",
           s.pid_output,
           s.duty_riscaldatore, s.duty_refrigeratore,
           s.sensore_ok    ? "true" : "false",
           (long)s.temp_eta_s,
           s.ntp_sync      ? "true" : "false",
           (unsigned long)(millis() / 1000UL),
           s.reset_reason  ? s.reset_reason  : "",
           s.rssi,
           (unsigned long)ESP.getFreeHeap(),
           (unsigned long)s.commutazioni,
           (unsigned long)s.sensore_diag.letture,
           (unsigned long)s.sensore_diag.non_valide,
           (unsigned long)s.sensore_diag.scollegato,
           (unsigned long)s.sensore_diag.poweron_85,
           (unsigned long)s.sensore_diag.fuori_scala,
           (unsigned long)s.sensore_diag.picco,
           (unsigned long)s.sensore_diag.vicino_rele,
           (unsigned long)s.sensore_diag.power_cycle,
           FIRMWARE_VERSION,
           DEVICE_HARDWARE_TARGET,
           DEVICE_FERMENTER_ID);
  if (n <= 0 || n >= (int)sizeof(payload)) return;  // mai JSON troncato
  // retain=true: HA riceve lo stato corrente anche dopo riavvio del broker
  _pubblica(TOPIC_STATE, payload, true);
}

void mqtt_pubblica_controllo(const StatoControllo& c) {
  char payload[768];
  const int n = snprintf(payload, sizeof(payload),
           "{\"control_mode\":\"%s\",\"attivo\":%s,"
           "\"temp\":%.2f,\"setpoint\":%.2f,\"errore\":%.3f,"
           "\"p\":%.2f,\"i\":%.2f,\"d\":%.3f,\"uscita\":%.2f,\"integrale\":%.1f,"
           "\"duty_caldo\":%.1f,\"duty_freddo\":%.1f,"
           "\"rele_caldo\":%s,\"rele_freddo\":%s,"
           "\"isteresi\":%.2f,\"isteresi_caldo\":%.2f,\"isteresi_freddo\":%.2f,"
           "\"kp_caldo\":%.3f,\"ki_caldo\":%.5f,\"kd_caldo\":%.2f,"
           "\"kp_freddo\":%.3f,\"ki_freddo\":%.5f,\"kd_freddo\":%.2f,"
           "\"zona_morta\":%.1f,\"uscita_minima\":%.1f,"
           "\"finestra_s\":%lu,\"uptime_s\":%lu}",
           c.control_mode ? c.control_mode : "", c.attivo ? "true" : "false",
           c.temp, c.setpoint, c.errore,
           c.p, c.i, c.d, c.uscita, c.integrale,
           c.duty_caldo, c.duty_freddo,
           c.rele_caldo ? "true" : "false", c.rele_freddo ? "true" : "false",
           c.isteresi, c.isteresi_caldo, c.isteresi_freddo,
           c.kp_caldo, c.ki_caldo, c.kd_caldo,
           c.kp_freddo, c.ki_freddo, c.kd_freddo,
           c.zona_morta, c.uscita_minima,
           (unsigned long)(PID_DUTY_WINDOW_MS / 1000UL), (unsigned long)(millis() / 1000UL));
  if (n <= 0 || n >= (int)sizeof(payload)) return;  // mai JSON troncato
  _pubblica(TOPIC_CONTROLLO, payload, true);
}

void mqtt_pubblica_ack(const char* request_id,
                       const char* command,
                       const char* status,
                       const char* reason_code) {
  // reason_code null/vuoto/"ok" → JSON null; altrimenti stringa quotata
  char rc_buf[80];
  if (!reason_code || reason_code[0] == '\0' || strcmp(reason_code, "ok") == 0) {
    strcpy(rc_buf, "null");
  } else {
    snprintf(rc_buf, sizeof(rc_buf), "\"%s\"", reason_code);
  }
  // "cmd", "event" e "profilo" sono i campi letti dalle automazioni HA esistenti
  // (ack avvio profilo, profilo completato); "command"/"reason_code" restano invariati.
  const bool is_event = status && strcmp(status, "event") == 0;
  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"schema_version\":\"%s\",\"fermenter_id\":\"%s\","
           "\"request_id\":\"%s\",\"command\":\"%s\",\"cmd\":\"%s\","
           "\"status\":\"%s\",\"reason_code\":%s,%s%s%s"
           "\"profilo\":\"%s\",\"uptime_s\":%lu}",
           SCHEMA_VERSION, DEVICE_FERMENTER_ID,
           request_id, command, command, status, rc_buf,
           is_event ? "\"event\":" : "",
           is_event ? rc_buf : "",
           is_event ? "," : "",
           _nome_profilo,
           millis() / 1000UL);
  _pubblica(TOPIC_ACK, payload, false);
}

void mqtt_pubblica_allarme(const char* tipo, uint32_t durata_minuti) {
  // Formato HASS_V6_SPEC §2.3: tipo / durata_minuti / uptime_s
  char payload[128];
  snprintf(payload, sizeof(payload),
           "{\"tipo\":\"%s\",\"durata_minuti\":%lu,\"uptime_s\":%lu}",
           tipo,
           (unsigned long)durata_minuti,
           millis() / 1000UL);
  _pubblica(TOPIC_ALARM, payload, false);
}

void mqtt_pubblica_autotune_state(const AutotuneContext& autotune, float temp_c) {
  // Formato HASS_V6_SPEC §2.4: state/phase/phase_index/phase_total/
  //   cycles_done/progress/temp/ts
  // Implementazione semplificata (single-phase): phase_index=0, phase_total=1
  const char* state_str = autotune_state_to_string(autotune.state);

  uint32_t now_ts = 0;
  time_t t;
  time(&t);
  if (t > 1000000L) now_ts = (uint32_t)t;

  const uint32_t dur_s = (autotune.last_tick_ms > autotune.start_ms)
                         ? ((autotune.last_tick_ms - autotune.start_ms) / 1000UL)
                         : 0UL;
  float progress = 0.0f;
  if (AUTOTUNE_MAX_DURATION_S > 0) {
    progress = (float)dur_s / (float)AUTOTUNE_MAX_DURATION_S * 100.0f;
    if (progress > 100.0f) progress = 100.0f;
  }

  char payload[320];
  snprintf(payload, sizeof(payload),
           "{\"state\":\"%s\","
           "\"phase\":\"heat\",\"phase_index\":0,\"phase_total\":1,"
           "\"phase_relay\":\"heat\","
           "\"cycles_done\":0,\"progress\":%.1f,"
           "\"temp\":%.2f,\"ts\":%lu}",
           state_str,
           progress,
           temp_c,
           (unsigned long)now_ts);
  _pubblica(TOPIC_AUTOTUNE_STATE, payload, false);
}

void mqtt_pubblica_autotune_result(const AutotuneContext& autotune) {
  if (!autotune.result.valid) return;

  char payload[512];
  snprintf(payload, sizeof(payload),
           "{\"schema_version\":\"%s\",\"fermenter_id\":\"%s\","
           "\"result_id\":\"%s\",\"quality_score\":%.3f,\"duration_s\":%lu,"
           "\"kp_heat\":%.3f,\"ki_heat\":%.4f,\"kd_heat\":%.3f,"
           "\"kp_cool\":%.3f,\"ki_cool\":%.4f,\"kd_cool\":%.3f}",
           SCHEMA_VERSION, DEVICE_FERMENTER_ID,
           autotune.result.result_id,
           autotune.result.quality_score,
           (unsigned long)autotune.result.duration_s,
           autotune.result.kp_heat, autotune.result.ki_heat, autotune.result.kd_heat,
           autotune.result.kp_cool, autotune.result.ki_cool, autotune.result.kd_cool);
  // retain=true: risultato persiste sul broker tra sessioni
  _pubblica(TOPIC_AUTOTUNE_RESULT, payload, true);
}

void mqtt_pubblica_log(const char* payload) {
  if (_mqtt.connected()) {
    _mqtt.publish(TOPIC_LOG, payload, false);
  }
}
