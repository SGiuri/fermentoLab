#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "controller.h"
#include "display.h"
#include "connettivita.h"
#include "command_processor.h"
#include "autotune.h"
#include "sensor_watchdog.h"
#include "uscite_rele.h"
#include "profilo.h"
#include "log.h"
#include "diag_reset.h"

static ControllerState g_controller = {};
static ControllerConfig g_config = {
  ControlMode::ON_OFF_HYST_SYM,
  DEFAULT_HYSTERESIS_C,
  DEFAULT_HYSTERESIS_C,
  DEFAULT_HYSTERESIS_C,
  DEFAULT_KP_HEAT,
  DEFAULT_KI_HEAT,
  DEFAULT_KD_HEAT,
  DEFAULT_KP_COOL,
  DEFAULT_KI_COOL,
  DEFAULT_KD_COOL,
  DEFAULT_OUTPUT_DEADBAND_PCT,
  DEFAULT_MIN_EFFECTIVE_OUTPUT_PCT
};

static AutotuneContext g_autotune = {};

static float g_temp = DEFAULT_STANDBY_TEMP_C;
static float g_setpoint = 20.5f;
static unsigned long g_last_control_ms = 0;
static unsigned long g_last_autotune_pub_ms = 0;
static unsigned long g_ts_sensor_instabile = 0;
static unsigned long g_ts_alarm_start = 0;
static unsigned long g_ms_inizio_loop = 0;

// --- Sensore ---
// Il controllo usa la temperatura solo se "utilizzabile": almeno SENSOR_VALID_STREAK_MIN
// letture valide consecutive e ultima lettura valida più recente di SENSOR_MAX_ETA_MS.
// Prima il controllo pilotava i relè anche con sensore guasto, su un valore congelato.
static SensorState g_sensore = {};
static bool    g_sensore_stabile = false;
static bool    g_fallback_attivo = false;   // sensore perso: relè forzati OFF
static uint8_t g_sensore_valid_streak = 0;
static uint8_t g_power_cycle_tentativi = 0;
static bool    g_riavvio_sensore_sospeso = false;
static bool    g_nuova_lettura = false;     // nuova lettura valida: il controllore ricalcola

// Contatore riavvii per allarme sensore: sopravvive a ESP.restart(), non al power-on.
// Evita un ciclo infinito di riavvii con sensore definitivamente guasto.
static RTC_NOINIT_ATTR uint32_t g_rtc_magic;
static RTC_NOINIT_ATTR uint32_t g_rtc_riavvii_sensore;
static const uint32_t RTC_MAGIC = 0x46365254UL;

// --- Profilo fermentazione ---
enum class ProfiloFsmState : uint8_t { STANDBY = 0, RUNNING = 1, HOLDING = 2 };
static ProfiloFermentazione g_profilo     = {};
static ProfiloFsmState      g_profilo_fsm = ProfiloFsmState::STANDBY;

// --- Persistenza NVS (scritture solo su cambio o ogni NVS_SAVE_INTERVAL_MS) ---
static const char*   g_nvs_fsm_salvato  = nullptr;
static int           g_nvs_step_salvato = -1;
static unsigned long g_ts_nvs_save      = 0;

// --- Heartbeat ---
static unsigned long g_last_status_ms    = 0;
static const char*   g_pub_fsm           = nullptr;
static bool          g_pub_heat          = false;
static bool          g_pub_cool          = false;

// --- Deduplica comandi ---
struct VoceDedup {
  char     request_id[48];
  uint32_t ts;
};
static VoceDedup g_dedup[CMD_DEDUP_SLOTS] = {};
static uint8_t   g_dedup_idx = 0;

static char g_cmd_line[640];
static size_t g_cmd_len = 0;

// --- Network state ---
static unsigned long g_ts_wifi_perso          = 0;
static unsigned long g_ts_last_wifi_attempt   = 0;
static unsigned long g_ts_last_mqtt_attempt   = 0;
static int           g_tentativi_riconn       = 0;
static int           g_tentativi_hotspot      = 0;
static unsigned long g_last_display_ms        = 0;
static bool          g_wifi_connesso          = false;
static bool          g_mqtt_era_connesso      = false;
static bool          g_ota_avviato            = false;
// true se al boot l'ESP si è connesso via hotspot OTA (HOTSPOT_SSID ha priorità su WIFI_SSID)
static bool          g_via_hotspot            = false;

static inline const char* profilo_fsm_str() {
  switch (g_profilo_fsm) {
    case ProfiloFsmState::RUNNING: return "running";
    case ProfiloFsmState::HOLDING: return "holding";
    default: return "standby";
  }
}

// ============================================================
// Tempo
// ============================================================
static bool orologio_valido() {
  return time(nullptr) >= (time_t)EPOCH_MINIMO_VALIDO;
}

// Avanza g_profilo.secondi_trascorsi: solo mentre il controllo è davvero in corso (RUNNING e
// sensore utilizzabile), a passi interi di secondo misurati sull'uptime (millis()), non
// sull'orologio di rete. Un blackout, un riavvio o un guasto del sensore mettono la curva in
// pausa invece di farla saltare avanti quando la connessione torna: fino alla 6.3.0 la posizione
// era "ora reale − avvio_epoch", e un'assenza di NTP dopo un'interruzione veniva recuperata tutta
// insieme al primo risincronismo (vedi docs/11_TIMING_PROFILO.md). Nessuna dipendenza da NTP:
// il profilo esegue la curva anche con l'ESP32 sempre offline.
static void avanza_profilo(unsigned long now_ms, bool temp_ok) {
  static unsigned long ts_prec_ms = 0;
  const bool attivo = g_profilo.valido &&
                      g_controller.runtime_state == RuntimeState::RUNNING && temp_ok;
  if (!attivo) {
    ts_prec_ms = now_ms;  // nessun "debito" di tempo da recuperare alla ripresa
    return;
  }
  if (ts_prec_ms == 0) ts_prec_ms = now_ms;
  const uint32_t dt_s = (uint32_t)((now_ms - ts_prec_ms) / 1000UL);
  if (dt_s == 0) return;
  g_profilo.secondi_trascorsi += dt_s;
  ts_prec_ms += dt_s * 1000UL;  // avanza a passi interi: nessun residuo che si accumula nel tempo
}

