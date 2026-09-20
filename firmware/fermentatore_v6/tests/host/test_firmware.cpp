// Test host del firmware fermentatore v6.
//
// Compila i sorgenti reali dello sketch con mock di Arduino/ESP32 e simula tempo,
// relè, sensore DS18B20, NVS e broker MQTT. Ogni scenario gira in un processo
// separato (le variabili statiche del firmware non si possono azzerare):
//   test_firmware.exe <scenario>
// Uscita 0 = superato, 1 = fallito.

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "mock_misc.h"
#include "config.h"
#include "display.h"
#include "uscite_rele.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

void setup();
void loop();
extern DisplayData g_mock_display;

// ============================================================
// Mini framework
// ============================================================
static int g_fallimenti = 0;

#define VERIFICA(cond, ...)                                      \
  do {                                                           \
    if (!(cond)) {                                               \
      g_fallimenti++;                                            \
      printf("  FALLITO (riga %d): %s -> ", __LINE__, #cond);    \
      printf(__VA_ARGS__);                                       \
      printf("\n");                                              \
    }                                                            \
  } while (0)

static const int64_t EPOCH_TEST = 1789000000;  // settembre 2026

// ============================================================
// Impianto termico simulato (fermentatore ~20 L)
// ============================================================
struct Impianto {
  double temp = 20.0;
  double ambiente = 24.0;
  double riscaldo_c_h = 1.2;   // fascia riscaldante 30 W
  double raffreddo_c_h = 3.0;  // pompa glicole
  double k_amb_h = 0.05;       // costante di tempo verso ambiente ~20 h
};
static Impianto g_imp;
static bool g_modello_termico = false;
static bool g_sensore_guasto = false;

static bool rele_heat() { return mock_livello_pin(PIN_RISCALDATORE) == RELAY_ON; }
static bool rele_cool() { return mock_livello_pin(PIN_REFRIGERATORE) == RELAY_ON; }

static double g_imp_temp_debug = 0.0;

struct OsservatoreRele {
  bool stato = false;
  unsigned long ts_cambio = 0;
  bool inizializzato = false;
  bool prima = true;  // la prima commutazione dopo l'inizializzazione ha durata ignota
  int commutazioni = 0;
  int violazioni_min_on = 0;
  int violazioni_min_off = 0;
  unsigned long ms_acceso = 0;

  void osserva(bool acceso, unsigned long now, unsigned long passo) {
    if (acceso) ms_acceso += passo;
    if (!inizializzato) {
      inizializzato = true;
      stato = acceso;
      ts_cambio = now;
      return;
    }
    if (acceso == stato) return;
    const unsigned long durata = now - ts_cambio;
    if (prima) {
      prima = false;
      stato = acceso;
      ts_cambio = now;
      commutazioni++;
      return;
    }
    const bool sicurezza = std::fabs(g_imp_temp_debug - 19.0) >= SICUREZZA_BANDA_C - 0.05;
    if (stato && durata + passo < RELE_MIN_ON_MS && !sicurezza) {
      violazioni_min_on++;
      if (violazioni_min_on <= 3) printf("  [violazione min ON] t=%lu s durata=%lu ms T=%.3f\n", now / 1000UL, durata, g_imp_temp_debug);
    }
    if (!stato && durata + passo < RELE_MIN_OFF_MS) violazioni_min_off++;
    stato = acceso;
    ts_cambio = now;
    commutazioni++;
  }
};

struct Statistiche {
  OsservatoreRele heat;
  OsservatoreRele cool;
  int simultanei = 0;
  double temp_min = 1e9;
  double temp_max = -1e9;
  bool registra_temp = false;
};
static Statistiche g_stat;

static double quantizza(double t) {
  return std::round(t / 0.0625) * 0.0625;  // DS18B20 a 12 bit
}

static void avanza(unsigned long durata_ms, unsigned long passo_ms = 250) {
  const unsigned long fine = g_mock_ms + durata_ms;
  while (g_mock_ms < fine) {
    g_mock_ms += passo_ms;
    if (g_modello_termico) {
      const double h = passo_ms / 3600000.0;
      double dT = (g_imp.ambiente - g_imp.temp) * g_imp.k_amb_h * h;
      if (rele_heat()) dT += g_imp.riscaldo_c_h * h;
      if (rele_cool()) dT -= g_imp.raffreddo_c_h * h;
      g_imp.temp += dT;
    }
    if (g_sensore_guasto) {
      g_mock_ds_temp = (float)DEVICE_DISCONNECTED_C;
    } else if (g_modello_termico) {
      g_mock_ds_temp = (float)quantizza(g_imp.temp);
    }

    loop();
    g_imp_temp_debug = g_imp.temp;

    g_stat.heat.osserva(rele_heat(), g_mock_ms, passo_ms);
    g_stat.cool.osserva(rele_cool(), g_mock_ms, passo_ms);
    if (rele_heat() && rele_cool()) g_stat.simultanei++;
    if (g_stat.registra_temp) {
      if (g_imp.temp < g_stat.temp_min) g_stat.temp_min = g_imp.temp;
      if (g_imp.temp > g_stat.temp_max) g_stat.temp_max = g_imp.temp;
    }
  }
}

// ============================================================
// Helper MQTT
// ============================================================
static std::string topic_cmd(const char* nome) {
  return std::string(TOPIC_PREFIX_HA "/") + nome;
}

static int64_t epoch_adesso() { return (int64_t)mock_time(nullptr); }

static std::string comando(const std::string& id, int64_t ts, const std::string& extra = "") {
  std::string s = "{\"schema_version\":\"1.0\",\"request_id\":\"" + id + "\",\"ts\":" + std::to_string(ts);
  if (!extra.empty()) s += "," + extra;
  return s + "}";
}

static std::string profilo_json(const std::string& nome, const std::string& steps, const std::string& extra = "") {
  std::string s = "\"profilo\":\"" + nome + "\",\"steps\":[" + steps + "]";
  if (!extra.empty()) s += "," + extra;
  return s;
}

static void invia(const char* nome_cmd, const std::string& payload) {
  mock_mqtt_consegna(topic_cmd(nome_cmd), payload);
}

// Ultimo ack con il request_id indicato (documento vuoto se assente).
static DynamicJsonDocument ultimo_ack(const std::string& request_id) {
  DynamicJsonDocument doc(1024);
  for (auto it = g_mock_pubblicazioni.rbegin(); it != g_mock_pubblicazioni.rend(); ++it) {
    if (it->topic != TOPIC_ACK) continue;
    DynamicJsonDocument d(1024);
    if (deserializeJson(d, it->payload)) continue;
    if (request_id == (d["request_id"] | "")) return d;
  }
  return doc;
}

static int conta_ack(const std::string& request_id) {
  int n = 0;
  for (const auto& p : g_mock_pubblicazioni) {
    if (p.topic == TOPIC_ACK && p.payload.find("\"request_id\":\"" + request_id + "\"") != std::string::npos) n++;
  }
  return n;
}

static DynamicJsonDocument ultimo_stato() {
  DynamicJsonDocument doc(2048);
  for (auto it = g_mock_pubblicazioni.rbegin(); it != g_mock_pubblicazioni.rend(); ++it) {
    if (it->topic == TOPIC_STATE) {
      const DeserializationError err = deserializeJson(doc, it->payload);
      VERIFICA(!err, "payload esp/state non è JSON valido: %s", it->payload.c_str());
      return doc;
    }
  }
  return doc;
}

static DynamicJsonDocument ultimo_controllo() {
  DynamicJsonDocument doc(2048);
  for (auto it = g_mock_pubblicazioni.rbegin(); it != g_mock_pubblicazioni.rend(); ++it) {
    if (it->topic == TOPIC_CONTROLLO) {
      const DeserializationError err = deserializeJson(doc, it->payload);
      VERIFICA(!err, "payload esp/controllo non è JSON valido: %s", it->payload.c_str());
      return doc;
    }
  }
  return doc;
}

static bool allarme_pubblicato(const char* tipo) {
  const std::string cerca = std::string("\"tipo\":\"") + tipo + "\"";
  for (const auto& p : g_mock_pubblicazioni) {
    if (p.topic == TOPIC_ALARM && p.payload.find(cerca) != std::string::npos) return true;
  }
  return false;
}

static std::string ultimo_availability() {
  for (auto it = g_mock_pubblicazioni.rbegin(); it != g_mock_pubblicazioni.rend(); ++it) {
    if (it->topic == TOPIC_AVAILABILITY) return it->payload + (it->retain ? "|retain" : "|no-retain");
  }
  return "";
}

// Boot con WiFi e broker disponibili, orologio sincronizzato.
static void avvia_firmware(bool orologio = true) {
  if (orologio) mock_imposta_epoch(EPOCH_TEST);
  WiFi.stato = WL_DISCONNECTED;
  g_mock_broker_raggiungibile = true;
  setup();
}

// Porta il firmware in RUNNING con un profilo a temperatura costante.
static void avvia_profilo_costante(float temp, const std::string& id = "prof-1") {
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  const std::string step = "{\"step\":\"Primaria\",\"temp\":" + std::to_string(temp) + ",\"duration_hours\":500}";
  invia("profile_set", comando(id, epoch_adesso(), profilo_json("Test", step)));
  const auto ack = ultimo_ack(id);
  VERIFICA(std::string(ack["status"] | "") == "ok", "profile_set non accettato: %s", (const char*)(ack["reason_code"] | "null"));
}

// ============================================================
// Scenari
// ============================================================

// Stadio uscite isolato: tempi minimi, divieti di sicurezza, interlock, finestra duty.
static void scenario_uscite() {
  g_mock_ms = 1000;
  uscite_init();
  VERIFICA(!rele_heat() && !rele_cool(), "relè accesi dopo uscite_init");

  const RichiestaUscite caldo = {true, false, 0.0f, 0.0f};
  const RichiestaUscite freddo = {false, true, 0.0f, 0.0f};
  const RichiestaUscite niente = {false, false, 0.0f, 0.0f};
  unsigned long t = 1000;

  uscite_aggiorna(caldo, false, 18.0f, 20.0f, t + RELE_MIN_OFF_MS - 1000);
  VERIFICA(!uscite_riscaldatore_acceso(), "acceso prima del tempo minimo OFF dal boot");
  t += RELE_MIN_OFF_MS;
  uscite_aggiorna(caldo, false, 18.0f, 20.0f, t);
  VERIFICA(uscite_riscaldatore_acceso(), "non acceso dopo il tempo minimo OFF");

  uscite_aggiorna(niente, false, 18.0f, 20.0f, t + 5000);
  VERIFICA(uscite_riscaldatore_acceso(), "spento prima del tempo minimo ON");
  uscite_aggiorna(niente, true, 18.0f, 20.0f, t + 6000);
  VERIFICA(!uscite_riscaldatore_acceso(), "spegnimento forzato non immediato");

  // Divieto di sicurezza: sopra setpoint + banda il riscaldatore si spegne subito
  t += 6000 + RELE_MIN_OFF_MS;
  uscite_aggiorna(caldo, false, 18.0f, 20.0f, t);
  VERIFICA(uscite_riscaldatore_acceso(), "riaccensione dopo min OFF fallita");
  uscite_aggiorna(caldo, false, 20.0f + SICUREZZA_BANDA_C + 0.1f, 20.0f, t + 1000);
  VERIFICA(!uscite_riscaldatore_acceso(), "riscaldatore acceso sopra setpoint+banda");
  uscite_aggiorna(caldo, false, SICUREZZA_TEMP_MAX_C + 0.5f, 60.0f, t + 60000);
  VERIFICA(!uscite_riscaldatore_acceso(), "riscaldatore acceso sopra il limite assoluto");
  uscite_aggiorna(freddo, false, SICUREZZA_TEMP_MIN_C - 0.5f, -5.0f, t + 120000);
  VERIFICA(!uscite_refrigeratore_acceso(), "refrigeratore acceso sotto il limite assoluto");

  // Cambio direzione: dopo lo spegnimento del caldo il freddo attende RELE_CAMBIO_DIREZIONE_MS
  g_mock_ms = 500000;
  uscite_init();
  t = g_mock_ms + RELE_CAMBIO_DIREZIONE_MS + 1000;
  uscite_aggiorna(caldo, false, 19.0f, 20.0f, t);
  VERIFICA(uscite_riscaldatore_acceso(), "caldo non acceso (setup cambio direzione)");
  t += RELE_MIN_ON_MS;
  uscite_aggiorna(freddo, false, 19.5f, 19.4f, t);
  VERIFICA(!uscite_riscaldatore_acceso(), "caldo non spento alla richiesta freddo");
  uscite_aggiorna(freddo, false, 19.5f, 19.4f, t + RELE_CAMBIO_DIREZIONE_MS - 5000);
  VERIFICA(!uscite_refrigeratore_acceso(), "freddo acceso prima dell'attesa cambio direzione");
  uscite_aggiorna(freddo, false, 19.5f, 19.4f, t + RELE_CAMBIO_DIREZIONE_MS);
  VERIFICA(uscite_refrigeratore_acceso(), "freddo non acceso dopo l'attesa");

  // Interlock: richiesta contemporanea → entrambi spenti
  const RichiestaUscite entrambi = {true, true, 0.0f, 0.0f};
  uscite_aggiorna(entrambi, false, 20.0f, 20.0f, t + RELE_CAMBIO_DIREZIONE_MS + 1000);
  VERIFICA(!uscite_riscaldatore_acceso() && !uscite_refrigeratore_acceso(), "interlock caldo/freddo violato");

  // Finestra duty: 50% → metà finestra; sotto l'impulso minimo → mai; OFF residuo
  // sotto il minimo → sempre; NaN → mai
  auto misura_on = [](float duty, unsigned long inizio) {
    g_mock_ms = inizio;
    uscite_init();
    const RichiestaUscite r = {true, false, duty, 0.0f};
    unsigned long on = 0;
    const unsigned long start = inizio + RELE_MIN_OFF_MS + PID_DUTY_WINDOW_MS - ((inizio + RELE_MIN_OFF_MS) % PID_DUTY_WINDOW_MS);
    for (unsigned long now = inizio; now < start + 5 * PID_DUTY_WINDOW_MS; now += 100) {
      uscite_aggiorna(r, false, 18.0f, 20.0f, now);
      if (now >= start && uscite_riscaldatore_acceso()) on += 100;
    }
    return (double)on / (5.0 * PID_DUTY_WINDOW_MS) * 100.0;
  };
  const double d50 = misura_on(50.0f, 10000000UL);
  VERIFICA(std::fabs(d50 - 50.0) < 2.0, "duty 50%% misurato %.1f%%", d50);
  const float duty_basso = 0.5f * 100.0f * RELE_IMPULSO_MIN_MS / PID_DUTY_WINDOW_MS;
  const double d_basso = misura_on(duty_basso, 20000000UL);
  VERIFICA(d_basso == 0.0, "duty %.1f%% (impulso < minimo) misurato %.1f%%", duty_basso, d_basso);
  const float duty_alto = 100.0f - 0.5f * 100.0f * RELE_MIN_OFF_MS / PID_DUTY_WINDOW_MS;
  const double d_alto = misura_on(duty_alto, 30000000UL);
  VERIFICA(d_alto == 100.0, "duty %.1f%% (OFF < minimo) misurato %.1f%%", duty_alto, d_alto);
  const double dnan = misura_on(NAN, 40000000UL);
  VERIFICA(dnan == 0.0, "duty NaN misurato %.1f%%", dnan);
}

// Sensore scollegato durante il riscaldamento: relè OFF entro SENSOR_MAX_ETA_MS, mai più accesi,
// allarmi pubblicati e riavvio controllato.
static void scenario_sensore_guasto() {
  g_modello_termico = true;
  g_imp.temp = 17.0;
  g_imp.ambiente = 15.0;
  avvia_firmware();
  avvia_profilo_costante(20.0f);
  avanza(5 * 60 * 1000UL);
  VERIFICA(rele_heat(), "riscaldatore non acceso con T=%.2f e setpoint 20", g_imp.temp);

  g_sensore_guasto = true;
  const unsigned long t_guasto = g_mock_ms;
  unsigned long t_spento = 0;
  bool riacceso_dopo_spegnimento = false;
  bool riavviato = false;
  try {
    while (g_mock_ms - t_guasto < 15 * 60 * 1000UL) {
      avanza(250);
      if (t_spento == 0 && !rele_heat()) t_spento = g_mock_ms;
      if (t_spento != 0 && (rele_heat() || rele_cool())) riacceso_dopo_spegnimento = true;
    }
  } catch (const RiavvioSimulato&) {
    riavviato = true;
  }

  const unsigned long ritardo = t_spento - t_guasto;
  printf("  spegnimento dopo %lu ms dal guasto, riavvio dopo %lu s\n", ritardo, (g_mock_ms - t_guasto) / 1000UL);
  VERIFICA(t_spento != 0, "riscaldatore mai spento con sensore guasto");
  VERIFICA(ritardo <= SENSOR_MAX_ETA_MS + SENSOR_READ_INTERVAL_MS, "spegnimento troppo lento: %lu ms", ritardo);
  VERIFICA(!riacceso_dopo_spegnimento, "relè riaccesi con sensore guasto");
  VERIFICA(allarme_pubblicato("sensore_instabile"), "allarme sensore_instabile non pubblicato");
  VERIFICA(allarme_pubblicato("sensore_offline"), "allarme sensore_offline non pubblicato");
  VERIFICA(allarme_pubblicato("riavvio_in_corso"), "allarme riavvio_in_corso non pubblicato");
  VERIFICA(riavviato, "riavvio controllato non avvenuto");
  const unsigned long min_riavvio = SENSOR_ALARM_TIMEOUT_MINUTES * 60000UL + SENSOR_REBOOT_DELAY_MS;
  VERIFICA(g_mock_ms - t_guasto >= min_riavvio && g_mock_ms - t_guasto <= min_riavvio + 60000UL,
           "riavvio fuori finestra attesa: %lu s", (g_mock_ms - t_guasto) / 1000UL);
  VERIFICA(ultimo_availability() == "offline|retain", "availability prima del riavvio: %s", ultimo_availability().c_str());
  const auto st = ultimo_stato();
  VERIFICA(!(st["sensore_ok"] | true), "stato pubblicato con sensore_ok=true durante il guasto");
  VERIFICA(!(st["riscaldatore"] | true), "stato pubblicato con riscaldatore acceso durante il guasto");
  VERIFICA(g_mock_wdt_loop_iscritto, "loop non iscritto al task watchdog");
  VERIFICA(g_mock_wdt_timeout_ms == WDT_TIMEOUT_S * 1000UL, "timeout watchdog %u", g_mock_wdt_timeout_ms);
  VERIFICA(g_mock_wdt_gap_max_ms < WDT_TIMEOUT_S * 1000UL / 2, "gap massimo tra reset watchdog %lu ms", g_mock_wdt_gap_max_ms);
}

// Guasto breve: il controllo riprende da solo, senza STOP e senza comandi da HA.
static void scenario_sensore_recupero() {
  g_modello_termico = true;
  g_imp.temp = 17.0;
  g_imp.ambiente = 15.0;
  avvia_firmware();
  avvia_profilo_costante(20.0f);
  avanza(5 * 60 * 1000UL);
  VERIFICA(rele_heat(), "riscaldatore non acceso prima del guasto");

  g_sensore_guasto = true;
  avanza(60 * 1000UL);
  VERIFICA(!rele_heat(), "riscaldatore acceso durante il guasto");
  g_sensore_guasto = false;
  const unsigned long t_rec = g_mock_ms;
  while (!rele_heat() && g_mock_ms - t_rec < 5 * 60 * 1000UL) avanza(250);
  printf("  controllo ripreso dopo %lu s dal recupero\n", (g_mock_ms - t_rec) / 1000UL);
  VERIFICA(rele_heat(), "controllo non ripreso dopo il recupero del sensore");
  VERIFICA(g_mock_ms - t_rec <= SENSOR_VALID_STREAK_MIN * SENSOR_READ_INTERVAL_MS + RELE_MIN_OFF_MS + 5000UL,
           "ripresa troppo lenta");
  VERIFICA(allarme_pubblicato("recovery_sensore"), "recovery_sensore non pubblicato");
  avanza(STATUS_INTERVAL_MS);
  const auto st = ultimo_stato();
  VERIFICA(std::string(st["stato_fsm"] | "") == "RUNNING", "stato_fsm dopo recupero: %s", (const char*)(st["stato_fsm"] | ""));
  VERIFICA(st["sensore_ok"] | false, "sensore_ok false dopo recupero");
}

// Una singola lettura errata non deve interrompere il controllo.
static void scenario_glitch_singolo() {
  g_modello_termico = true;
  g_imp.temp = 17.0;
  g_imp.ambiente = 15.0;
  avvia_firmware();
  avvia_profilo_costante(20.0f);
  avanza(5 * 60 * 1000UL);
  VERIFICA(rele_heat(), "riscaldatore non acceso prima del glitch");
  const int commutazioni = g_stat.heat.commutazioni;
  g_sensore_guasto = true;
  avanza(SENSOR_READ_INTERVAL_MS);
  g_sensore_guasto = false;
  avanza(60 * 1000UL);
  VERIFICA(rele_heat() && g_stat.heat.commutazioni == commutazioni, "glitch singolo ha spento il riscaldatore");
  VERIFICA(!allarme_pubblicato("sensore_instabile"), "allarme spurio per glitch singolo");
}

// Comandi retained, vecchi, duplicati, STOP sempre eseguito, cambio modo durante RUNNING.
static void scenario_comandi() {
  avvia_firmware();
  g_mock_ds_temp = 19.0f;

  // Dentro la finestra retained: senza ts recente viene ignorato
  avanza(1000);
  const std::string step = "{\"step\":\"P\",\"temp\":19,\"duration_hours\":100}";
  invia("profile_set", comando("ret-1", 0, profilo_json("Vecchio", step)));
  VERIFICA(std::string(ultimo_ack("ret-1")["reason_code"] | "") == "possible_retained_ignored", "retained non ignorato");
  avanza(1000);
  VERIFICA(std::string(g_mock_display.stato_fsm) != "RUNNING", "avviato da un comando retained");

  // Dentro la finestra ma con ts recente: eseguito
  invia("profile_set", comando("fresh-1", epoch_adesso(), profilo_json("Nuovo", step)));
  VERIFICA(std::string(ultimo_ack("fresh-1")["status"] | "") == "ok", "comando recente rifiutato nella finestra");
  invia("stop", comando("stop-fresh", epoch_adesso()));

  avanza(CMD_FINESTRA_RETAINED_MS + 1000);

  // Fuori finestra: ts assente accettato (script HA con id fissi)
  invia("start", comando("f1-riprendi", 0));
  VERIFICA(std::string(ultimo_ack("f1-riprendi")["status"] | "") == "ok", "start senza ts rifiutato");
  invia("stop", comando("f1-stop", 0));
  invia("stop", comando("f1-stop", 0));
  VERIFICA(conta_ack("f1-stop") == 2, "stop ripetuto: %d ack", conta_ack("f1-stop"));
  VERIFICA(ultimo_ack("f1-stop")["reason_code"].isNull(), "secondo stop non eseguito: %s",
           (const char*)(ultimo_ack("f1-stop")["reason_code"] | ""));

  // Comando vecchio
  invia("start", comando("old-1", epoch_adesso() - 3600));
  VERIFICA(std::string(ultimo_ack("old-1")["reason_code"] | "") == "stale_command", "comando vecchio accettato");

  // Duplicato (stesso request_id e ts)
  const int64_t ts = epoch_adesso();
  invia("start", comando("dup-1", ts));
  invia("start", comando("dup-1", ts));
  VERIFICA(std::string(ultimo_ack("dup-1")["reason_code"] | "") == "duplicate_ignored", "duplicato non riconosciuto");

  // RUNNING: cambio modo rifiutato, stesso modo accettato (reinvio automazioni HA)
  invia("control_set", comando("cs-pi", epoch_adesso(), "\"control_mode\":\"PI\",\"params\":{}"));
  VERIFICA(std::string(ultimo_ack("cs-pi")["reason_code"] | "") == "mode_change_requires_stopped_state", "cambio modo in RUNNING accettato");
  invia("control_set", comando("cs-same", epoch_adesso(), "\"control_mode\":\"on_off_hyst_sym\",\"params\":{}"));
  VERIFICA(std::string(ultimo_ack("cs-same")["status"] | "") == "ok", "stesso modo in RUNNING rifiutato");
  invia("control_set", comando("cs-nan", epoch_adesso(),
        "\"control_mode\":\"on_off_hyst_sym\",\"params\":{\"kp_heat\":-1,\"ki_heat\":0,\"kd_heat\":0,\"kp_cool\":1,\"ki_cool\":0,\"kd_cool\":0}"));
  VERIFICA(std::string(ultimo_ack("cs-nan")["reason_code"] | "") == "invalid_pid_params", "guadagni negativi accettati");

  // Autotune disabilitato
  invia("autotune_start", comando("at-1", epoch_adesso()));
  VERIFICA(std::string(ultimo_ack("at-1")["reason_code"] | "") == "autotune_disabled", "autotune non disabilitato");

  // Riconnessione: LWT retained, online retained, retained riconsegnato ignorato
  invia("stop", comando("stop-2", epoch_adesso()));
  VERIFICA(g_mock_lwt_topic == TOPIC_AVAILABILITY && g_mock_lwt_payload == PAYLOAD_OFFLINE && g_mock_lwt_retain,
           "LWT errato: %s %s", g_mock_lwt_topic.c_str(), g_mock_lwt_payload.c_str());
  mock_mqtt_disconnetti();
  VERIFICA(ultimo_availability() == "offline|retain", "LWT non pubblicato: %s", ultimo_availability().c_str());
  const unsigned long t_disc = g_mock_ms;
  while (!g_mock_mqtt_connesso && g_mock_ms - t_disc < MQTT_CONNECT_TIMEOUT_MS + 5000UL) avanza(250);
  VERIFICA(ultimo_availability() == "online|retain", "online non ripubblicato: %s", ultimo_availability().c_str());
  invia("start", comando("f1-start-retained", 0));
  VERIFICA(std::string(ultimo_ack("f1-start-retained")["reason_code"] | "") == "possible_retained_ignored",
           "start retained eseguito alla riconnessione");
}

// avvio_epoch = 0: il profilo parte adesso e avanza (prima restava al primo step).
static void scenario_profilo_avvio_zero() {
  avvia_firmware();
  g_mock_ds_temp = 20.0f;
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  const std::string steps = "{\"step\":\"Fermentazione\",\"temp\":22.0,\"duration_hours\":1},"
                            "{\"step\":\"Cold crash\",\"temp\":3.0,\"duration_hours\":1}";
  invia("profile_set", comando("p0", epoch_adesso(), profilo_json("Lager \\\"test\\\"", steps, "\"avvio_epoch\":0")));
  VERIFICA(std::string(ultimo_ack("p0")["status"] | "") == "ok", "profilo rifiutato: %s", (const char*)(ultimo_ack("p0")["reason_code"] | ""));

  avanza(10 * 60 * 1000UL, 1000);
  auto st = ultimo_stato();
  VERIFICA(std::fabs((st["setpoint"] | 0.0f) - 22.0f) < 0.01f, "setpoint iniziale %.2f", (float)(st["setpoint"] | 0.0f));
  VERIFICA(std::string(st["profilo"] | "").find('"') == std::string::npos, "virgolette nel nome non sanificate");

  avanza(80 * 60 * 1000UL, 1000);
  st = ultimo_stato();
  VERIFICA(std::fabs((st["setpoint"] | 0.0f) - 3.0f) < 0.01f, "dopo 1.5 h setpoint %.2f (atteso 3.0)", (float)(st["setpoint"] | 0.0f));
  VERIFICA((st["step_index"] | -1) == 1, "step_index %d", (int)(st["step_index"] | -1));

  avanza(40 * 60 * 1000UL, 1000);
  st = ultimo_stato();
  VERIFICA(std::string(st["stato_fsm"] | "") == "HOLDING", "stato a fine profilo: %s", (const char*)(st["stato_fsm"] | ""));
  bool evento = false;
  for (const auto& p : g_mock_pubblicazioni) {
    if (p.topic != TOPIC_ACK) continue;
    DynamicJsonDocument d(1024);
    if (deserializeJson(d, p.payload)) continue;
    if (std::string(d["event"] | "") == "profilo_completato" && std::string(d["cmd"] | "") == "profile_complete" &&
        std::string(d["profilo"] | "").find("Lager") == 0) {
      evento = true;
    }
  }
  VERIFICA(evento, "ack profilo_completato senza i campi event/cmd/profilo letti da HA");

  // --- Dalla 6.4.0: nessuna dipendenza da NTP per l'esecuzione del profilo ---
  // Con l'orologio desincronizzato per tutta la durata, il profilo si completa comunque
  // esattamente secondo il proprio programma (2 h), usando solo l'uptime. Fino alla 6.3.0 la
  // posizione dipendeva da un epoch assoluto: senza NTP il profilo restava fermo, poi saltava
  // avanti tutto insieme alla prima sincronizzazione (vedi docs/11_TIMING_PROFILO.md).
  invia("stop", comando("stop-x", epoch_adesso()));
  mock_desincronizza_orologio();
  invia("profile_set", comando("p-nontp", 0, profilo_json("Senza NTP", steps, "\"avvio_epoch\":0")));
  VERIFICA(std::string(ultimo_ack("p-nontp")["status"] | "") == "ok", "profilo senza NTP rifiutato");

  avanza(90 * 60 * 1000UL, 1000);  // 1.5 h, sempre senza NTP
  st = ultimo_stato();
  VERIFICA(std::fabs((st["setpoint"] | 0.0f) - 3.0f) < 0.01f,
           "senza NTP: setpoint %.2f dopo 1.5 h (atteso 3.0, nessuna dipendenza da NTP)", (float)(st["setpoint"] | 0.0f));
  VERIFICA((st["step_index"] | -1) == 1, "senza NTP: step_index %d", (int)(st["step_index"] | -1));

  // La sincronizzazione tardiva del NTP non deve cambiare nulla: nessun salto del setpoint.
  const float sp_prima_sync = st["setpoint"] | 0.0f;
  mock_imposta_epoch(EPOCH_TEST + 100000);
  avanza(2000);
  st = ultimo_stato();
  VERIFICA(std::fabs((float)(st["setpoint"] | 0.0f) - sp_prima_sync) < 0.01f,
           "la sincronizzazione NTP ha spostato il setpoint: %.2f -> %.2f", sp_prima_sync, (float)(st["setpoint"] | 0.0f));

  avanza(40 * 60 * 1000UL, 1000);  // fino a completamento (2 h totali)
  st = ultimo_stato();
  VERIFICA(std::string(st["stato_fsm"] | "") == "HOLDING", "senza NTP: stato a fine profilo %s",
           (const char*)(st["stato_fsm"] | ""));
}

// Blackout durante una rampa: il progresso persistito riprende esattamente da dove era, non
// dall'ora reale trascorsa nel frattempo. Due processi separati come scenario_persistenza_*.
static const char* FILE_NVS_PAUSA = "nvs_pausa_blackout.bin";
static void scenario_profilo_pausa_blackout_a() {
  g_modello_termico = false;
  avvia_firmware();
  g_mock_ds_temp = 20.0f;
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  // Step iniziale breve a 20 °C poi rampa di 4 h verso 4 °C (-4 °C/h): dopo 1 h reale ~16 °C.
  const std::string step = "{\"step\":\"Iniziale\",\"temp\":20.0,\"duration_hours\":0.003},"
                           "{\"step\":\"Discesa\",\"temp\":4.0,\"ramp_hours\":4}";
  invia("profile_set", comando("pb", epoch_adesso(), profilo_json("Blackout", step)));
  VERIFICA(std::string(ultimo_ack("pb")["status"] | "") == "ok", "profilo rifiutato");
  avanza(3600 * 1000UL, 1000);  // 1 h di rampa realmente eseguita
  const auto st_prima = ultimo_stato();
  VERIFICA(std::fabs((st_prima["setpoint"] | 0.0f) - 16.0f) < 0.05f,
           "setpoint prima del blackout: %.2f (atteso ~16.0 dopo 1/4 di rampa)", (float)(st_prima["setpoint"] | 0.0f));
  mock_nvs_salva_su_file(FILE_NVS_PAUSA);
}
static void scenario_profilo_pausa_blackout_b() {
  mock_nvs_carica_da_file(FILE_NVS_PAUSA);
  g_mock_reset_reason = ESP_RST_POWERON;
  g_mock_ms = 0;
  // L'orologio di rete, quando torna, indica che sono passate 5 ore reali di blackout: con la
  // vecchia logica a epoch assoluto il profilo avrebbe dovuto "recuperare" tutto quel tempo,
  // saltando alla fine della rampa (4.0 °C). Il checkpoint su NVS è periodico (ogni
  // NVS_SAVE_INTERVAL_MS): il valore ripristinato può essere fino a un intervallo indietro
  // rispetto a quando è stato salvato "a mano" con mock_nvs_salva_su_file, per questo la verifica
  // sotto usa una soglia larga (< 3.0 °C, molto meno dei 12 °C di un salto a fine rampa) invece di
  // un valore assoluto.
  mock_imposta_epoch(EPOCH_TEST + 5 * 3600);
  avvia_firmware(false);  // orologio non sincronizzato al boot, come dopo un vero blackout
  g_mock_ds_temp = 20.0f;
  avanza(20000);  // solo il tempo di ripartire; NTP non ancora tornato nel mock
  auto st = ultimo_stato();
  const float sp_ripresa = st["setpoint"] | 0.0f;
  VERIFICA(sp_ripresa > 13.0f && sp_ripresa < 17.0f,
           "dopo il riavvio, prima di NTP: setpoint %.2f (atteso 13-17, non deve saltare a 4.0)", sp_ripresa);

  // NTP torna disponibile (con le 5 h di blackout "passate" nell'orologio): nessun salto atteso.
  mock_desincronizza_orologio();
  mock_imposta_epoch(EPOCH_TEST + 5 * 3600 + (int64_t)(g_mock_ms / 1000));
  avanza(2000);
  st = ultimo_stato();
  VERIFICA(std::fabs((float)(st["setpoint"] | 0.0f) - sp_ripresa) < 0.05f,
           "la sincronizzazione NTP tardiva ha spostato il setpoint: %.2f -> %.2f", sp_ripresa,
           (float)(st["setpoint"] | 0.0f));

  // Il profilo riprende ad avanzare normalmente da qui in poi: un'altra ora reale = altri -4 °C.
  avanza(3600 * 1000UL, 1000);
  st = ultimo_stato();
  VERIFICA(std::fabs((float)(st["setpoint"] | 0.0f) - (sp_ripresa - 4.0f)) < 0.05f,
           "dopo un'altra ora di rampa: setpoint %.2f (atteso ~%.2f)", (float)(st["setpoint"] | 0.0f),
           sp_ripresa - 4.0f);
  remove(FILE_NVS_PAUSA);
}

// Guasto del sensore durante una rampa: il profilo pianificato si ferma finché il sensore non
// torna utilizzabile (prima avanzava comunque, mentre i relè restavano spenti per sicurezza).
static void scenario_profilo_pausa_sensore() {
  avvia_firmware();
  g_mock_ds_temp = 20.0f;
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  const std::string step = "{\"step\":\"Iniziale\",\"temp\":20.0,\"duration_hours\":0.003},"
                           "{\"step\":\"Discesa\",\"temp\":4.0,\"ramp_hours\":4}";
  invia("profile_set", comando("ps", epoch_adesso(), profilo_json("PausaSensore", step)));
  VERIFICA(std::string(ultimo_ack("ps")["status"] | "") == "ok", "profilo rifiutato");
  avanza(3600 * 1000UL, 1000);  // 1 h reale
  const float sp_prima_guasto = ultimo_stato()["setpoint"] | 0.0f;

  // Il sensore si guasta per 2 minuti: abbastanza per superare SENSOR_MAX_ETA_MS (20 s) e vedere
  // il profilo in pausa, ma sotto i SENSOR_ALARM_TIMEOUT_MINUTES (5 min) che farebbero scattare
  // l'allarme e il riavvio automatico del sensore — quel percorso è già coperto da
  // scenario_sensore_guasto/scenario_sensore_recupero.
  g_sensore_guasto = true;
  g_mock_ds_temp = (float)DEVICE_DISCONNECTED_C;
  avanza(2 * 60 * 1000UL, 1000);
  auto st = ultimo_stato();
  VERIFICA(std::fabs((float)(st["setpoint"] | 0.0f) - sp_prima_guasto) < 0.05f,
           "il setpoint pianificato è avanzato durante il guasto del sensore: %.2f -> %.2f",
           sp_prima_guasto, (float)(st["setpoint"] | 0.0f));
  VERIFICA(!(bool)(st["sensore_ok"] | true), "sensore segnalato utilizzabile durante il guasto");

  // Il sensore torna valido: il profilo riprende da dove era, non da dove "dovrebbe essere ora".
  g_sensore_guasto = false;
  g_mock_ds_temp = 20.0f;
  const unsigned long t_recupero = g_mock_ms;
  while (!(bool)(ultimo_stato()["sensore_ok"] | false) && g_mock_ms - t_recupero < 5 * 60 * 1000UL) avanza(250);
  VERIFICA((bool)(ultimo_stato()["sensore_ok"] | false), "sensore non tornato utilizzabile");
  avanza(3600 * 1000UL, 1000);  // un'altra ora di rampa dopo il recupero
  st = ultimo_stato();
  VERIFICA(std::fabs((float)(st["setpoint"] | 0.0f) - (sp_prima_guasto - 4.0f)) < 0.15f,
           "dopo un'altra ora di rampa dal recupero: setpoint %.2f (atteso ~%.2f)",
           (float)(st["setpoint"] | 0.0f), sp_prima_guasto - 4.0f);
}

// Validazione profilo: niente troncamenti silenziosi né temperature fuori range.
static void scenario_profilo_validazione() {
  avvia_firmware();
  g_mock_ds_temp = 20.0f;
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);

  auto steps_n = [](int n) {
    std::string s;
    for (int i = 0; i < n; i++) {
      if (i) s += ",";
      s += "{\"step\":\"Step numero " + std::to_string(i) + " con un nome piuttosto lungo xx\",\"temp\":18.5,\"duration_hours\":12}";
    }
    return s;
  };
  const std::string p20 = comando("v20", epoch_adesso(), profilo_json("Venti", steps_n(20)));
  VERIFICA(p20.size() > 1500, "payload di prova troppo corto");
  invia("profile_set", p20);
  VERIFICA(std::string(ultimo_ack("v20")["status"] | "") == "ok", "profilo da 20 step rifiutato: %s",
           (const char*)(ultimo_ack("v20")["reason_code"] | ""));
  invia("profile_set", comando("v21", epoch_adesso(), profilo_json("Ventuno", steps_n(21))));
  VERIFICA(std::string(ultimo_ack("v21")["reason_code"] | "") == "too_many_steps", "21 step non rifiutati");

  auto verifica_rifiuto = [](const char* id, const std::string& step, const char* motivo, const std::string& extra = "") {
    invia("profile_set", comando(id, epoch_adesso(), profilo_json("X", step, extra)));
    const std::string reason = ultimo_ack(id)["reason_code"] | "";
    VERIFICA(reason == motivo, "%s: atteso %s, ottenuto %s", id, motivo, reason.c_str());
  };
  verifica_rifiuto("t60", "{\"step\":\"A\",\"temp\":60,\"duration_hours\":1}", "step_temp_out_of_range");
  verifica_rifiuto("tneg", "{\"step\":\"A\",\"temp\":-5,\"duration_hours\":1}", "step_temp_out_of_range");
  verifica_rifiuto("tmiss", "{\"step\":\"A\",\"duration_hours\":1}", "step_temp_missing");
  verifica_rifiuto("dzero", "{\"step\":\"A\",\"temp\":18,\"duration_hours\":0}", "step_duration_invalid");
  verifica_rifiuto("dmiss", "{\"step\":\"A\",\"temp\":18}", "step_duration_missing");
  verifica_rifiuto("epoch", "{\"step\":\"A\",\"temp\":18,\"duration_hours\":1}", "invalid_avvio_epoch", "\"avvio_epoch\":12345");

  // avvia=false: profilo salvato ma controllo non avviato
  invia("stop", comando("stop-v", epoch_adesso()));
  invia("profile_set", comando("noav", epoch_adesso(),
        profilo_json("Solo salva", "{\"step\":\"A\",\"temp\":18,\"duration_hours\":1}", "\"avvia\":false")));
  VERIFICA(std::string(ultimo_ack("noav")["reason_code"] | "") == "profile_stored", "avvia=false non rispettato");
  avanza(1000);
  VERIFICA(std::string(g_mock_display.stato_fsm) != "RUNNING", "avviato nonostante avvia=false");
}

