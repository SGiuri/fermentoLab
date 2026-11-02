// ============================================================
// FERMENTATORE V7 — NODO SENSORI (ESP32-S3 Super Mini)
// Legge tre DS18B20 (F1, chiller, F2) e pubblica ogni lettura su fermentazione/v7/sensori/temp/<id>.
// Recupero delle sonde come la 6.5.1 (power-cycle), più riavvii limitati del nodo; OTA via hotspot.
// ============================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "config.h"
#include "secrets.h"
#include "sonde.h"

static WiFiClientSecure g_tls;
static PubSubClient     g_mqtt(g_tls);

static String        g_mac;
static String        g_wifi_pass;
static bool          g_via_hotspot = false;
static int           g_tentativi_hotspot = 0;
static uint32_t      g_ping_seq = 0;
static uint32_t      g_temp_seq = 0;
static uint32_t      g_wifi_riconnessioni = 0;
static uint32_t      g_mqtt_riconnessioni = 0;
static unsigned long g_ts_wifi_begin_ms = 0;
static unsigned long g_ts_wifi_log_ms = 0;
static unsigned long g_ts_mqtt_tentativo_ms = 0;
static unsigned long g_ts_ping_ms = 0;
static unsigned long g_ts_led_spegni_ms = 0;
static unsigned long g_ts_tutte_ok_ms = 0;
static unsigned long g_ts_seriale_ms = 0;
static bool          g_wifi_era_connesso = false;
static bool          g_ota_avviato = false;

// Riavvii del nodo per una sonda persa: sopravvivono a ESP.restart(), non all'accensione.
static RTC_NOINIT_ATTR uint32_t g_rtc_magic;
static RTC_NOINIT_ATTR uint32_t g_rtc_riavvii;
static RTC_NOINIT_ATTR char     g_rtc_sonda_riavvio[4];
static const uint32_t RTC_MAGIC = 0x53375653UL;

// Mailbox: la callback MQTT copia solo il comando, il loop lo esegue.
static char          g_cmd_topic[64];
static char          g_cmd_payload[160];
static volatile bool g_cmd_pronto = false;

// Password WiFi per il MAC di questa scheda (stessa logica della V6). Se il MAC non è nella mappa vale la
// password dei dispositivi nuovi (WIFI_PASSWORD_NUOVI in secrets.h, facoltativa); senza, stringa vuota.
static String wifi_password_per_mac(const String& macAddress) {
  WIFI_MAC_PASSWORD_MAP
#ifdef WIFI_PASSWORD_NUOVI
  return WIFI_PASSWORD_NUOVI;
#else
  return "";
#endif
}

// MAC Wi-Fi station dagli eFuse: WiFi.macAddress() subito dopo WiFi.mode() dà zeri col core 3.x.
static String mac_wifi() {
  uint8_t m[6] = {0};
  esp_read_mac(m, ESP_MAC_WIFI_STA);
  char s[18];
  snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(s);
}

static void led_colore(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(PIN_LED_RGB, r, g, b);
}

static void led_riposo() {
  if (sonde_tutte_ok()) led_colore(0, 0, 0);
  else led_colore(LED_LUMINOSITA / 2, 0, 0);
}

static const char* causa_reset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default:                return "altro";
  }
}

static uint32_t epoch_valido() {
  const time_t t = time(nullptr);
  return t >= (time_t)EPOCH_MINIMO_VALIDO ? (uint32_t)t : 0;
}

static void watchdog_init() {
  const esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_S * 1000U,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  esp_err_t err = esp_task_wdt_reconfigure(&cfg);
  if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&cfg);
  if (err == ESP_OK) err = esp_task_wdt_add(nullptr);
  Serial.printf("[WDT] task watchdog %us: %s\n", (unsigned)WDT_TIMEOUT_S, err == ESP_OK ? "attivo" : "ERRORE");
}