// Indice dello step corrente (ultimo se il profilo è terminato), -1 se nessun profilo.
static int posizione_profilo(float* ore_step, float* ore_totali, float* durata_step) {
  *ore_step = 0.0f;
  *ore_totali = 0.0f;
  *durata_step = 0.0f;
  if (!g_profilo.valido || g_profilo.num_steps <= 0) return -1;

  const float ore = g_profilo.secondi_trascorsi / 3600.0f;
  *ore_totali = ore;
  float t_start = 0.0f;
  for (int i = 0; i < g_profilo.num_steps; i++) {
    const float dur = g_profilo.steps[i].is_ramp
                      ? g_profilo.steps[i].ramp_hours
                      : g_profilo.steps[i].duration_hours;
    if (ore >= t_start && ore < t_start + dur) {
      *ore_step = ore - t_start;
      *durata_step = dur;
      return i;
    }
    t_start += dur;
  }
  const int ultimo = g_profilo.num_steps - 1;
  *durata_step = g_profilo.steps[ultimo].is_ramp
                 ? g_profilo.steps[ultimo].ramp_hours
                 : g_profilo.steps[ultimo].duration_hours;
  return ultimo;
}

// Aggiorna setpoint e stato RUNNING/HOLDING dal profilo. avvio_epoch è solo informativo (mostra
// "avviato il"): gli assegna l'ora corrente appena l'orologio è valido, se non già impostato.
// La posizione nella curva usa g_profilo.secondi_trascorsi (vedi avanza_profilo), non l'orologio.
static void aggiorna_setpoint_da_profilo(bool notifica_completamento) {
  if (!g_profilo.valido || g_profilo.num_steps <= 0) return;

  if (g_profilo.avvio_epoch == 0 && orologio_valido()) {
    g_profilo.avvio_epoch = (uint32_t)time(nullptr);
    salvaProfilo(g_profilo);
    LOG_I("PROFILO", "avvio_epoch assegnato all'ora corrente: %lu", (unsigned long)g_profilo.avvio_epoch);
  }

  const uint32_t s = g_profilo.secondi_trascorsi;
  const float sp   = calcolaSetpoint(g_profilo, s);
  if (sp > -998.0f) g_setpoint = sp;

  const bool terminato = (s / 3600.0f) >= calcolaDurataTotaleOre(g_profilo);
  if (terminato && g_profilo_fsm == ProfiloFsmState::RUNNING && notifica_completamento) {
    // Evento profilo_completato su ACK (non su alarm)
    mqtt_pubblica_ack("", "profile_complete", "event", "profilo_completato");
  }
  g_profilo_fsm = terminato ? ProfiloFsmState::HOLDING : ProfiloFsmState::RUNNING;
}

// ============================================================
// Sensore
// ============================================================
static bool temperatura_utilizzabile() {
  return g_sensore_stabile && sensor_eta_ultima_valida_ms(g_sensore) <= SENSOR_MAX_ETA_MS;
}

static float setpoint_limitato() {
  if (g_setpoint < DEFAULT_MIN_SETPOINT_C) return DEFAULT_MIN_SETPOINT_C;
  if (g_setpoint > DEFAULT_MAX_SETPOINT_C) return DEFAULT_MAX_SETPOINT_C;
  return g_setpoint;
}

// ============================================================
// Stato / diagnostica
// ============================================================
static const char* reset_reason_str() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "poweron";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int_wdt";
    case ESP_RST_TASK_WDT:  return "task_wdt";
    case ESP_RST_WDT:       return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "other";
  }
}

// Restituisce la stringa stato_fsm conforme a HASS_V6_SPEC §2.1
static const char* get_stato_fsm_string() {
  if (g_sensore.in_allarme) return "ALLARME_SENSORE";
  if (g_controller.runtime_state == RuntimeState::AUTOTUNING) return "AUTOTUNE";
  if (g_controller.runtime_state == RuntimeState::RUNNING) {
    return (g_profilo_fsm == ProfiloFsmState::HOLDING) ? "HOLDING" : "RUNNING";
  }
  return "STOPPED";
}

// Compone il payload heartbeat da stato globale (HASS_V6_SPEC §2.1)
static StatoHeartbeat build_heartbeat() {
  StatoHeartbeat hb = {};
  hb.stato_fsm         = get_stato_fsm_string();
  hb.control_mode      = controller_mode_to_string(g_config.mode);
  hb.temp_disponibile  = (g_sensore.ts_ultima_valida_ms != 0);
  hb.temp_reale        = g_temp;
  hb.setpoint          = g_setpoint;
  hb.riscaldatore      = uscite_riscaldatore_acceso();
  hb.refrigeratore     = uscite_refrigeratore_acceso();
  hb.duty_riscaldatore = g_controller.heat_duty_pct;
  hb.duty_refrigeratore = g_controller.cool_duty_pct;
  // pid_output: positivo = caldo, negativo = freddo
  if (g_controller.heat_on) {
    hb.pid_output = g_controller.heat_duty_pct;
  } else if (g_controller.cool_on) {
    hb.pid_output = -g_controller.cool_duty_pct;
  } else {
    hb.pid_output = 0.0f;
  }
  hb.allarme      = g_sensore.in_allarme;
  hb.profilo_nome = g_profilo.valido ? g_profilo.nome : "";

  float ore_step = 0.0f;
  float ore_tot = 0.0f;
  float durata = 0.0f;
  const int idx = posizione_profilo(&ore_step, &ore_tot, &durata);
  hb.step_index = (idx < 0) ? 0 : idx;
  hb.step_nome  = (idx < 0) ? "" : g_profilo.steps[idx].nome;
  hb.ore_step   = ore_step;
  hb.ore_totali = ore_tot;

  hb.sensore_ok = temperatura_utilizzabile();
  const uint32_t eta_ms = sensor_eta_ultima_valida_ms(g_sensore);
  hb.temp_eta_s = (eta_ms == UINT32_MAX) ? -1 : (int32_t)(eta_ms / 1000UL);
  hb.ntp_sync = orologio_valido();
  hb.reset_reason = reset_reason_str();
  hb.rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  hb.commutazioni = uscite_commutazioni();
  hb.sensore_diag = g_sensore.diag;
  return hb;
}