// Simulazione ad anello chiuso di 48 h: temperatura nella banda, nessuna violazione dei tempi minimi.
static void simulazione_48h(const char* modo, const std::string& params = "{}", double banda = 1.0) {
  g_modello_termico = true;
  const char* amb = getenv("SIM_AMBIENTE");  // default 24 °C: carico di raffreddamento
  g_imp.ambiente = amb ? atof(amb) : 24.0;
  g_imp.temp = g_imp.ambiente > 19.0 ? 23.0 : 15.0;
  avvia_firmware();
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  if (std::string(modo) != "on_off_hyst_sym") {
    invia("control_set", comando("cs", epoch_adesso(), std::string("\"control_mode\":\"") + modo + "\",\"params\":" + params));
    VERIFICA(std::string(ultimo_ack("cs")["status"] | "") == "ok", "control_set %s rifiutato", modo);
  }
  const std::string step = "{\"step\":\"Primaria\",\"temp\":19.0,\"duration_hours\":200}";
  invia("profile_set", comando("sim", epoch_adesso(), profilo_json("Sim", step)));

  avanza(6 * 3600 * 1000UL, 500);  // assestamento da 23 °C
  g_stat = Statistiche{};
  g_stat.registra_temp = true;
  const int scritture_nvs = g_mock_nvs_scritture;
  const size_t pubblicazioni = g_mock_pubblicazioni.size();
  avanza(48 * 3600 * 1000UL, 500);

  const int commutazioni = g_stat.heat.commutazioni + g_stat.cool.commutazioni;
  printf("  %s: T min %.2f max %.2f, commutazioni %d (%.1f/h), caldo on %.0f%%, freddo on %.0f%%, scritture NVS %d, messaggi MQTT %zu\n",
         modo, g_stat.temp_min, g_stat.temp_max, commutazioni, commutazioni / 48.0,
         100.0 * g_stat.heat.ms_acceso / (48.0 * 3600000.0),
         100.0 * g_stat.cool.ms_acceso / (48.0 * 3600000.0), g_mock_nvs_scritture - scritture_nvs,
         g_mock_pubblicazioni.size() - pubblicazioni);
  VERIFICA(g_stat.temp_min >= 19.0 - banda && g_stat.temp_max <= 19.0 + banda,
           "temperatura fuori banda ±%.1f: %.2f..%.2f", banda, g_stat.temp_min, g_stat.temp_max);
  VERIFICA(g_stat.simultanei == 0, "caldo e freddo accesi insieme %d volte", g_stat.simultanei);
  VERIFICA(g_stat.heat.violazioni_min_on + g_stat.cool.violazioni_min_on == 0, "violazioni tempo minimo ON");
  VERIFICA(g_stat.heat.violazioni_min_off + g_stat.cool.violazioni_min_off == 0, "violazioni tempo minimo OFF");
  const double max_h = (std::string(modo).rfind("on_off", 0) == 0) ? 4.0 : 13.0;
  VERIFICA(commutazioni / 48.0 <= max_h, "troppe commutazioni: %.1f/h (max %.0f)", commutazioni / 48.0, max_h);
  // 2 chiavi (ore_save, progresso_s) ogni NVS_SAVE_INTERVAL_MS: 288 voci/giorno.
  // Il firmware 6.1.19 riscriveva ogni 60 s il blob profilo intero (~1.3 KB).
  VERIFICA(g_mock_nvs_scritture - scritture_nvs <= 48 * 6 * 2 + 10, "troppe scritture NVS: %d", g_mock_nvs_scritture - scritture_nvs);
}