// ============================================================
// Pubblicazioni
// ============================================================
static void pubblica_evento(const char* tipo, const char* sonda, const char* dettaglio) {
  if (!g_mqtt.connected()) return;
  StaticJsonDocument<256> doc;
  doc["tipo"] = tipo;
  if (sonda) doc["sonda"] = sonda;
  if (dettaglio) doc["dettaglio"] = dettaglio;
  doc["uptime_s"] = millis() / 1000UL;
  const uint32_t ts = epoch_valido();
  if (ts) doc["ts"] = ts;
  char buf[256];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  g_mqtt.publish(TOPIC_EVENTO, (const uint8_t*)buf, n, false);
}

// Ogni lettura completata: valida col valore, scartata con "t": null e la causa. Non retained: una
// lettura vecchia non deve mai arrivare al nodo attuatori come se fosse di adesso.
static void on_lettura(const Sonda& s, bool valida, float t, const char* errore) {
  if (!g_mqtt.connected()) return;
  StaticJsonDocument<160> doc;
  doc["seq"] = ++g_temp_seq;
  if (valida) {
    doc["t"] = serialized(String(t, 4));
  } else {
    doc["t"] = nullptr;
    doc["err"] = errore;
  }
  const uint32_t ts = epoch_valido();
  if (ts) doc["ts"] = ts;
  char buf[160];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  char topic[64];
  snprintf(topic, sizeof(topic), TOPIC_TEMP "%s", s.id);
  g_mqtt.publish(topic, (const uint8_t*)buf, n, false);
}

static void on_power_cycle(const Sonda& s) {
  Serial.printf("[SONDA] %s: power-cycle #%u (perdita da %lus)\n", s.id, (unsigned)s.power_cycle_tentativi,
                s.ts_persa_ms ? (millis() - s.ts_persa_ms) / 1000UL : 0UL);
  char d[32];
  snprintf(d, sizeof(d), "tentativo %u", (unsigned)s.power_cycle_tentativi);
  pubblica_evento("power_cycle", s.id, d);
}

static void ping_pubblica() {
  DynamicJsonDocument doc(1024);
  doc["seq"] = ++g_ping_seq;
  doc["uptime_s"] = millis() / 1000UL;
  doc["rssi"] = WiFi.RSSI();
  doc["rete"] = g_via_hotspot ? HOTSPOT_SSID : WIFI_SSID;
  doc["heap"] = ESP.getFreeHeap();
  doc["fw"] = FIRMWARE_VERSION;
  doc["reset"] = causa_reset();
  doc["wifi_conn"] = g_wifi_riconnessioni;
  doc["mqtt_conn"] = g_mqtt_riconnessioni;
  doc["riavvii_sonde"] = g_rtc_riavvii;
  if (g_rtc_riavvii) doc["sonda_riavvio"] = g_rtc_sonda_riavvio;
  JsonObject sonde = doc.createNestedObject("sonde");
  const unsigned long now = millis();
  for (int i = 0; i < sonde_numero(); i++) {
    const Sonda& s = sonde_get(i);
    JsonObject o = sonde.createNestedObject(s.id);
    if (s.ts_ultima_valida_ms) {
      o["t"] = serialized(String(s.ultima_valida, 4));
      o["eta_s"] = (now - s.ts_ultima_valida_ms) / 1000UL;
    } else {
      o["t"] = nullptr;
    }
    o["ok"] = s.ts_persa_ms == 0 && s.ts_ultima_valida_ms != 0;
    if (s.ultimo_errore && s.ultimo_errore[0]) o["errore"] = s.ultimo_errore;
    o["letture"] = s.diag.letture;
    o["non_valide"] = s.diag.non_valide;
    o["scollegato"] = s.diag.scollegato;
    o["poweron_85"] = s.diag.poweron_85;
    o["fuori_scala"] = s.diag.fuori_scala;
    o["power_cycle"] = s.diag.power_cycle;
  }
  char buf[1024];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  const bool ok = g_mqtt.publish(TOPIC_PING, (const uint8_t*)buf, n, false);
  Serial.printf("[PING] #%lu %s\n", (unsigned long)g_ping_seq, ok ? "inviato" : "NON inviato");
  if (ok) {
    led_colore(0, LED_LUMINOSITA, 0);
    g_ts_led_spegni_ms = millis() + 150;
  }
}