static StatoControllo build_controllo() {
  StatoControllo c = {};
  c.control_mode = controller_mode_to_string(g_config.mode);
  c.attivo = g_controller.runtime_state == RuntimeState::RUNNING && temperatura_utilizzabile();
  c.temp = g_temp;
  c.setpoint = setpoint_limitato();
  c.errore = g_controller.errore;
  c.p = g_controller.p_term;
  c.i = g_controller.i_term;
  c.d = g_controller.d_term;
  c.uscita = g_controller.uscita_pct;
  c.integrale = g_controller.integrale;
  c.duty_caldo = g_controller.heat_duty_pct;
  c.duty_freddo = g_controller.cool_duty_pct;
  c.rele_caldo = uscite_riscaldatore_acceso();
  c.rele_freddo = uscite_refrigeratore_acceso();
  c.isteresi = g_config.hysteresis_sym_c;
  c.isteresi_caldo = g_config.hysteresis_heat_c;
  c.isteresi_freddo = g_config.hysteresis_cool_c;
  c.kp_caldo = g_config.kp_heat;
  c.ki_caldo = g_config.ki_heat;
  c.kd_caldo = g_config.kd_heat;
  c.kp_freddo = g_config.kp_cool;
  c.ki_freddo = g_config.ki_cool;
  c.kd_freddo = g_config.kd_cool;
  c.zona_morta = g_config.output_deadband_pct;
  c.uscita_minima = g_config.min_effective_output_pct;
  return c;
}

static void pubblica_stato(unsigned long now) {
  if (!connettivita_mqtt_connesso()) return;
  // Al boot non pubblicare una temperatura fittizia prima della prima lettura.
  if (g_sensore.ts_ultima_valida_ms == 0 && now - g_ms_inizio_loop < STATUS_ATTESA_PRIMA_LETTURA_MS) return;
  LOG_D("HB", "T=%.2f SP=%.2f ok=%d heap=%u stato=%s profilo=%s",
        g_temp, g_setpoint, temperatura_utilizzabile() ? 1 : 0, ESP.getFreeHeap(),
        get_stato_fsm_string(), profilo_fsm_str());
  mqtt_pubblica_stato(build_heartbeat());
  mqtt_pubblica_controllo(build_controllo());
  mqtt_pubblica_diag(diag_reset_precedente(), diag_reset_loop());
  g_last_status_ms = now;
  g_pub_fsm  = get_stato_fsm_string();
  g_pub_heat = uscite_riscaldatore_acceso();
  g_pub_cool = uscite_refrigeratore_acceso();
}

// Heartbeat periodico + pubblicazione anticipata quando cambiano relè o stato FSM,
// così HA vede le commutazioni reali invece di un campione ogni 60 s.
static void handle_heartbeat(unsigned long now) {
  const bool cambiato = (uscite_riscaldatore_acceso() != g_pub_heat) ||
                        (uscite_refrigeratore_acceso() != g_pub_cool) ||
                        (get_stato_fsm_string() != g_pub_fsm);
  const unsigned long trascorso = now - g_last_status_ms;
  if (trascorso < STATUS_MIN_INTERVAL_MS) return;
  if (!cambiato && trascorso < STATUS_INTERVAL_MS) return;

  if (connettivita_mqtt_connesso()) {
    pubblica_stato(now);
  } else if (trascorso >= STATUS_INTERVAL_MS) {
    g_last_status_ms = now;
  }
}

// ============================================================
// Persistenza NVS
// ============================================================
static void handle_persistenza(unsigned long now) {
  if (g_controller.runtime_state != RuntimeState::RUNNING) return;

  const char* fsm_save = (g_profilo_fsm == ProfiloFsmState::HOLDING) ? "HOLDING" : "RUNNING";
  float ore_step = 0.0f;
  float ore_tot = 0.0f;
  float durata = 0.0f;
  const int step = posizione_profilo(&ore_step, &ore_tot, &durata);

  const bool cambiato = (fsm_save != g_nvs_fsm_salvato) || (step != g_nvs_step_salvato);
  if (!cambiato && now - g_ts_nvs_save < NVS_SAVE_INTERVAL_MS) return;

  salvaStatoFsm(fsm_save, step < 0 ? 0 : step, ore_tot);
  if (g_profilo.valido) {
    salvaProgresso(g_profilo.secondi_trascorsi);
  }
  g_nvs_fsm_salvato  = fsm_save;
  g_nvs_step_salvato = step;
  g_ts_nvs_save      = now;
}

// ============================================================
// Controllo
// ============================================================
static void spegni_uscite_subito() {
  const RichiestaUscite nessuna = {};
  uscite_aggiorna(nessuna, true, g_temp, g_setpoint, millis());
}

static void start_run() {
  LOG_I("FSM", "start_run: RUNNING (profilo=%s)", profilo_fsm_str());
  // Reset stato PID: altrimenti il primo dt copre tutto il tempo passato in STOPPED
  // e l'integrale salta al limite.
  g_controller.integrale = 0.0f;
  g_controller.errore_prec = 0.0f;
  g_controller.ts_calc_ms = 0;
  g_controller.misura_prec_valida = false;
  g_controller.derivata_filtrata = 0.0f;
  g_controller.runtime_state = RuntimeState::RUNNING;
}

static void stop_run() {
  LOG_I("FSM", "stop_run: STOPPED");
  g_controller.runtime_state = RuntimeState::STOPPED;
  controller_stop_outputs(g_controller);
  spegni_uscite_subito();
}

static void riavvia_controllato(const char* motivo) {
  LOG_W("SYS", "Riavvio: %s", motivo);
  // Checkpoint immediato del progresso: su un riavvio volontario evita di ripartire da
  // fino a NVS_SAVE_INTERVAL_MS indietro rispetto a dove il profilo era arrivato davvero.
  if (g_profilo.valido) salvaProgresso(g_profilo.secondi_trascorsi);
  spegni_uscite_subito();
  connettivita_annuncia_offline();
  delay(200);
  ESP.restart();
}

static void watchdog_init() {
  uint32_t idle_mask = 0;
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
  idle_mask |= (1U << 0);
#endif
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
  idle_mask |= (1U << 1);
#endif
  const esp_task_wdt_config_t cfg = {
    .timeout_ms = WDT_TIMEOUT_S * 1000U,
    .idle_core_mask = idle_mask,
    .trigger_panic = true,
  };
  esp_err_t err = esp_task_wdt_reconfigure(&cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    err = esp_task_wdt_init(&cfg);
  }
  if (err == ESP_OK) {
    err = esp_task_wdt_add(nullptr);  // loopTask: setup() e loop() girano qui
  }
  Serial.printf("[WDT] task watchdog %us: %s\n", (unsigned)WDT_TIMEOUT_S, err == ESP_OK ? "attivo" : "ERRORE");
}