// WiFi assente per 2 ore: il controllo continua, poi riconnessione automatica.
static void scenario_wifi_assente() {
  g_modello_termico = true;
  g_imp.temp = 19.0;
  g_imp.ambiente = 24.0;
  avvia_firmware();
  avvia_profilo_costante(19.0f);
  avanza(2 * 3600 * 1000UL, 500);

  WiFi.stato = WL_DISCONNECTED;
  WiFi.rete_disponibile = false;
  mock_mqtt_disconnetti();
  g_stat = Statistiche{};
  g_stat.registra_temp = true;
  avanza(2 * 3600 * 1000UL, 500);
  VERIFICA(g_stat.cool.commutazioni > 0, "nessuna regolazione durante l'assenza WiFi");
  VERIFICA(g_stat.temp_max <= 20.0, "temperatura salita a %.2f senza WiFi", g_stat.temp_max);

  WiFi.rete_disponibile = true;
  const unsigned long t0 = g_mock_ms;
  while (!g_mock_mqtt_connesso && g_mock_ms - t0 < 5 * 60 * 1000UL) avanza(500);
  printf("  riconnessione dopo %lu s\n", (g_mock_ms - t0) / 1000UL);
  VERIFICA(g_mock_mqtt_connesso, "nessuna riconnessione entro 5 minuti");
  VERIFICA(g_mock_ms - t0 <= WIFI_RECONNECT_INTERVAL_MS + MQTT_CONNECT_TIMEOUT_MS + 5000UL, "riconnessione lenta");
  VERIFICA(ultimo_availability() == "online|retain", "online non pubblicato dopo riconnessione");
}