// Riepilogo sulla seriale ogni SERIALE_RIEPILOGO_MS, anche senza rete: per il banco.
static void seriale_riepilogo(unsigned long now) {
  if (now - g_ts_seriale_ms < SERIALE_RIEPILOGO_MS) return;
  g_ts_seriale_ms = now;
  Serial.print("[SONDE]");
  for (int i = 0; i < sonde_numero(); i++) {
    const Sonda& s = sonde_get(i);
    Serial.printf(" %s (DQ %u, VDD %u): ", s.id, (unsigned)s.pin_dq, (unsigned)s.pin_vdd);
    if (s.ts_ultima_valida_ms && s.ts_persa_ms == 0) {
      Serial.printf("%.4f °C", s.ultima_valida);
    } else {
      Serial.printf("%s, power-cycle %lu", s.ultimo_errore && s.ultimo_errore[0] ? s.ultimo_errore : "persa",
                    (unsigned long)s.diag.power_cycle);
    }
    Serial.print(i + 1 < sonde_numero() ? " |" : "\n");
  }
}

// ============================================================
// Comandi da HA (mailbox): ping, riavvia, power_cycle
// ============================================================
static void mqtt_callback(char* topic, byte* payload, unsigned int len) {
  if (g_cmd_pronto || strncmp(topic, TOPIC_CMD, strlen(TOPIC_CMD)) != 0) return;
  strncpy(g_cmd_topic, topic + strlen(TOPIC_CMD), sizeof(g_cmd_topic) - 1);
  g_cmd_topic[sizeof(g_cmd_topic) - 1] = '\0';
  const unsigned int n = len < sizeof(g_cmd_payload) - 1 ? len : sizeof(g_cmd_payload) - 1;
  memcpy(g_cmd_payload, payload, n);
  g_cmd_payload[n] = '\0';
  g_cmd_pronto = true;
}

static void riavvia(const char* motivo, const char* sonda) {
  Serial.printf("[SYS] Riavvio: %s\n", motivo);
  pubblica_evento("riavvio", sonda, motivo);
  if (g_mqtt.connected()) g_mqtt.publish(TOPIC_STATO, "offline", true);
  delay(300);
  ESP.restart();
}

static void comando_esegui() {
  StaticJsonDocument<192> in;
  deserializeJson(in, g_cmd_payload);  // payload vuoto o non JSON: i campi restano assenti
  if (strcmp(g_cmd_topic, "ping") == 0) {
    StaticJsonDocument<128> out;
    out["token"] = in["token"];
    out["uptime_s"] = millis() / 1000UL;
    char buf[128];
    const size_t n = serializeJson(out, buf, sizeof(buf));
    g_mqtt.publish(TOPIC_PONG, (const uint8_t*)buf, n, false);
  } else if (strcmp(g_cmd_topic, "riavvia") == 0) {
    // Per esempio per far cercare l'hotspot dell'OTA: lo si cerca solo all'avvio.
    riavvia("comando riavvia", nullptr);
  } else if (strcmp(g_cmd_topic, "power_cycle") == 0) {
    const char* id = in["sonda"] | "";
    const bool ok = sonde_power_cycle(id);
    pubblica_evento("comando_power_cycle", id, ok ? "avviato" : "sonda sconosciuta o power-cycle in corso");
  } else {
    pubblica_evento("comando_sconosciuto", nullptr, g_cmd_topic);
  }
}