// ============================================================
// Sensore: letture, fallback, power-cycle, allarme
// ============================================================
static void handle_sensor_logic(unsigned long now) {
  const LetturaSensore esito = sensor_aggiorna(g_sensore, SENSOR_ALARM_TIMEOUT_MINUTES,
                                               uscite_ultima_commutazione_ms());

  if (esito == LetturaSensore::VALIDA) {
    g_temp = g_sensore.ultima_temp_valida;
    g_nuova_lettura = true;
    if (g_sensore_valid_streak < 255) g_sensore_valid_streak++;
    if (g_sensore_valid_streak >= SENSOR_VALID_STREAK_MIN) g_sensore_stabile = true;
  } else if (esito == LetturaSensore::NON_VALIDA) {
    g_sensore_valid_streak = 0;
  }

  const uint32_t eta_ms = sensor_eta_ultima_valida_ms(g_sensore);
  if (eta_ms > SENSOR_MAX_ETA_MS) g_sensore_stabile = false;

  // Sensore "perso": nessuna lettura valida recente (al boot dopo un periodo di grazia).
  const bool sensore_perso = (g_sensore.ts_ultima_valida_ms == 0)
                             ? (now - g_ms_inizio_loop > SENSOR_MAX_ETA_MS)
                             : (eta_ms > SENSOR_MAX_ETA_MS);

  if (temperatura_utilizzabile()) {
    if (g_fallback_attivo) {
      g_fallback_attivo = false;
      LOG_I("SENSORE", "Sensore recuperato: controllo ripristinato");
      mqtt_pubblica_allarme("recovery_sensore", 0);
    }
    g_ts_sensor_instabile = 0;
    g_power_cycle_tentativi = 0;
    g_riavvio_sensore_sospeso = false;
    g_rtc_riavvii_sensore = 0;
  } else if (sensore_perso) {
    if (!g_fallback_attivo) {
      g_fallback_attivo = true;
      LOG_W("SENSORE", "Nessuna lettura valida da %lus: rele' forzati OFF", (unsigned long)(eta_ms / 1000UL));
      mqtt_pubblica_allarme("sensore_instabile", 0);
    }
    if (g_ts_sensor_instabile == 0) {
      g_ts_sensor_instabile = now;
    }
  }

  if (g_fallback_attivo && g_ts_sensor_instabile > 0 &&
      (now - g_ts_sensor_instabile) >= SENSOR_POWER_CYCLE_TIMEOUT_MS &&
      g_power_cycle_tentativi < SENSOR_POWER_CYCLE_MAX_ATTEMPTS) {
    g_power_cycle_tentativi++;
    esp_task_wdt_reset();
    const bool ok = sensor_power_cycle(g_sensore);
    g_ts_sensor_instabile = millis();
    LOG_W("SENSORE", "Power-cycle #%u: %s", g_power_cycle_tentativi, ok ? "lettura valida" : "fallito");
    mqtt_pubblica_ack("n/a", "sensor_power_cycle", ok ? "ok" : "error", ok ? "recovered" : "failed");
  }

  if (sensor_allarme_nuovo(g_sensore)) {
    // Nessuno stop_run(): i relè sono già OFF per il gating e il controllo
    // riprende da solo quando il sensore torna valido.
    g_ts_alarm_start = now;
    LOG_E("SENSORE", "DS18B20 offline: allarme attivato");
    mqtt_pubblica_allarme("sensore_offline", SENSOR_ALARM_TIMEOUT_MINUTES);
  }
  if (!g_sensore.in_allarme) {
    g_ts_alarm_start = 0;
  }

  if (g_sensore.in_allarme && g_ts_alarm_start > 0 && !g_riavvio_sensore_sospeso &&
      (now - g_ts_alarm_start) >= SENSOR_REBOOT_DELAY_MS) {
    if (g_rtc_riavvii_sensore < SENSOR_REBOOT_MAX) {
      g_rtc_riavvii_sensore++;
      mqtt_pubblica_allarme("riavvio_in_corso", 0);
      riavvia_controllato("allarme sensore");
    } else {
      g_riavvio_sensore_sospeso = true;
      LOG_E("SENSORE", "Sensore guasto dopo %u riavvii: resto in allarme senza riavviare", (unsigned)SENSOR_REBOOT_MAX);
      mqtt_pubblica_allarme("riavvio_sospeso", 0);
    }
  }
}

// ============================================================
// Comandi
// ============================================================
static bool comando_duplicato(const CommandRequest& cmd) {
  if (cmd.ts == 0) return false;  // senza ts non è possibile distinguere un ripetuto da uno nuovo
  for (size_t i = 0; i < CMD_DEDUP_SLOTS; i++) {
    if (g_dedup[i].ts == cmd.ts && strcmp(g_dedup[i].request_id, cmd.request_id) == 0) {
      return true;
    }
  }
  return false;
}

static void registra_comando(const CommandRequest& cmd) {
  if (cmd.ts == 0) return;
  VoceDedup& v = g_dedup[g_dedup_idx];
  strncpy(v.request_id, cmd.request_id, sizeof(v.request_id) - 1);
  v.request_id[sizeof(v.request_id) - 1] = '\0';
  v.ts = cmd.ts;
  g_dedup_idx = (uint8_t)((g_dedup_idx + 1) % CMD_DEDUP_SLOTS);
}

// Copia nella configurazione i parametri presenti nel comando (guadagni PID, isteresi).
static void applica_parametri(ControllerConfig& c, const CommandRequest& cmd) {
  if (cmd.has_pid_params) {
    c.kp_heat = cmd.kp_heat;
    c.ki_heat = cmd.ki_heat;
    c.kd_heat = cmd.kd_heat;
    c.kp_cool = cmd.kp_cool;
    c.ki_cool = cmd.ki_cool;
    c.kd_cool = cmd.kd_cool;
  }
  if (cmd.has_isteresi) {
    c.hysteresis_sym_c = cmd.isteresi;
    c.hysteresis_heat_c = cmd.isteresi;
    c.hysteresis_cool_c = cmd.isteresi;
  }
  if (cmd.has_isteresi_caldo) c.hysteresis_heat_c = cmd.isteresi_caldo;
  if (cmd.has_isteresi_freddo) c.hysteresis_cool_c = cmd.isteresi_freddo;
}

static bool ha_parametri(const CommandRequest& cmd) {
  return cmd.has_pid_params || cmd.has_isteresi || cmd.has_isteresi_caldo || cmd.has_isteresi_freddo;
}