// Hotspot OTA spento dopo l'aggiornamento: ripiego sulla rete principale.
static void scenario_hotspot_ripiego() {
  WiFi.hotspot_visibile = true;
  WiFi.rete_ok = "HotIoT";
  avvia_firmware();
  VERIFICA(WiFi.ultimo_ssid == "HotIoT" && WiFi.stato == WL_CONNECTED, "boot via hotspot fallito");
  WiFi.hotspot_visibile = false;
  WiFi.rete_ok = "IoT";
  WiFi.stato = WL_DISCONNECTED;
  mock_mqtt_disconnetti();
  avanza(10 * 60 * 1000UL, 500);
  VERIFICA(WiFi.ultimo_ssid == "IoT" && WiFi.stato == WL_CONNECTED, "nessun ripiego su IoT: ultimo SSID %s", WiFi.ultimo_ssid.c_str());
}

// Persistenza del modo di controllo attraverso un riavvio (due processi).
static const char* FILE_NVS = "nvs_persistenza.bin";
static void scenario_persistenza_a() {
  avvia_firmware();
  g_mock_ds_temp = 19.0f;
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  invia("control_set", comando("pi", epoch_adesso(), "\"control_mode\":\"PI\",\"params\":{\"kp_heat\":7,\"ki_heat\":0.01,\"kd_heat\":0,\"kp_cool\":2,\"ki_cool\":0.002,\"kd_cool\":0,\"isteresi\":0.7}"));
  VERIFICA(std::string(ultimo_ack("pi")["status"] | "") == "ok", "control_set PI rifiutato");
  const std::string step = "{\"step\":\"Primaria\",\"temp\":19.0,\"duration_hours\":100}";
  invia("profile_set", comando("pp", epoch_adesso(), profilo_json("Persistente", step)));
  avanza(60 * 1000UL);
  mock_nvs_salva_su_file(FILE_NVS);
}
static void scenario_persistenza_b() {
  mock_nvs_carica_da_file(FILE_NVS);
  g_mock_reset_reason = ESP_RST_SW;
  g_mock_ms = 0;
  avvia_firmware();
  g_mock_ds_temp = 19.0f;
  avanza(STATUS_INTERVAL_MS + 20000);
  const auto st = ultimo_stato();
  VERIFICA(std::string(st["control_mode"] | "") == "PI", "modo dopo riavvio: %s", (const char*)(st["control_mode"] | ""));
  VERIFICA(std::string(st["stato_fsm"] | "") == "RUNNING", "stato dopo riavvio: %s", (const char*)(st["stato_fsm"] | ""));
  VERIFICA(std::string(st["profilo"] | "") == "Persistente", "profilo dopo riavvio: %s", (const char*)(st["profilo"] | ""));
  VERIFICA(std::string(st["reset_reason"] | "") == "software", "reset_reason: %s", (const char*)(st["reset_reason"] | ""));
  const auto ctl = ultimo_controllo();
  VERIFICA(std::fabs((ctl["isteresi"] | 0.0f) - 0.7f) < 0.001f, "isteresi dopo riavvio: %.2f", (float)(ctl["isteresi"] | 0.0f));
  VERIFICA(std::fabs((ctl["kp_freddo"] | 0.0f) - 2.0f) < 0.001f, "Kp freddo dopo riavvio: %.2f", (float)(ctl["kp_freddo"] | 0.0f));
  remove(FILE_NVS);
}