// ============================================================
// Rete: WiFi (hotspot OTA prima, se visibile all'avvio), MQTT, OTA
// ============================================================
static void wifi_avvia() {
  WiFi.disconnect(false, false);
  if (g_via_hotspot) WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
  else WiFi.begin(WIFI_SSID, g_wifi_pass.c_str());
  g_ts_wifi_begin_ms = millis();
}

static void ota_avvia() {
  if (g_ota_avviato) return;
  g_ota_avviato = true;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("[OTA] Aggiornamento in corso"); });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Errore %u\n", (unsigned)e); });
  ArduinoOTA.begin();
  Serial.printf("[OTA] Pronto: %s\n", OTA_HOSTNAME);
}

static void wifi_gestisci() {
  const unsigned long now = millis();
  const bool connesso = WiFi.status() == WL_CONNECTED;

  if (connesso && !g_wifi_era_connesso) {
    Serial.printf("[WIFI] Connesso a %s, IP %s, RSSI %d dBm\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    g_wifi_riconnessioni++;
    g_tentativi_hotspot = 0;
    configTime(0, 0, NTP_SERVER);
    ota_avvia();
  }
  if (!connesso && g_wifi_era_connesso) Serial.println("[WIFI] Connessione persa");
  g_wifi_era_connesso = connesso;
  if (connesso) {
    ArduinoOTA.handle();
    return;
  }

  led_colore(0, 0, LED_LUMINOSITA);
  if (now - g_ts_wifi_log_ms >= WIFI_LOG_MS) {
    g_ts_wifi_log_ms = now;
    Serial.printf("[WIFI] Non connesso (stato %d). MAC %s, password %s\n", (int)WiFi.status(), g_mac.c_str(),
                  g_wifi_pass.length() ? "presente" : "ASSENTE: aggiungere il MAC in secrets.h");
  }
  if (now - g_ts_wifi_begin_ms < WIFI_RIPROVA_MS) return;
  // Hotspot spento dopo l'aggiornamento: dopo qualche tentativo si torna alla rete IoT.
  if (g_via_hotspot && ++g_tentativi_hotspot > HOTSPOT_TENTATIVI_MAX) {
    g_via_hotspot = false;
    Serial.printf("[WIFI] Hotspot '%s' non disponibile: ripiego su '%s'\n", HOTSPOT_SSID, WIFI_SSID);
  }
  if (g_via_hotspot || g_wifi_pass.length()) wifi_avvia();
}

static void mqtt_gestisci() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (g_mqtt.connected()) {
    g_mqtt.loop();
    return;
  }
  const unsigned long now = millis();
  if (g_ts_mqtt_tentativo_ms && now - g_ts_mqtt_tentativo_ms < MQTT_RIPROVA_MS) return;
  g_ts_mqtt_tentativo_ms = now;

  led_colore(LED_LUMINOSITA, LED_LUMINOSITA / 2, 0);
  String client_id = "ferm-v7-sensori-" + g_mac.substring(9);
  client_id.replace(":", "");
  Serial.printf("[MQTT] Connessione come %s\n", client_id.c_str());
  esp_task_wdt_reset();
  // LWT retained: HA vede "offline" ~45 s dopo una caduta.
  if (!g_mqtt.connect(client_id.c_str(), MQTT_USER, MQTT_PASS, TOPIC_STATO, 1, true, "offline")) {
    Serial.printf("[MQTT] Fallita, stato %d\n", g_mqtt.state());
    return;
  }
  g_mqtt_riconnessioni++;
  g_mqtt.publish(TOPIC_STATO, "online", true);
  g_mqtt.subscribe(TOPIC_CMD "#", 1);
  Serial.println("[MQTT] Connesso");
  led_riposo();
  g_ts_ping_ms = 0;  // primo ping subito
}