static void apply_control_request(const CommandRequest& cmd) {
  if (!cmd.has_control_mode) {
    mqtt_pubblica_ack(cmd.request_id, "control_set", "error", "missing_control_mode");
    return;
  }

  // Durante RUNNING il cambio di algoritmo resta vietato (workflow stop → control_set → start),
  // ma lo stesso modo è accettato: aggiorna i parametri e non genera errori ripetuti
  // dalle automazioni HA che reinviano control_set dopo un riconnessione.
  if (g_controller.runtime_state != RuntimeState::STOPPED && cmd.requested_mode == g_config.mode) {
    if (ha_parametri(cmd)) {
      applica_parametri(g_config, cmd);
      salvaConfigControllo(g_config);
    }
    mqtt_pubblica_ack(cmd.request_id, "control_set", "ok", "mode_already_active");
    pubblica_stato(millis());
    return;
  }

  ControllerConfig nuova = g_config;
  applica_parametri(nuova, cmd);

  char reason[96];
  const bool ok = controller_set_mode(g_controller,
                                      nuova,
                                      cmd.requested_mode,
                                      reason,
                                      sizeof(reason));
  if (ok) {
    g_config = nuova;
    salvaConfigControllo(g_config);
  }
  mqtt_pubblica_ack(cmd.request_id,
                    "control_set",
                    ok ? "ok" : "error",
                    reason);
  if (ok) {
    pubblica_stato(millis());
  }
}

static void apply_autotune_command(const CommandRequest& cmd) {
  char reason[96];

  if (!AUTOTUNE_ABILITATO && cmd.type != CommandType::AUTOTUNE_STOP) {
    mqtt_pubblica_ack(cmd.request_id, command_type_to_string(cmd.type), "error", "autotune_disabled");
    return;
  }

  if (cmd.type == CommandType::AUTOTUNE_START) {
    const bool ok = autotune_start(g_autotune,
                                   g_controller,
                                   g_setpoint,
                                   reason,
                                   sizeof(reason));
    if (ok) {
      g_controller.runtime_state = RuntimeState::AUTOTUNING;
      LOG_I("AUTOTUNE", "Avviato: setpoint=%.2f", g_setpoint);
    } else {
      LOG_W("AUTOTUNE", "Start fallito: %s", reason);
    }
    mqtt_pubblica_ack(cmd.request_id,
                      "autotune_start",
                      ok ? "ok" : "error",
                      reason);
    return;
  }

  if (cmd.type == CommandType::AUTOTUNE_STOP) {
    if (g_controller.runtime_state == RuntimeState::AUTOTUNING) {
      autotune_stop(g_autotune, "manual_stop");
      stop_run();
      LOG_I("AUTOTUNE", "Fermato manualmente");
    }
    mqtt_pubblica_ack(cmd.request_id, "autotune_stop", "ok", "manual_stop");
    return;
  }

  if (cmd.type == CommandType::AUTOTUNE_APPLY) {
    const bool ok = autotune_apply(g_autotune,
                                   g_controller,
                                   g_config,
                                   cmd.require_quality_score_gte,
                                   reason,
                                   sizeof(reason));
    mqtt_pubblica_ack(cmd.request_id,
                      "autotune_apply",
                      ok ? "ok" : "error",
                      reason);
    if (ok) {
      salvaConfigControllo(g_config);
      LOG_I("AUTOTUNE", "Parametri applicati: kp_h=%.3f ki_h=%.3f",
            g_config.kp_heat, g_config.ki_heat);
      mqtt_pubblica_autotune_result(g_autotune);
    } else {
      LOG_W("AUTOTUNE", "Apply fallito: %s", reason);
    }
  }
}

static void apply_profile_request(const CommandRequest& cmd, const char* payload) {
  static ProfiloFermentazione p;  // static: evita 1344 B sullo stack loopTask durante il callback MQTT
  char motivo[64];
  if (!parsaProfiloJSON(payload, p, motivo, sizeof(motivo))) {
    LOG_W("PROFILO", "Profilo rifiutato: %s", motivo);
    mqtt_pubblica_ack(cmd.request_id, "profile_set", "error", motivo);
    return;
  }
  // Senza avvio_epoch parte adesso; se l'orologio non è valido viene assegnato alla prima sincronizzazione.
  if (p.avvio_epoch == 0 && orologio_valido()) {
    p.avvio_epoch = (uint32_t)time(nullptr);
  }
  g_profilo = p;
  salvaProfilo(g_profilo);
  aggiorna_setpoint_da_profilo(false);

  LOG_I("PROFILO", "Profilo '%s': %u step, setpoint=%.2f, stato=%s, avvia=%d",
        g_profilo.nome, g_profilo.num_steps, g_setpoint, profilo_fsm_str(), cmd.avvia ? 1 : 0);

  if (cmd.avvia) {
    start_run();
    g_nvs_fsm_salvato = nullptr;  // forza salvataggio NVS immediato
    mqtt_pubblica_ack(cmd.request_id, "profile_set", "ok", "profile_applied");
  } else {
    mqtt_pubblica_ack(cmd.request_id, "profile_set", "ok", "profile_stored");
  }
  pubblica_stato(millis());
}