// Payload esp/state: JSON valido, nuovi campi, nessuna temperatura fittizia al boot.
static void scenario_stato_json() {
  g_mock_ds_temp = (float)DEVICE_DISCONNECTED_C;
  avvia_firmware();
  avanza(10 * 1000UL);
  bool stato_prima_lettura = false;
  for (const auto& p : g_mock_pubblicazioni) if (p.topic == TOPIC_STATE) stato_prima_lettura = true;
  VERIFICA(!stato_prima_lettura, "stato pubblicato prima di qualunque lettura valida");
  avanza(STATUS_ATTESA_PRIMA_LETTURA_MS + STATUS_INTERVAL_MS);
  auto st = ultimo_stato();
  VERIFICA(st["temp_reale"].isNull(), "temp_reale non null senza letture valide");
  VERIFICA((st["temp_eta_s"] | 0) == -1, "temp_eta_s %d", (int)(st["temp_eta_s"] | 0));

  g_mock_ds_temp = 18.5f;
  avanza(SENSOR_VALID_STREAK_MIN * SENSOR_READ_INTERVAL_MS + STATUS_INTERVAL_MS);
  st = ultimo_stato();
  for (const char* campo : {"temp_reale", "setpoint", "stato_fsm", "sensore_ok", "temp_eta_s", "ntp_sync",
                            "uptime_s", "reset_reason", "rssi", "commutazioni", "firmware", "riscaldatore"}) {
    VERIFICA(st.containsKey(campo), "campo %s mancante in esp/state", campo);
  }
  VERIFICA(std::fabs((st["temp_reale"] | 0.0f) - 18.5f) < 0.01f, "temp_reale %.2f", (float)(st["temp_reale"] | 0.0f));
  VERIFICA(st["sensore_ok"] | false, "sensore_ok false con sensore valido");
  VERIFICA(std::string(st["firmware"] | "") == FIRMWARE_VERSION, "firmware %s", (const char*)(st["firmware"] | ""));
  for (const char* k : {"letture", "non_valide", "scollegato", "poweron_85", "fuori_scala", "picco", "vicino_rele", "power_cycle"}) {
    VERIFICA(st["sensore_diag"].containsKey(k), "campo sensore_diag.%s mancante in esp/state", k);
  }
  for (const auto& p : g_mock_pubblicazioni) {
    if (p.topic == TOPIC_STATE) VERIFICA(p.payload.size() < 900, "payload esp/state di %u byte: troppo vicino al buffer di 1024", (unsigned)p.payload.size());
  }
}