// ============================================================
// Riavvio del nodo per una sonda persa (limitato)
// ============================================================
static void riavvii_gestisci(unsigned long now) {
  if (sonde_tutte_ok()) {
    if (g_ts_tutte_ok_ms == 0) g_ts_tutte_ok_ms = now ? now : 1;
    if (g_rtc_riavvii && now - g_ts_tutte_ok_ms >= NODO_RIAVVII_RESET_MS) {
      Serial.printf("[SONDA] Sonde a posto da %lu min: contatore dei riavvii azzerato\n", NODO_RIAVVII_RESET_MS / 60000UL);
      g_rtc_riavvii = 0;
    }
    return;
  }
  g_ts_tutte_ok_ms = 0;
  const Sonda* s = sonde_persa_da(now, NODO_RIAVVIO_DOPO_MS);
  if (!s || g_rtc_riavvii >= NODO_RIAVVII_MAX) return;
  g_rtc_riavvii++;
  strncpy(g_rtc_sonda_riavvio, s->id, sizeof(g_rtc_sonda_riavvio) - 1);
  g_rtc_sonda_riavvio[sizeof(g_rtc_sonda_riavvio) - 1] = '\0';
  char motivo[64];
  snprintf(motivo, sizeof(motivo), "sonda %s persa da %lu min (riavvio %u di %u)", s->id,
           (now - s->ts_persa_ms) / 60000UL, (unsigned)g_rtc_riavvii, (unsigned)NODO_RIAVVII_MAX);
  riavvia(motivo, s->id);
}

void setup() {
  Serial.begin(115200);
  delay(1500);  // tempo alla seriale USB di agganciarsi, per vedere i primi messaggi
  led_colore(0, 0, LED_LUMINOSITA);
  watchdog_init();

  const esp_reset_reason_t rr = esp_reset_reason();
  if (g_rtc_magic != RTC_MAGIC || rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT) {
    g_rtc_magic = RTC_MAGIC;
    g_rtc_riavvii = 0;
    g_rtc_sonda_riavvio[0] = '\0';
  }
  sonde_init(on_lettura, on_power_cycle);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.setAutoReconnect(true);
  g_mac = mac_wifi();
  g_wifi_pass = wifi_password_per_mac(g_mac);
  Serial.printf("\n[BOOT] Nodo sensori V7 %s, reset %s, MAC %s, riavvii per sonda %u\n", FIRMWARE_VERSION,
                causa_reset(), g_mac.c_str(), (unsigned)g_rtc_riavvii);

  // Hotspot OTA cercato solo all'avvio (come la V6): se è visibile ha la precedenza.
  const int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == HOTSPOT_SSID) {
      g_via_hotspot = true;
      break;
    }
  }
  WiFi.scanDelete();
  Serial.printf("[WIFI] %s\n", g_via_hotspot ? "Hotspot OTA trovato: mi collego a quello" : "Hotspot OTA assente: rete IoT");

  g_tls.setInsecure();  // come la V6: TLS senza verifica del certificato
  g_tls.setHandshakeTimeout(10);
  g_mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  g_mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  g_mqtt.setSocketTimeout(5);
  g_mqtt.setBufferSize(MQTT_BUFFER_SIZE);
  g_mqtt.setCallback(mqtt_callback);

  if (g_via_hotspot || g_wifi_pass.length()) wifi_avvia();
}

void loop() {
  esp_task_wdt_reset();
  const unsigned long now = millis();
  sonde_aggiorna(now);
  wifi_gestisci();
  mqtt_gestisci();

  if (g_cmd_pronto) {
    if (g_mqtt.connected()) comando_esegui();
    g_cmd_pronto = false;
  }
  if (g_mqtt.connected() && (g_ts_ping_ms == 0 || now - g_ts_ping_ms >= PING_INTERVALLO_MS)) {
    g_ts_ping_ms = now;
    ping_pubblica();
  }
  if (g_ts_led_spegni_ms && (long)(now - g_ts_led_spegni_ms) >= 0) {
    g_ts_led_spegni_ms = 0;
    led_riposo();
  }
  riavvii_gestisci(now);
  seriale_riepilogo(now);
  delay(5);
}