static void esegui_comando(const char* topic, const char* payload, bool da_mqtt) {
  CommandRequest cmd;
  char reason[96];
  if (!parse_command(topic, payload, cmd, reason, sizeof(reason))) {
    // request_id e comando quando già estratti: HA può correlare l'errore alla richiesta
    mqtt_pubblica_ack(cmd.request_id[0] ? cmd.request_id : "n/a",
                      cmd.type != CommandType::NONE ? command_type_to_string(cmd.type) : "parse",
                      "error", reason);
    return;
  }
  const char* nome_cmd = command_type_to_string(cmd.type);

  if (da_mqtt) {
    const int64_t ora = (int64_t)time(nullptr);
    const bool orologio_ok = orologio_valido();
    const int64_t eta_s = orologio_ok && cmd.ts > 0 ? ora - (int64_t)cmd.ts : 0;
    const bool ts_recente = orologio_ok && cmd.ts > 0 && eta_s <= CMD_MAX_ETA_S && eta_s >= -CMD_MAX_ETA_S;

    // Un comando retained viene riconsegnato a ogni (ri)connessione: senza questo filtro
    // un profile_set o uno start vecchio riavviava un fermentatore messo in STOP.
    if (connettivita_ms_da_sottoscrizione() < CMD_FINESTRA_RETAINED_MS && !ts_recente) {
      LOG_W("CMD", "Ignorato %s id=%s: arrivato alla sottoscrizione senza ts recente (probabile retained)",
            nome_cmd, cmd.request_id);
      mqtt_pubblica_ack(cmd.request_id, nome_cmd, "error", "possible_retained_ignored");
      return;
    }
    if (orologio_ok && cmd.ts > 0 && eta_s > CMD_MAX_ETA_S) {
      LOG_W("CMD", "Ignorato %s id=%s: ts vecchio di %lds", nome_cmd, cmd.request_id, (long)eta_s);
      mqtt_pubblica_ack(cmd.request_id, nome_cmd, "error", "stale_command");
      return;
    }
  }

  // STOP è idempotente e di sicurezza: sempre eseguito.
  if (cmd.type != CommandType::STOP && comando_duplicato(cmd)) {
    mqtt_pubblica_ack(cmd.request_id, nome_cmd, "ok", "duplicate_ignored");
    return;
  }
  registra_comando(cmd);

  switch (cmd.type) {
    case CommandType::START:
      if (!g_profilo.valido) {
        mqtt_pubblica_ack(cmd.request_id, "start", "error", "no_profile");
        break;
      }
      aggiorna_setpoint_da_profilo(false);
      start_run();
      g_nvs_fsm_salvato = nullptr;
      mqtt_pubblica_ack(cmd.request_id, "start", "ok", nullptr);
      pubblica_stato(millis());
      break;
    case CommandType::STOP:
      stop_run();
      // Aggiorna NVS stato → STOPPED (interrompe recovery al prossimo boot)
      salvaStatoFsm("STOPPED", 0, 0.0f);
      g_nvs_fsm_salvato = nullptr;
      // Non azzerare g_profilo: un successivo START riprende da dove era rimasto
      mqtt_pubblica_ack(cmd.request_id, "stop", "ok", nullptr);
      pubblica_stato(millis());
      break;
    case CommandType::CONTROL_SET:
      apply_control_request(cmd);
      break;
    case CommandType::AUTOTUNE_START:
    case CommandType::AUTOTUNE_STOP:
    case CommandType::AUTOTUNE_APPLY:
      apply_autotune_command(cmd);
      break;
    case CommandType::REBOOT:
      mqtt_pubblica_ack(cmd.request_id, "reboot", "ok", nullptr);
      riavvia_controllato("comando reboot");
      break;
    case CommandType::PROFILE_SET:
      apply_profile_request(cmd, payload);
      break;
    default:
      mqtt_pubblica_ack(cmd.request_id, nome_cmd, "error", "unsupported");
      break;
  }
}

static void process_command_message(const char* topic, const char* payload) {
  esegui_comando(topic, payload, true);
}

// Debug input format on serial: <topic>|<json>
static void process_serial_commands() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      g_cmd_line[g_cmd_len] = '\0';
      if (g_cmd_len > 0) {
        char* sep = strchr(g_cmd_line, '|');
        if (sep) {
          *sep = '\0';
          const char* topic = g_cmd_line;
          const char* payload = sep + 1;
          esegui_comando(topic, payload, false);
        } else {
          LOG_W("SERIAL", "Formato seriale non valido: atteso topic|json");
        }
      }
      g_cmd_len = 0;
      continue;
    }

    if (g_cmd_len + 1 < sizeof(g_cmd_line)) {
      g_cmd_line[g_cmd_len++] = c;
    }
  }
}

// ============================================================
// Display: aggiornamento periodico OLED
// ============================================================
static void handle_display(unsigned long now) {
  if (now - g_last_display_ms < DISPLAY_UPDATE_MS) return;
  g_last_display_ms = now;

  // ── Stato FSM display ────────────────────────────────────
  // Il controllo ha priorità sullo stato di rete: prima con WiFi assente il display
  // mostrava "Nessun profilo" anche durante una fermentazione in corso.
  const char* stato;
  if (g_sensore.in_allarme) {
    stato = "ALLARME";
  } else if (g_controller.runtime_state == RuntimeState::AUTOTUNING) {
    stato = "AUTOTUNE";
  } else if (g_controller.runtime_state == RuntimeState::RUNNING) {
    stato = (g_profilo_fsm == ProfiloFsmState::HOLDING) ? "HOLDING" : "RUNNING";
  } else if (WiFi.status() != WL_CONNECTED) {
    stato = "WIFI";
  } else if (!connettivita_mqtt_connesso()) {
    stato = "MQTT";
  } else {
    stato = "STANDBY";
  }

  // ── Step corrente ────────────────────────────────────────
  float ore_st = 0.0f;
  float ore_tot = 0.0f;
  float step_dur = 0.0f;
  const int sidx = posizione_profilo(&ore_st, &ore_tot, &step_dur);
  float giorni_next = -1.0f;
  float prog_pct = 0.0f;

  if (sidx >= 0) {
    const float tot_h = calcolaDurataTotaleOre(g_profilo);
    if (tot_h > 0.0f) {
      prog_pct = (ore_tot / tot_h) * 100.0f;
      if (prog_pct < 0.0f)   prog_pct = 0.0f;
      if (prog_pct > 100.0f) prog_pct = 100.0f;
    }
    float rem_h = step_dur - ore_st;
    if (rem_h < 0.0f) rem_h = 0.0f;
    giorni_next = rem_h / 24.0f;
  }

  DisplayData dd = {};
  dd.stato_fsm              = stato;
  dd.temp_reale             = g_temp;
  dd.temp_valida            = temperatura_utilizzabile();
  dd.setpoint               = g_setpoint;
  dd.step_nome              = (sidx >= 0) ? g_profilo.steps[sidx].nome : "";
  dd.ore_step               = ore_st;
  dd.ore_totali             = ore_tot;
  dd.progresso_pct          = prog_pct;
  dd.giorni_prossimo_step   = giorni_next;
  dd.profilo_nome           = g_profilo.valido ? g_profilo.nome : "nessuno";
  dd.riscaldatore_on        = uscite_riscaldatore_acceso();
  dd.refrigeratore_on       = uscite_refrigeratore_acceso();
  dd.wifi_offline           = (WiFi.status() != WL_CONNECTED);
  const String ssid_str     = WiFi.SSID();
  strncpy(dd.wifi_ssid, ssid_str.c_str(), sizeof(dd.wifi_ssid) - 1);
  dd.wifi_ssid[sizeof(dd.wifi_ssid) - 1] = '\0';
  dd.ts_ingresso_allarme_ms = g_ts_alarm_start;

  display_aggiorna(dd);
}

// ============================================================
// Rete: avvia NTP + OTA dopo connessione WiFi riuscita
// ============================================================
static void configure_wifi_driver_logging() {
  // Runtime log level for WiFi stack tags.
  // Note: full output also requires Arduino IDE Core Debug Level >= Verbose.
  esp_log_level_set("wifi", ESP_LOG_VERBOSE);
  esp_log_level_set("net80211", ESP_LOG_VERBOSE);
  esp_log_level_set("wpa", ESP_LOG_VERBOSE);
}