// 6.3.0: grandezze del controllore pubblicate su esp/controllo e coerenti tra loro;
// derivata sulla misura senza picchi da quantizzazione.
static void scenario_controllo_didattico() {
  g_modello_termico = true;
  g_imp.temp = 18.0;
  g_imp.ambiente = 15.0;
  avvia_firmware();
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  invia("control_set", comando("cs-pid", epoch_adesso(),
        "\"control_mode\":\"PID\",\"params\":{\"kp_heat\":10,\"ki_heat\":0.01,\"kd_heat\":20000,"
        "\"kp_cool\":10,\"ki_cool\":0.01,\"kd_cool\":20000,\"isteresi\":0.8}"));
  VERIFICA(std::string(ultimo_ack("cs-pid")["status"] | "") == "ok", "control_set PID rifiutato: %s",
           (const char*)(ultimo_ack("cs-pid")["reason_code"] | ""));
  const std::string step = "{\"step\":\"Primaria\",\"temp\":20.0,\"duration_hours\":100}";
  invia("profile_set", comando("pp", epoch_adesso(), profilo_json("Didattica", step)));
  avanza(20 * 60 * 1000UL);

  auto c = ultimo_controllo();
  for (const char* k : {"control_mode", "errore", "p", "i", "d", "uscita", "integrale", "isteresi",
                        "kp_caldo", "ki_caldo", "kd_caldo", "kp_freddo", "duty_caldo", "rele_caldo", "finestra_s"}) {
    VERIFICA(c.containsKey(k), "campo %s mancante in esp/controllo", k);
  }
  const float e = c["errore"] | 0.0f, P = c["p"] | 0.0f, I = c["i"] | 0.0f, D = c["d"] | 0.0f, U = c["uscita"] | 0.0f;
  const float T = c["temp"] | 0.0f, SP = c["setpoint"] | 0.0f;
  printf("  errore %.3f P %.2f I %.2f D %.3f uscita %.2f\n", e, P, I, D, U);
  VERIFICA(std::string(c["control_mode"] | "") == "PID", "modo %s", (const char*)(c["control_mode"] | ""));
  VERIFICA(std::fabs(e - (SP - T)) < 0.07f, "errore %.3f diverso da SP-T %.3f", e, SP - T);
  VERIFICA(std::fabs(P - 10.0f * e) < 0.05f, "P %.2f diverso da Kp*e %.2f", P, 10.0f * e);
  VERIFICA(I > 0.0f, "integrale non accumulato con errore positivo: I=%.3f", I);
  const float somma = std::max(-100.0f, std::min(100.0f, P + I + D));
  VERIFICA(std::fabs(U - somma) < 0.05f, "uscita %.2f diversa da P+I+D %.2f", U, somma);
  VERIFICA(std::fabs((c["isteresi"] | 0.0f) - 0.8f) < 0.001f, "isteresi %.2f", (float)(c["isteresi"] | 0.0f));

  // D: con Kd 20000 la derivata grezza a ogni scalino da 0,0625 °C varrebbe 250 %; filtrata e
  // sulla misura deve restare piccola e regolare (riscaldamento ~1 °C/h => D ~ -5 %)
  float d_min = 1e9f, d_max = -1e9f, salto_max = 0.0f, d_prec = NAN;
  for (int k = 0; k < 60; k++) {
    avanza(30 * 1000UL);
    const float d = ultimo_controllo()["d"] | 0.0f;
    d_min = std::min(d_min, d);
    d_max = std::max(d_max, d);
    if (!std::isnan(d_prec)) salto_max = std::max(salto_max, std::fabs(d - d_prec));
    d_prec = d;
  }
  printf("  D in 30 min: min %.2f max %.2f salto max tra campioni %.2f\n", d_min, d_max, salto_max);
  VERIFICA(d_max <= 1.0f && d_min >= -20.0f, "D fuori scala: %.2f..%.2f", d_min, d_max);
  VERIFICA(salto_max < 5.0f, "D a scatti: salto %.2f", salto_max);

  // stesso modo durante RUNNING: parametri aggiornati al volo
  invia("control_set", comando("cs-live", epoch_adesso(),
        "\"control_mode\":\"PID\",\"params\":{\"kp_heat\":12,\"ki_heat\":0.01,\"kd_heat\":0,"
        "\"kp_cool\":12,\"ki_cool\":0.01,\"kd_cool\":0}"));
  VERIFICA(std::string(ultimo_ack("cs-live")["reason_code"] | "") == "mode_already_active", "aggiornamento al volo rifiutato");
  VERIFICA(std::fabs((ultimo_controllo()["kp_caldo"] | 0.0f) - 12.0f) < 0.001f, "Kp non aggiornato al volo");

  // valori fuori limite rifiutati
  invia("control_set", comando("cs-ist", epoch_adesso(), "\"control_mode\":\"PID\",\"params\":{\"isteresi\":5}"));
  VERIFICA(std::string(ultimo_ack("cs-ist")["reason_code"] | "") == "invalid_isteresi", "isteresi 5 °C accettata");
}

// 6.3.0: l'isteresi impostata da HA sposta davvero la soglia di intervento.
static void scenario_isteresi_regolabile() {
  g_modello_termico = true;
  g_imp.temp = 19.0;
  g_imp.ambiente = 24.0;
  avvia_firmware();
  avanza(CMD_FINESTRA_RETAINED_MS + 1000);
  invia("control_set", comando("cs-h", epoch_adesso(), "\"control_mode\":\"on_off_hyst_sym\",\"params\":{\"isteresi\":1.0}"));
  VERIFICA(std::string(ultimo_ack("cs-h")["status"] | "") == "ok", "control_set isteresi rifiutato");
  const std::string step = "{\"step\":\"Primaria\",\"temp\":19.0,\"duration_hours\":100}";
  invia("profile_set", comando("pp", epoch_adesso(), profilo_json("Isteresi", step)));
  double t_on = -1.0;
  for (int k = 0; k < 12 * 3600 && t_on < 0; k++) {
    avanza(1000, 1000);
    if (rele_cool()) t_on = g_imp.temp;
  }
  printf("  refrigeratore acceso a T=%.3f con setpoint 19,0 e isteresi 1,0\n", t_on);
  VERIFICA(t_on >= 19.95, "refrigeratore acceso troppo presto: T=%.3f", t_on);
  VERIFICA(t_on > 0 && t_on <= 20.2, "refrigeratore acceso troppo tardi o mai: T=%.3f", t_on);
}


// Contatori di diagnostica del DS18B20 (6.4.1): letture, scartate per causa, vicino ai relè, power-cycle.
struct DiagSensore {
  unsigned long letture = 0, non_valide = 0, scollegato = 0, poweron_85 = 0, fuori_scala = 0,
                picco = 0, vicino_rele = 0, power_cycle = 0;
};

static DiagSensore leggi_diag_sensore() {
  avanza(STATUS_INTERVAL_MS);
  const auto st = ultimo_stato();
  DiagSensore d;
  VERIFICA(st["sensore_diag"].is<JsonObjectConst>(), "sensore_diag mancante in esp/state");
  const JsonObjectConst o = st["sensore_diag"].as<JsonObjectConst>();
  d.letture     = o["letture"] | 0UL;
  d.non_valide  = o["non_valide"] | 0UL;
  d.scollegato  = o["scollegato"] | 0UL;
  d.poweron_85  = o["poweron_85"] | 0UL;
  d.fuori_scala = o["fuori_scala"] | 0UL;
  d.picco       = o["picco"] | 0UL;
  d.vicino_rele = o["vicino_rele"] | 0UL;
  d.power_cycle = o["power_cycle"] | 0UL;
  return d;
}

static void stampa_diag(const char* etichetta, const DiagSensore& d) {
  printf("  %s: letture %lu, non valide %lu (scollegato %lu, 85C %lu, fuori scala %lu, picco %lu), vicino ai rele %lu, power-cycle %lu\n",
         etichetta, d.letture, d.non_valide, d.scollegato, d.poweron_85, d.fuori_scala, d.picco, d.vicino_rele, d.power_cycle);
}