static void setup_network_services() {
  configTime(NTP_OFFSET, NTP_DST, NTP_SERVER);

  if (g_ota_avviato) return;
  g_ota_avviato = true;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    // Ferma output relè prima del flash OTA
    controller_stop_outputs(g_controller);
    spegni_uscite_subito();
  });
  ArduinoOTA.onProgress([](unsigned int, unsigned int) {
    // L'upload blocca il loop per decine di secondi
    esp_task_wdt_reset();
  });
  ArduinoOTA.begin();
}

// ============================================================
// Rete: gestione non-bloccante WiFi + MQTT nel loop
// ============================================================
static void handle_network(unsigned long now) {
  const wl_status_t wifi_status = WiFi.status();

  if (wifi_status == WL_CONNECTED) {
    if (!g_wifi_connesso) {
      g_wifi_connesso = true;
      setup_network_services();
      LOG_I("WIFI", "Connesso a %s IP=%s RSSI=%d",
            WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
    g_ts_wifi_perso     = 0;
    g_tentativi_riconn  = 0;
    g_tentativi_hotspot = 0;

    ArduinoOTA.handle();

    if (connettivita_mqtt_connesso()) {
      connettivita_mqtt_loop();
      return;
    }

    if (g_mqtt_era_connesso) {
      g_mqtt_era_connesso = false;
      LOG_W("MQTT", "Connessione al broker persa (stato=%d)", connettivita_mqtt_stato());
    }
    if (g_ts_last_mqtt_attempt != 0 && now - g_ts_last_mqtt_attempt < MQTT_CONNECT_TIMEOUT_MS) {
      return;
    }
    g_ts_last_mqtt_attempt = now;
    esp_task_wdt_reset();
    if (connettivita_mqtt_connetti()) {
      g_mqtt_era_connesso = true;
      LOG_I("MQTT", "Connesso al broker");
      // Prima pubblicazione dopo (ri)connessione
      pubblica_stato(millis());
    } else {
      LOG_W("MQTT", "Connessione fallita (stato=%d)", connettivita_mqtt_stato());
    }
    return;
  }

  // WiFi assente: tentativi non bloccanti, burst iniziale poi ogni WIFI_RECONNECT_INTERVAL_MS.
  // Prima: attesa bloccante di 8 s per tentativo e pausa di 20 min dopo 3 tentativi.
  if (g_wifi_connesso) {
    g_wifi_connesso = false;
    g_mqtt_era_connesso = false;
    LOG_W("WIFI", "Connessione persa (stato=%d)", (int)wifi_status);
  }
  if (g_ts_wifi_perso == 0) {
    g_ts_wifi_perso = now;
    g_ts_last_wifi_attempt = now;  // lascia prima lavorare l'auto-reconnect del driver
    return;
  }

  const unsigned long intervallo = (g_tentativi_riconn < 3) ? HOTSPOT_RECONNECT_INTERVAL_MS
                                                             : WIFI_RECONNECT_INTERVAL_MS;
  if (now - g_ts_last_wifi_attempt < intervallo) return;
  // Tentativo precedente ancora in corso
  if (wifi_status == WL_IDLE_STATUS && now - g_ts_last_wifi_attempt < WIFI_TENTATIVO_TIMEOUT_MS) return;

  g_ts_last_wifi_attempt = now;
  g_tentativi_riconn++;

  // Hotspot OTA spento dopo l'aggiornamento: prima l'ESP lo cercava per sempre
  // e restava offline fino al riavvio.
  if (g_via_hotspot && ++g_tentativi_hotspot > HOTSPOT_TENTATIVI_MAX) {
    g_via_hotspot = false;
    LOG_W("WIFI", "Hotspot '%s' non disponibile: ripiego su '%s'", HOTSPOT_SSID, WIFI_SSID);
  }

  const char* ssid = g_via_hotspot ? HOTSPOT_SSID : WIFI_SSID;
  LOG_I("WIFI", "Tentativo riconnessione #%d SSID=%s", g_tentativi_riconn, ssid);
  WiFi.disconnect(false, false);
  if (g_via_hotspot) {
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
  } else {
    WiFi.begin(WIFI_SSID, getWifiPassword().c_str());
  }
}

void setup() {
  // Legge i dati del reset precedente (memoria RTC) prima che qualunque altra cosa li sovrascriva:
  // pochi microsecondi, poi i relè.
  diag_reset_init();
  // Relè OFF prima di qualunque altra cosa.
  uscite_init();

  Serial.begin(115200);
  delay(200);
  watchdog_init();
  configure_wifi_driver_logging();
  Serial.println("[WIFI] ESP log tag level set to VERBOSE (wifi/net80211/wpa)");
  Serial.println("[WIFI] In Arduino IDE set Tools > Core Debug Level = Verbose for full WiFi logs");
  LOG_I("BOOT", "Fermentatore v6 fw=%s device=%s reset=%s",
        FIRMWARE_VERSION, DEVICE_FERMENTER_ID, reset_reason_str());
  {
    const DiagReset& d = diag_reset_precedente();
    LOG_I("DIAG", "Reset precedente: hw=%u %s valida=%d giro=%ld ms comm=%ld ms (%s %s) sezione=%s loop_max=%lu ms",
          (unsigned)d.hw_codice, d.hw_nome, d.valida ? 1 : 0, (long)d.giro_ms, (long)d.comm_ms,
          diag_reset_nome_rele(d.comm_rele), d.comm_acceso ? "on" : "off",
          diag_reset_nome_sezione(d.sezione), (unsigned long)d.loop_max_ms);
  }

  const esp_reset_reason_t rr = esp_reset_reason();
  if (g_rtc_magic != RTC_MAGIC || rr == ESP_RST_POWERON || rr == ESP_RST_BROWNOUT) {
    g_rtc_magic = RTC_MAGIC;
    g_rtc_riavvii_sensore = 0;
  }

  display_init();
  sensor_power_cycle_init();
  sensor_init(g_sensore);

  if (caricaConfigControllo(g_config)) {
    LOG_I("NVS", "Configurazione controllo ripristinata: modo=%s", controller_mode_to_string(g_config.mode));
  } else {
    LOG_I("NVS", "Nessuna configurazione controllo salvata: modo=%s", controller_mode_to_string(g_config.mode));
  }
  controller_init(g_controller, g_config);
  autotune_init(g_autotune);
  stop_run();

  // --- Registra callback comandi e inizializza stack MQTT ---
  connettivita_mqtt_init(process_command_message);
  mqtt_imposta_nome_profilo(g_profilo.nome);
  log_init(mqtt_pubblica_log);

  // --- NVS: ripristina profilo + stato FSM prima della connessione WiFi (§8 boot autonomy) ---
  {
    if (caricaProfilo(g_profilo)) {
      aggiorna_setpoint_da_profilo(false);

      char fsm_buf[24] = {};
      int  saved_step  = 0;
      float saved_ore  = 0.0f;
      const bool has_fsm = caricaStatoFsm(fsm_buf, sizeof(fsm_buf), saved_step, saved_ore);
      const bool resume  = has_fsm &&
                           (strcmp(fsm_buf, "RUNNING") == 0 || strcmp(fsm_buf, "HOLDING") == 0);
      if (resume) {
        LOG_I("NVS", "Profilo '%s' ripristinato da NVS, stato=%s setpoint=%.2f ore=%.1f",
              g_profilo.nome, fsm_buf, g_setpoint, saved_ore);
        start_run();
      } else {
        LOG_I("NVS", "Profilo '%s' trovato in NVS ma stato=%s: rimane STOPPED",
              g_profilo.nome, has_fsm ? fsm_buf : "(nessuno)");
      }
    } else {
      LOG_I("NVS", "Nessun profilo in NVS");
    }
  }

  // --- WiFi: scan-first — hotspot OTA ha priorità sulla rete standard ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, false);
  delay(100);
  const String wifi_mac = WiFi.macAddress();

  // Scansione reti: se HOTSPOT_SSID è visibile si connette a quello (OTA prioritario)
  LOG_I("WIFI", "Scansione reti (ricerca hotspot OTA '%s')...", HOTSPOT_SSID);
  const int n_reti = WiFi.scanNetworks();  // bloccante, ~2-4 s, solo all'avvio
  g_via_hotspot = false;
  for (int i = 0; i < n_reti; i++) {
    if (WiFi.SSID(i) == HOTSPOT_SSID) {
      g_via_hotspot = true;
      break;
    }
  }
  WiFi.scanDelete();

  if (g_via_hotspot) {
    LOG_I("WIFI", "Hotspot OTA trovato - connessione a '%s'...", HOTSPOT_SSID);
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
  } else {
    LOG_I("WIFI", "Hotspot non trovato - connessione a '%s' MAC=%s...", WIFI_SSID, wifi_mac.c_str());
    WiFi.begin(WIFI_SSID, getWifiPassword().c_str());
  }
  {
    const unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
      delay(250);
      esp_task_wdt_reset();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    g_wifi_connesso = true;
    setup_network_services();
    LOG_I("WIFI", "Connesso a %s IP=%s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    esp_task_wdt_reset();
    g_ts_last_mqtt_attempt = millis();
    g_mqtt_era_connesso = connettivita_mqtt_connetti();
  } else {
    LOG_W("WIFI", "Connessione WiFi fallita al boot");
  }

  g_ms_inizio_loop = millis();
  diag_reset_boot_completato();
}

void loop() {
  esp_task_wdt_reset();
  const unsigned long now = millis();

  // Le chiamate diag_reset_* misurano quanto dura ogni sezione e lasciano tracce in memoria RTC
  // (vedi diag_reset.h): non cambiano il comportamento del loop.
  diag_reset_sezione(SEZ_SERIALE);
  process_serial_commands();
  diag_reset_sezione(SEZ_RETE);
  handle_network(now);
  diag_reset_sezione(SEZ_SENSORE);
  handle_sensor_logic(now);
  diag_reset_sezione(SEZ_DISPLAY);
  handle_display(now);

  diag_reset_sezione(SEZ_CONTROLLO);
  const bool temp_ok = temperatura_utilizzabile();

  if (now - g_last_control_ms >= CONTROL_INTERVAL_MS) {
    g_last_control_ms = now;

    // --- Avanzamento del profilo e aggiornamento del setpoint (RUNNING → HOLDING a fine profilo) ---
    avanza_profilo(now, temp_ok);
    aggiorna_setpoint_da_profilo(true);

    if (g_controller.runtime_state == RuntimeState::AUTOTUNING) {
      autotune_tick(g_autotune, g_temp, now);
      if (now - g_last_autotune_pub_ms >= 2000UL) {
        g_last_autotune_pub_ms = now;
        mqtt_pubblica_autotune_state(g_autotune, g_temp);
      }
      if (g_autotune.state == AutotuneState::COMPLETED || g_autotune.state == AutotuneState::ABORTED) {
        stop_run();
        if (g_autotune.state == AutotuneState::COMPLETED) {
          LOG_I("AUTOTUNE", "Completato: q=%.2f dur=%lus kp_h=%.3f",
                g_autotune.result.quality_score,
                (unsigned long)g_autotune.result.duration_s,
                g_autotune.result.kp_heat);
        } else {
          LOG_W("AUTOTUNE", "Abortito");
        }
        mqtt_pubblica_autotune_result(g_autotune);
      }
    }

    if (!temp_ok) {
      controller_stop_outputs(g_controller);
      g_controller.ts_calc_ms = 0;  // evita un dt enorme sull'integrale alla ripresa
      g_controller.misura_prec_valida = false;
    }
  }

  // Il controllore ricalcola a ogni nuova lettura valida (ogni SENSOR_READ_INTERVAL_MS):
  // dt coerente per I e D, nessun calcolo ripetuto sulla stessa misura.
  if (g_nuova_lettura) {
    g_nuova_lettura = false;
    if (temp_ok) {
      controller_apply_inputs(g_controller, g_config, g_temp, g_setpoint, now);
    }
  }

  // Stadio uscite a ogni loop: risoluzione fine della finestra duty e spegnimenti immediati.
  diag_reset_sezione(SEZ_USCITE);
  const RichiestaUscite richiesta = {
    g_controller.heat_on,
    g_controller.cool_on,
    g_controller.heat_duty_pct,
    g_controller.cool_duty_pct
  };
  const bool forza_spegnimento = !temp_ok || g_controller.runtime_state != RuntimeState::RUNNING;
  uscite_aggiorna(richiesta, forza_spegnimento, g_temp, setpoint_limitato(), now);

  diag_reset_sezione(SEZ_HEARTBEAT);
  handle_heartbeat(now);
  diag_reset_sezione(SEZ_NVS);
  handle_persistenza(now);
  diag_reset_giro();
}