static void scenario_sensore_diagnostica() {
  // Fase A: nessun relè, lettura pilotata a mano (il modello termico resta spento).
  avvia_firmware();
  g_mock_ds_temp = 20.0f;
  avanza(2 * 60 * 1000UL);
  const DiagSensore a = leggi_diag_sensore();
  stampa_diag("dopo 2 min regolari", a);
  VERIFICA(a.letture >= 20, "letture %lu dopo 2 min (attese ~24)", a.letture);
  VERIFICA(a.non_valide == 0, "letture scartate senza alcun guasto: %lu", a.non_valide);
  VERIFICA(a.vicino_rele == 0 && a.power_cycle == 0, "contatori spuri: vicino_rele %lu power_cycle %lu", a.vicino_rele, a.power_cycle);

  g_mock_ds_temp = 85.0f;                          avanza(12 * 1000UL);   // marcatore di power-on
  g_mock_ds_temp = 20.0f;                          avanza(30 * 1000UL);
  g_mock_ds_temp = 50.0f;                          avanza(12 * 1000UL);   // fuori scala
  g_mock_ds_temp = 20.0f;                          avanza(30 * 1000UL);
  g_mock_ds_temp = (float)DEVICE_DISCONNECTED_C;   avanza(12 * 1000UL);   // -127
  g_mock_ds_temp = 20.0f;                          avanza(30 * 1000UL);
  for (int i = 0; i < 8; i++) {                                           // picchi alternati
    g_mock_ds_temp = (i % 2 == 0) ? 28.0f : 20.0f;
    avanza(SENSOR_READ_INTERVAL_MS);
  }
  g_mock_ds_temp = 20.0f;                          avanza(60 * 1000UL);
  const DiagSensore b = leggi_diag_sensore();
  stampa_diag("dopo le iniezioni", b);
  VERIFICA(b.poweron_85 > a.poweron_85, "85 C non contato");
  VERIFICA(b.fuori_scala > a.fuori_scala, "fuori scala non contato");
  VERIFICA(b.scollegato > a.scollegato, "-127 non contato");
  VERIFICA(b.picco > a.picco, "picchi alternati non contati");
  VERIFICA(b.non_valide == b.scollegato + b.poweron_85 + b.fuori_scala + b.picco,
           "totale %lu diverso dalla somma delle cause", b.non_valide);
  VERIFICA(b.letture > b.non_valide, "più scartate (%lu) che letture (%lu)", b.non_valide, b.letture);
  VERIFICA(b.vicino_rele == 0, "vicino_rele %lu senza nessuna commutazione dei relè", b.vicino_rele);

  // Fase B: con i relè in funzione, una lettura persa subito dopo una commutazione va nel contatore
  // "vicino ai relè", una persa lontano dalle commutazioni no.
  g_modello_termico = true;
  g_imp.temp = 17.0;
  g_imp.ambiente = 15.0;
  avvia_profilo_costante(20.0f);
  avanza(5 * 60 * 1000UL);
  VERIFICA(rele_heat(), "riscaldatore non acceso con T=%.2f e setpoint 20", g_imp.temp);
  const DiagSensore c = leggi_diag_sensore();

  const std::string step15 = "{\"step\":\"Primaria\",\"temp\":15.0,\"duration_hours\":500}";
  invia("profile_set", comando("prof-2", epoch_adesso(), profilo_json("Test2", step15)));
  VERIFICA(std::string(ultimo_ack("prof-2")["status"] | "") == "ok", "profile_set di prova rifiutato");
  const unsigned long t0 = g_mock_ms;
  while (rele_heat() && g_mock_ms - t0 < 2 * 60 * 1000UL) avanza(250);
  VERIFICA(!rele_heat(), "riscaldatore non spento dopo il calo del setpoint");
  g_sensore_guasto = true;
  avanza(12 * 1000UL);
  g_sensore_guasto = false;
  const DiagSensore d = leggi_diag_sensore();
  stampa_diag("guasto subito dopo lo spegnimento", d);
  VERIFICA(d.scollegato >= c.scollegato + 2, "letture -127 contate: %lu", d.scollegato - c.scollegato);
  VERIFICA(d.vicino_rele > c.vicino_rele, "nessuna lettura persa contata vicino ai relè");

  avanza(5 * 60 * 1000UL);
  const int sw0 = g_stat.heat.commutazioni + g_stat.cool.commutazioni;
  const DiagSensore e = leggi_diag_sensore();
  g_sensore_guasto = true;
  avanza(12 * 1000UL);
  g_sensore_guasto = false;
  const DiagSensore f = leggi_diag_sensore();
  const int sw1 = g_stat.heat.commutazioni + g_stat.cool.commutazioni;
  stampa_diag("guasto lontano dalle commutazioni", f);
  VERIFICA(sw1 == sw0, "relè commutati durante la prova (%d -> %d): premessa non valida", sw0, sw1);
  VERIFICA(f.scollegato >= e.scollegato + 2, "letture -127 contate: %lu", f.scollegato - e.scollegato);
  VERIFICA(f.vicino_rele == e.vicino_rele, "letture contate vicino ai relè senza commutazioni: %lu -> %lu",
           e.vicino_rele, f.vicino_rele);

  // Fase C: guasto abbastanza lungo da far scattare il power-cycle del sensore.
  g_sensore_guasto = true;
  avanza(3 * 60 * 1000UL);
  g_sensore_guasto = false;
  const DiagSensore g = leggi_diag_sensore();
  stampa_diag("dopo 3 min di guasto", g);
  VERIFICA(g.power_cycle >= 1, "power-cycle del sensore non contato: %lu", g.power_cycle);
  VERIFICA(g.non_valide == g.scollegato + g.poweron_85 + g.fuori_scala + g.picco,
           "totale %lu diverso dalla somma delle cause", g.non_valide);
}

// ============================================================
int main(int argc, char** argv) {
  if (argc < 2) {
    printf("uso: test_firmware <scenario> [-v]\n");
    return 2;
  }
  g_mock_verbose = (argc > 2 && std::string(argv[2]) == "-v");
  const std::string s = argv[1];
  g_mock_ms = 1000;

  try {
    if (s == "uscite") scenario_uscite();
    else if (s == "sensore_guasto") scenario_sensore_guasto();
    else if (s == "sensore_recupero") scenario_sensore_recupero();
    else if (s == "glitch_singolo") scenario_glitch_singolo();
    else if (s == "comandi") scenario_comandi();
    else if (s == "profilo_avvio_zero") scenario_profilo_avvio_zero();
    else if (s == "profilo_validazione") scenario_profilo_validazione();
    else if (s == "sim_onoff_48h") simulazione_48h("on_off_hyst_sym");
    else if (s == "sim_pi_48h") simulazione_48h("PI");
    else if (s == "sim_pid_48h") simulazione_48h("PID");
    else if (s == "sim_pi_lab_48h")
      // guadagni configurati nel Lab HA (input_number f1_v6_kp/ki_risc e _frigo)
      simulazione_48h("PI", "{\"kp_heat\":5,\"ki_heat\":0.005,\"kd_heat\":0,\"kp_cool\":1,\"ki_cool\":0.001,\"kd_cool\":0}",
                      SICUREZZA_BANDA_C + 0.1);
    else if (s == "sim_custom_48h") {
      // taratura: SIM_MODO=PI SIM_PARAMS={...} SIM_BANDA=0.5
      const char* m = getenv("SIM_MODO");
      const char* pr = getenv("SIM_PARAMS");
      const char* b = getenv("SIM_BANDA");
      simulazione_48h(m ? m : "PI", pr ? pr : "{}", b ? atof(b) : 1.0);
    }
    else if (s == "wifi_assente") scenario_wifi_assente();
    else if (s == "hotspot_ripiego") scenario_hotspot_ripiego();
    else if (s == "persistenza_a") scenario_persistenza_a();
    else if (s == "persistenza_b") scenario_persistenza_b();
    else if (s == "profilo_pausa_blackout_a") scenario_profilo_pausa_blackout_a();
    else if (s == "profilo_pausa_blackout_b") scenario_profilo_pausa_blackout_b();
    else if (s == "profilo_pausa_sensore") scenario_profilo_pausa_sensore();
    else if (s == "stato_json") scenario_stato_json();
    else if (s == "sensore_diagnostica") scenario_sensore_diagnostica();
    else if (s == "controllo_didattico") scenario_controllo_didattico();
    else if (s == "isteresi_regolabile") scenario_isteresi_regolabile();
    else {
      printf("scenario sconosciuto: %s\n", s.c_str());
      return 2;
    }
  } catch (const RiavvioSimulato&) {
    g_fallimenti++;
    printf("  FALLITO: riavvio inatteso a t=%lu ms\n", g_mock_ms);
  }

  printf("%s %s\n", g_fallimenti == 0 ? "OK      " : "FALLITO ", s.c_str());
  return g_fallimenti == 0 ? 0 : 1;
}
