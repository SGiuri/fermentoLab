// ============================================================
// Fermentatore V4 — Firmware ESP32
// Termostato autonomo a macchina a stati.
//
// NOVITÀ RISPETTO A V3:
//   - Supporto OTA via hotspot personale.
//     All'avvio l'ESP32 scansiona le reti disponibili:
//     • Se trova HOTSPOT_SSID (config.h) → si connette a quella rete,
//       avvia ArduinoOTA e opera normalmente (MQTT, ecc.).
//     • Se non trova HOTSPOT_SSID → si connette a WIFI_SSID come in V3.
//     Per aggiornare il firmware: attiva l'hotspot, riavvia l'ESP32,
//     connetti il PC alla stessa rete hotspot, usa Arduino IDE OTA.
//
// FLUSSO DI AVVIO:
//
//   [ACCENSIONE]
//        │
//        ▼
//   [WIFI_CONNESSIONE] — scan hotspot → hotspot trovato? ─┐
//        │ hotspot non trovato → rete standard             │
//        │ OK (qualunque rete)                             │ OTA avviato
//        ▼                                                 │
//   [MQTT_CONNESSIONE] ── timeout ──► verifica NVS ──► RUNNING / STANDBY
//        │ OK
//        ▼
//   [RICHIESTA_PROFILO]  ← pubblica su fermentazione/f1/richiedi_profilo
//        │                  HA risponde su fermentazione/f1/cmd/profilo
//        ├── profilo ricevuto ──────────────────────────► [RUNNING]
//        ├── "nessun_profilo" / timeout ─► verifica NVS ► [RUNNING] / [STANDBY]
//        │
//   [RUNNING]  calcola setpoint, controlla relè, ogni 60s → MQTT stato
//        ├── profilo completato ────────────────────────► [HOLDING]
//        └── CMD stop ─────────────────────────────────► [STANDBY]
//
//   [STANDBY]  mantiene temperatura standard, attende nuovo profilo
//        └── CMD profilo ───────────────────────────────► [RUNNING]
//
//   [HOLDING]  mantiene ultima temperatura profilo fino a CMD stop
//        ├── CMD stop ─────────────────────────────────► [STANDBY]
//        └── CMD profilo ───────────────────────────────► [RUNNING]
//
//   [ALLARME_SENSORE]  relè off, triggera da qualsiasi stato operativo
//        └── sensore ripristinato ──────────────────────► stato precedente
//
// Librerie richieste (Arduino Library Manager):
//   - PubSubClient       (Nick O'Leary)
//   - ArduinoJson        (Benoit Blanchon)  v6.x
//   - OneWire            (Paul Stoffregen)
//   - DallasTemperature  (Miles Burton)
//   - U8g2               (Oliver Kraus)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h>

#include "config.h"
#include "connettivita.h"
#include "profilo.h"
#include "termostato.h"
#include "duty_cycle.h"
#include "sensor_watchdog.h"
#include "log.h"
#include "display.h"

// ============================================================
// Macchina a stati — definizione
// ============================================================
enum class Stato : uint8_t {
  WIFI_CONNESSIONE  = 0,
  MQTT_CONNESSIONE,
  RICHIESTA_PROFILO,
  RUNNING,           // profilo attivo, calcola setpoint, controlla relè
  STANDBY,           // nessun profilo, mantiene temperatura standard
  HOLDING,           // profilo completato, mantiene ultima temperatura
  ALLARME_SENSORE    // sensore guasto, relè off, attende ripristino
};

static const char* stato_str(Stato s) {
  switch (s) {
    case Stato::WIFI_CONNESSIONE:  return "WIFI";
    case Stato::MQTT_CONNESSIONE:  return "MQTT";
    case Stato::RICHIESTA_PROFILO: return "RICHIESTA_PROFILO";
    case Stato::RUNNING:           return "RUNNING";
    case Stato::STANDBY:           return "STANDBY";
    case Stato::HOLDING:           return "HOLDING";
    case Stato::ALLARME_SENSORE:   return "ALLARME";
    default:                       return "?";
  }
}

// ============================================================
// Variabili globali
// ============================================================
static Stato                g_stato              = Stato::WIFI_CONNESSIONE;
static bool                 g_primo_tick         = true;  // primo loop() in questo stato
static unsigned long        g_ts_stato_ingresso  = 0;

static ProfiloFermentazione g_profilo    = {};
static StatoTermostato      g_termostato = {};
static SensorState          g_sensore    = {};
static EventiMQTT           g_eventi     = {};

static float  g_temp_corrente    = 20.0f;
static float  g_temp_controllo   = 20.0f;
static float  g_setpoint         = DEFAULT_STANDBY_TEMP;
static float  g_temp_standby     = DEFAULT_STANDBY_TEMP;
static bool   g_completato_notif = false;
static bool   g_temp_valida      = true;
static bool   g_duty_fallback_attivo = false;
// Debounce uscita fallback: conta letture valide consecutive dopo un guasto sensore.
// Inizializzato a SENSOR_VALID_STREAK_MIN: nessun debounce al primo avvio.
static uint8_t g_sensore_valid_streak = SENSOR_VALID_STREAK_MIN;
// Power cycle DS18B20: timestamp primo segno di instabilità (0 = stabile) e contatore tentativi.
static unsigned long g_ts_sensore_instabile  = 0;
static uint8_t       g_power_cycle_tentativi = 0;
static unsigned long g_ts_wifi_perso          = 0;  // millis() al blackout WiFi; 0 = connesso OK
static unsigned long g_ts_ultima_riconnessione = 0;  // millis() dell'ultimo WiFi.reconnect()
static int           g_tentativi_riconn        = 0;  // tentativi usati nella serie corrente
static unsigned long g_ts_ciclo_riconn         = 0;  // inizio dell'ultimo ciclo di 3 tentativi

// Flag: true se connesso via hotspot OTA (anziché rete standard)
static bool   g_via_hotspot = false;
// Flag: true dopo che il burst iniziale di 3 tentativi rapidi su hotspot è esaurito;
// in seguito si usa il ciclo standard (3 × 30s + 20 min).
static bool   g_hotspot_burst_done = false;
// Flag event-driven: settato dalla callback del driver WiFi (task separato) al momento
// esatto della perdita dell'associazione 802.11 — molto più rapido di WiFi.status()
// che può restare WL_CONNECTED per decine di secondi dopo che l'AP è sparito.
static volatile bool g_wifi_disconnesso_evento = false;
// SSID della rete su cui si è connessi (vuota se mai connessi)
static char   g_wifi_ssid[33] = {};

static unsigned long g_ts_ultimo_stato      = 0;
static unsigned long g_ts_ultima_lettura    = 0;
static unsigned long g_ts_richiesta_profilo = 0;
static unsigned long g_ts_ultimo_display    = 0;

// Il fallback duty cycle si attiva quando il sensore non è valido o non ha ancora
// raggiunto SENSOR_VALID_STREAK_MIN letture valide consecutive dopo un guasto.
// Il debounce evita il rimbalzo on/off quando il sensore restituisce valori
// numericamente plausibili ma fisicamente errati (es. 8°C invece di 21°C)
// che superano temp_valida() ma non ancora oscillazione_incoerente().
static bool fallback_duty_attivo() {
  return !g_temp_valida || g_sensore_valid_streak < SENSOR_VALID_STREAK_MIN;
}

static float aggiorna_temp_controllo(float temp_reale, bool temp_valida) {
  if (!temp_valida) return g_temp_controllo;

  float alpha = DEFAULT_CONTROL_FILTER_ALPHA;
  if (alpha <= 0.0f) return temp_reale;
  if (alpha >= 1.0f) {
    g_temp_controllo = temp_reale;
    return g_temp_controllo;
  }

  g_temp_controllo += alpha * (temp_reale - g_temp_controllo);
  return g_temp_controllo;
}

static void applica_controllo_con_fallback(unsigned long ora,
                                           float setpoint,
                                           float isteresi_ctrl,
                                           uint32_t min_cycle_ctrl,
                                           float anticip_frigo_ctrl) {
  bool usa_fallback = fallback_duty_attivo();
  float temp_ctrl = g_temp_controllo;

  if (usa_fallback) {
    // prefer_risc/frigo: guida il duty cycle su quale canale usare (solo se sensore valido)
    bool prefer_risc  = g_temp_valida && (temp_ctrl < (setpoint - isteresi_ctrl));
    bool prefer_frigo = g_temp_valida && (temp_ctrl > (setpoint + isteresi_ctrl));
    DutyCycleStats stats = {};
    bool applicato = duty_cycle_applica(g_termostato, ora, prefer_risc, prefer_frigo,
                                        min_cycle_ctrl, &stats);
    if (!applicato) {
      // Storico < 10 min: se il sensore funziona usa l'isteresi come bridge
      // (evita sia lo spegnimento completo che il chattering a pochi secondi).
      // Se anche il sensore è ko → spegni per sicurezza.
      if (g_temp_valida) {
        termostato_aggiorna(g_termostato, temp_ctrl, setpoint,
                            isteresi_ctrl, min_cycle_ctrl, anticip_frigo_ctrl);
      } else {
        termostato_spegni_tutto(g_termostato);
      }
    }

    if (!g_duty_fallback_attivo) {
      bool _rete_ok = (WiFi.status() == WL_CONNECTED) && connettivita_mqtt_connesso();
      LOG_W("DUTY", "Fallback attivato [%s%s] duty=%s (osservazione=%lus, heat=%.1f%% cool=%.1f%%).",
            !_rete_ok ? "rete_assente " : "",
            !g_temp_valida ? "sensore_ko" : "",
            applicato ? "SI" : "no (<10min storia - uso isteresi)",
            (unsigned long)(stats.osservazione_ms / 1000UL),
            stats.duty_riscaldamento * 100.0f,
            stats.duty_refrigerazione * 100.0f);
      g_duty_fallback_attivo = true;
    }
    return;
  }

  if (g_duty_fallback_attivo) {
    LOG_I("DUTY", "Fallback disattivato: rete e sensore tornati affidabili.");
    g_duty_fallback_attivo = false;
  }

  termostato_aggiorna(g_termostato, temp_ctrl, setpoint,
                      isteresi_ctrl, min_cycle_ctrl, anticip_frigo_ctrl);
}

// ============================================================
// Cambio stato
// ============================================================
static void entra_stato(Stato nuovo) {
  LOG_D("FSM", "%-18s -> %s", stato_str(g_stato), stato_str(nuovo));
  g_stato             = nuovo;
  g_ts_stato_ingresso = millis();
  g_primo_tick        = true;
  g_ts_ultimo_display = 0;  // forza refresh display immediato al prossimo loop
}

// ============================================================
// Helpers
// ============================================================
static bool ntp_ok() {
  time_t t; time(&t);
  return t > 1000000000L;
}

static uint32_t secs_dall_avvio() {
  if (!g_profilo.valido || g_profilo.avvio_epoch == 0) return 0;

  time_t epoch_corrente = 0;

  if (ntp_ok()) {
    // Caso normale: NTP disponibile, calcolo esatto
    time(&epoch_corrente);
  } else if (g_profilo.ultimo_epoch_noto > 0) {
    // Fallback offline: usiamo l'ultimo epoch salvato in NVS prima del riavvio.
    // Errore massimo = tempo_tra_heartbeat (60s) + tempo_boot (~10s) ≈ 70s
    // su fermentazioni di giorni/settimane: errore trascurabile.
    LOG_W("NTP", "Offline - uso epoch NVS come fallback.");
    epoch_corrente = (time_t)g_profilo.ultimo_epoch_noto;
  } else {
    return 0;  // nessun riferimento temporale disponibile
  }

  long diff = (long)epoch_corrente - (long)g_profilo.avvio_epoch;
  return (diff > 0) ? (uint32_t)diff : 0;
}

// Parametri relè con fallback ai default se il profilo non è caricato
static float    isteresi()   { return (g_profilo.valido && g_profilo.isteresi > 0.0f) ? g_profilo.isteresi : DEFAULT_ISTERESI; }
static uint32_t min_cycle()  { return (g_profilo.valido && g_profilo.min_cycle_sec > 0) ? g_profilo.min_cycle_sec : DEFAULT_MIN_CYCLE_SEC; }

// Durata totale pianificata del profilo
static float durata_tot_ore() {
  float tot = 0.0f;
  for (int i = 0; i < g_profilo.num_steps; i++)
    tot += g_profilo.steps[i].is_ramp ? g_profilo.steps[i].ramp_hours
                                      : g_profilo.steps[i].duration_hours;
  return tot;
}

// Avvia RUNNING usando i dati del profilo corrente (da NVS o da HA)
static void avvia_running() {
  g_setpoint       = calcolaSetpoint(g_profilo, secs_dall_avvio());
  g_completato_notif = false;
  entra_stato(Stato::RUNNING);
}

// Gestione connettività best-effort (non bloccante).
//
// Rete standard (g_via_hotspot == false):
//   Serie di 3 tentativi (ogni WIFI_RECONNECT_INTERVAL_MS = 30s),
//   poi pausa WIFI_RECONNECT_CYCLE_MS (20 min), ripete.
//
// Rete hotspot OTA (g_via_hotspot == true):
//   1. Alla disconnessione: 3 tentativi rapidi (HOTSPOT_RECONNECT_INTERVAL_MS = 5s).
//   2. Se tutti falliscono: offline NVS, pausa WIFI_RECONNECT_CYCLE_MS (20 min).
//   3. Poi: ciclo standard (3 x 30s, pausa 20 min, ripete).
static void mantieni_connessione() {
  unsigned long ora = millis();

  // WiFi.status() può essere stantio: usa il flag event-driven come criterio primario.
  // Il flag viene settato dal driver 802.11 appena perde l'associazione (~500ms),
  // e azzerato alla ricezione dell'IP dalla nuova connessione.
  if (WiFi.status() == WL_CONNECTED && !g_wifi_disconnesso_evento) {
    if (g_ts_wifi_perso != 0) {
      LOG_I("WiFi", "Connessione ripristinata dopo %lu min.", (ora - g_ts_wifi_perso) / 60000UL);
    }
    g_ts_wifi_perso           = 0;
    g_ts_ultima_riconnessione = 0;
    g_tentativi_riconn        = 0;
    g_ts_ciclo_riconn         = 0;
    g_hotspot_burst_done      = false;
    if (!connettivita_mqtt_connesso()) connettivita_mqtt_connetti();
    connettivita_mqtt_loop();
    return;
  }

  // Primo rilevamento blackout
  if (g_ts_wifi_perso == 0) {
    g_ts_wifi_perso           = ora;
    g_ts_ciclo_riconn         = ora;
    g_tentativi_riconn        = 0;
    g_ts_ultima_riconnessione = 0;
    g_hotspot_burst_done      = false;
    if (g_via_hotspot) {
      LOG_W("WiFi", "Hotspot perso - 3 tentativi rapidi (%lus), poi offline NVS + ciclo standard.",
            HOTSPOT_RECONNECT_INTERVAL_MS / 1000UL);
    } else {
      LOG_W("WiFi", "Blackout WiFi - termostato on/off attivo. Serie 3 riconn. ogni 20 min.");
    }
  }

  // ── Burst rapido hotspot (solo prima della serie standard) ──────────────────
  if (g_via_hotspot && !g_hotspot_burst_done) {
    if (g_tentativi_riconn < 3) {
      unsigned long attesa = (g_tentativi_riconn == 0) ? 0UL : HOTSPOT_RECONNECT_INTERVAL_MS;
      if (ora - g_ts_ultima_riconnessione >= attesa) {
        g_ts_ultima_riconnessione = ora;
        g_tentativi_riconn++;
        LOG_W("WiFi", "Hotspot riconn. rapida %d/3...", g_tentativi_riconn);
        WiFi.reconnect();
      }
    } else {
      // Burst esaurito: offline NVS, poi ciclo standard dopo 20 min
      g_hotspot_burst_done      = true;
      g_tentativi_riconn        = 0;
      g_ts_ciclo_riconn         = ora;
      g_ts_ultima_riconnessione = 0;
      LOG_W("WiFi", "Hotspot: burst fallito - offline NVS, ciclo standard tra %lu min.",
            WIFI_RECONNECT_CYCLE_MS / 60000UL);
    }
    return;
  }

  // ── Ciclo standard (rete IoT, oppure hotspot dopo il burst) ────────────────
  if (g_tentativi_riconn < 3) {
    unsigned long attesa = (g_tentativi_riconn == 0) ? 0UL : WIFI_RECONNECT_INTERVAL_MS;
    if (ora - g_ts_ultima_riconnessione >= attesa) {
      g_ts_ultima_riconnessione = ora;
      g_tentativi_riconn++;
      LOG_W("WiFi", "Riconn. %d/3 (blackout %lu min)...",
            g_tentativi_riconn, (ora - g_ts_wifi_perso) / 60000UL);
      WiFi.reconnect();
    }
  } else {
    if (ora - g_ts_ciclo_riconn >= WIFI_RECONNECT_CYCLE_MS) {
      g_ts_ciclo_riconn         = ora;
      g_tentativi_riconn        = 0;
      g_ts_ultima_riconnessione = 0;
      LOG_W("WiFi", "Nuovo ciclo riconn. (blackout %lu min).", (ora - g_ts_wifi_perso) / 60000UL);
    }
  }
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  // Registra callback MQTT per il logger.
  // Serial è subito disponibile; MQTT lo sarà dopo la connessione.
  log_init(mqtt_pubblica_log);
  LOG_I("BOOT", "=== Fermentatore V4 - Avvio ===");

  // Display OLED: init prima del resto per mostrare subito feedback visivo
  display_init();

  // Hardware in stato sicuro prima di tutto il resto
  termostato_init(g_termostato);
  sensor_power_cycle_init();  // configura GPIO25 (PIN_SENSOR_PWR) come output, HIGH
  sensor_init(g_sensore);
  duty_cycle_init();

  // Event handler WiFi: il driver 802.11 notifica disconnessione/riconnessione
  // immediatamente (beacon loss ~500ms), prima che WiFi.status() si aggiorni.
  // Callback chiamata dal task WiFi → flag volatile, scrittura atomica su Xtensa.
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    g_wifi_disconnesso_evento = true;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
    g_wifi_disconnesso_evento = false;
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  entra_stato(Stato::WIFI_CONNESSIONE);
}

// ============================================================
// loop() — macchina a stati
// ============================================================
void loop() {
  unsigned long ora = millis();

  // Preleva e azzera il flag "primo tick" per questo stato
  bool primo_tick = g_primo_tick;
  g_primo_tick    = false;
  g_eventi.ping_ricevuto = false;  // pong già inviato nella callback, flag non usato dalla FSM

  // ---- OTA handle (non bloccante, solo se connesso via hotspot) ----
  if (g_via_hotspot && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  // Test offline: salva flag NVS → riavvio con password WiFi sbagliata → esercizio fallback duty cycle
  if (g_eventi.test_no_wifi_ricevuto) {
    g_eventi.test_no_wifi_ricevuto = false;
    termostato_spegni_tutto(g_termostato);
    LOG_W("CMD", "Ricevuto cmd/test_no_wifi - riavvio in modalita' test offline.");
    {
      Preferences _prefs;
      _prefs.begin(NVS_NS_FERM, false);
      _prefs.putBool(NVS_KEY_TEST_NO_WIFI, true);
      _prefs.end();
    }
    if (connettivita_mqtt_connesso()) {
      mqtt_pubblica_ack("test_no_wifi_avviato", g_profilo.valido ? g_profilo.nome : "");
      delay(500);  // flush MQTT
    }
    ESP.restart();
  }

  // Riavvio remoto: esecuzione nel loop (mai nella callback MQTT)
  if (g_eventi.reboot_ricevuto) {
    g_eventi.reboot_ricevuto = false;
    termostato_spegni_tutto(g_termostato);
    LOG_W("CMD", "Ricevuto cmd/reboot remoto.");
    if (connettivita_mqtt_connesso()) {
      mqtt_pubblica_ack("riavvio_remoto", g_profilo.valido ? g_profilo.nome : "");
      mqtt_pubblica_allarme("riavvio_in_corso", 0);
    }
    delay(500);  // flush MQTT
    ESP.restart();
  }

  // ---- Lettura sensore (ogni SENSOR_READ_INTERVAL_MS, indipendente dallo stato) ----
  if (ora - g_ts_ultima_lettura >= SENSOR_READ_INTERVAL_MS) {
    g_ts_ultima_lettura = ora;

    uint32_t timeout_min = (g_profilo.valido && g_profilo.allarme_timeout_minuti > 0)
                             ? g_profilo.allarme_timeout_minuti : 5;
    g_temp_valida = sensor_leggi(g_sensore, g_temp_corrente, timeout_min);
    // Aggiorna il contatore di debounce: un guasto azzera la streak; una lettura
    // valida la incrementa fino a SENSOR_VALID_STREAK_MIN (25s con 5s di intervallo).
    if (g_temp_valida) {
      if (g_sensore_valid_streak < SENSOR_VALID_STREAK_MIN) g_sensore_valid_streak++;
    } else {
      g_sensore_valid_streak = 0;
    }
    aggiorna_temp_controllo(g_temp_corrente, g_temp_valida);

    // Timer instabilità: avvia quando il sensore non raggiunge la streak minima,
    // azzera (con contatore tentativi) quando torna stabile.
    bool _sensore_stabile = g_temp_valida && (g_sensore_valid_streak >= SENSOR_VALID_STREAK_MIN);
    if (_sensore_stabile) {
      g_ts_sensore_instabile  = 0;
      g_power_cycle_tentativi = 0;
    } else if (g_ts_sensore_instabile == 0) {
      g_ts_sensore_instabile = ora;  // primo segno di instabilità: avvia timer
    }

    // Power cycle proattivo: se il sensore è instabile da troppo tempo e non è ancora in
    // ALLARME_SENSORE, prova a ciclare il power prima che scatti l'allarme.
    if (g_ts_sensore_instabile > 0 &&
        (ora - g_ts_sensore_instabile) >= SENSOR_POWER_CYCLE_TIMEOUT_MS &&
        g_power_cycle_tentativi < SENSOR_POWER_CYCLE_MAX_ATTEMPTS) {
      g_power_cycle_tentativi++;
      LOG_W("SENSORE", "Instabile da %lu min - power cycle %d/%d.",
            (ora - g_ts_sensore_instabile) / 60000UL,
            g_power_cycle_tentativi, SENSOR_POWER_CYCLE_MAX_ATTEMPTS);
      if (sensor_power_cycle(g_sensore)) {
        g_temp_corrente         = g_sensore.ultima_temp_valida;
        g_temp_valida           = true;
        g_sensore_valid_streak  = SENSOR_VALID_STREAK_MIN;
        g_ts_sensore_instabile  = 0;
        g_power_cycle_tentativi = 0;
        aggiorna_temp_controllo(g_temp_corrente, true);
        LOG_I("SENSORE", "Power cycle riuscito: T=%.2f grC.", g_temp_corrente);
      } else {
        g_ts_sensore_instabile = ora;  // reset timer per il prossimo tentativo
        LOG_W("SENSORE", "Power cycle fallito (%d/%d).",
              g_power_cycle_tentativi, SENSOR_POWER_CYCLE_MAX_ATTEMPTS);
      }
    }

    // Ingresso in ALLARME_SENSORE (solo dagli stati operativi)
    if (sensor_allarme_nuovo(g_sensore)) {
      switch (g_stato) {
        case Stato::RUNNING:
        case Stato::STANDBY:
        case Stato::HOLDING:
          termostato_spegni_tutto(g_termostato);
          LOG_E("SENSORE", "Allarme: DS18B20 offline da %d min.",
                (int)((ora - g_sensore.ts_primo_errore_ms) / 60000UL));
          if (connettivita_mqtt_connesso())
            mqtt_pubblica_allarme("sensore_offline",
              (int)((ora - g_sensore.ts_primo_errore_ms) / 60000UL));
          entra_stato(Stato::ALLARME_SENSORE);
          break;
        default: break;
      }
    }
  }

  // ---- Macchina a stati ----
  switch (g_stato) {

    // ----------------------------------------------------------
    // WIFI_CONNESSIONE
    // ----------------------------------------------------------
    case Stato::WIFI_CONNESSIONE:
      if (primo_tick) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true, true);  // reset stato precedente (NVS credenziali + connessione)
        delay(200);

        // Controlla flag test_no_wifi: se attivo usa password sbagliata e auto-cancella il flag
        bool _test_no_wifi = false;
        {
          Preferences _prefs;
          _prefs.begin(NVS_NS_FERM, false);
          _test_no_wifi = _prefs.getBool(NVS_KEY_TEST_NO_WIFI, false);
          if (_test_no_wifi) { _prefs.putBool(NVS_KEY_TEST_NO_WIFI, false); }  // auto-cancella
          _prefs.end();
        }

        if (_test_no_wifi) {
          // Test offline: salta la scansione hotspot e usa password sbagliata
          g_via_hotspot = false;
          WiFi.begin(WIFI_SSID, "WRONG_TEST_PWD_XXXX");
          LOG_W("WiFi", "TEST_NO_WIFI attivo: password intenzionalmente sbagliata - esercizio fallback offline.");

        } else {
          // Scansione reti per rilevare l'hotspot OTA personale
          LOG_I("WiFi", "Scansione reti (ricerca hotspot OTA '%s')...", HOTSPOT_SSID);
          int n = WiFi.scanNetworks();  // bloccante, ~2-4s, solo all'avvio
          g_via_hotspot = false;
          for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == HOTSPOT_SSID) {
              g_via_hotspot = true;
              break;
            }
          }
          WiFi.scanDelete();

          if (g_via_hotspot) {
            WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
            LOG_I("WiFi", "Hotspot OTA trovato - connessione a '%s'...", HOTSPOT_SSID);
          } else {
            String pwd = getWifiPassword();
            if (pwd.isEmpty()) {
              LOG_E("WiFi", "MAC %s non in lista - aggiungilo in wifi_connection.ino", WiFi.macAddress().c_str());
            }
            WiFi.begin(WIFI_SSID, pwd.c_str());
            LOG_I("WiFi", "Hotspot non trovato - connessione a rete standard '%s' (MAC: %s)...",
                  WIFI_SSID, WiFi.macAddress().c_str());
          }
        }
      }

      if (WiFi.status() == WL_CONNECTED) {
        strncpy(g_wifi_ssid, WiFi.SSID().c_str(), 32);
        g_wifi_ssid[32] = '\0';
        LOG_I("WiFi", "Connesso a '%s' - IP: %s", g_wifi_ssid, WiFi.localIP().toString().c_str());
        configTime(NTP_OFFSET, NTP_DST, NTP_SERVER);

        // Avvia ArduinoOTA solo quando connesso via hotspot
        if (g_via_hotspot) {
          ArduinoOTA.setHostname(OTA_HOSTNAME);
          ArduinoOTA.setPassword(OTA_PASSWORD);
          ArduinoOTA.onStart([]() {
            termostato_spegni_tutto(g_termostato);  // sicurezza: relè off prima dell'aggiornamento
            LOG_I("OTA", "Inizio aggiornamento OTA...");
          });
          ArduinoOTA.onEnd([]() {
            LOG_I("OTA", "Aggiornamento completato. Riavvio...");
          });
          ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            // Nessun log per evitare flood; progresso visibile in Arduino IDE
          });
          ArduinoOTA.onError([](ota_error_t error) {
            LOG_E("OTA", "Errore OTA [%u]", error);
          });
          ArduinoOTA.begin();
          LOG_I("OTA", "Servizio OTA attivo - hostname: %s  IP: %s",
                OTA_HOSTNAME, WiFi.localIP().toString().c_str());
          LOG_I("OTA", "Usa Arduino IDE: Strumenti -> Porta -> '%s (ESP32)'", OTA_HOSTNAME);
        }

        connettivita_mqtt_init(&g_eventi);
        entra_stato(Stato::MQTT_CONNESSIONE);

      } else if (ora - g_ts_stato_ingresso >= WIFI_TIMEOUT_MS) {
        LOG_W("WiFi", "Timeout - modalita' offline.");
        WiFi.disconnect(true);
        if (caricaProfilo(g_profilo)) { avvia_running(); }
        else { g_setpoint = g_temp_standby; entra_stato(Stato::STANDBY); }
      }
      break;

    // ----------------------------------------------------------
    // MQTT_CONNESSIONE
    // ----------------------------------------------------------
    case Stato::MQTT_CONNESSIONE:
      if (WiFi.status() != WL_CONNECTED) {
        entra_stato(Stato::WIFI_CONNESSIONE);
        break;
      }

      if (connettivita_mqtt_connetti()) {
        LOG_I("MQTT", "Connesso al broker. Rete: %s", g_wifi_ssid);
        entra_stato(Stato::RICHIESTA_PROFILO);

      } else if (ora - g_ts_stato_ingresso >= MQTT_CONNECT_TIMEOUT_MS) {
        LOG_W("MQTT", "Timeout connessione - fallback NVS.");
        if (caricaProfilo(g_profilo)) { avvia_running(); }
        else { g_setpoint = g_temp_standby; entra_stato(Stato::STANDBY); }
      }
      break;

    // ----------------------------------------------------------
    // RICHIESTA_PROFILO
    // ----------------------------------------------------------
    case Stato::RICHIESTA_PROFILO:
      connettivita_mqtt_loop();

      if (primo_tick) {
        // Attendi NTP (max 5s) per avere un orario valido prima di richiedere il profilo
        for (int i = 0; i < 50 && !ntp_ok(); i++) delay(100);
        if (!ntp_ok()) LOG_W("NTP", "Non sincronizzato, continuo senza.");

        g_eventi = {};  // azzera eventi MQTT residui
        mqtt_richiedi_profilo_attivo();
        g_ts_richiesta_profilo = millis();
        LOG_I("PROFILO", "Richiesta profilo attivo -> HA.");
      }

      // HA risponde con profilo completo
      if (g_eventi.profilo_ricevuto) {
        g_eventi.profilo_ricevuto = false;
        if (parsaProfiloJSON(g_eventi.profilo_json, g_profilo)) {
          salvaProfilo(g_profilo);
          mqtt_pubblica_ack("avviato", g_profilo.nome);
          LOG_I("PROFILO", "'%s' ricevuto (%d step).", g_profilo.nome, g_profilo.num_steps);
          avvia_running();
          break;
        } else {
          mqtt_pubblica_ack("errore_parsing", "");
          LOG_E("PROFILO", "Errore parsing JSON - attendo timeout.");
        }
      }

      // HA risponde "nessun_profilo"
      if (g_eventi.nessun_profilo) {
        g_eventi.nessun_profilo = false;
        if (g_eventi.temp_standby_valida) {
          g_temp_standby = g_eventi.temp_standby_ricevuta;
          g_eventi.temp_standby_valida = false;
        }
        LOG_I("FSM", "HA: nessun profilo attivo.");
        if (caricaProfilo(g_profilo)) {  // NVS ha ancora un profilo non terminato?
          LOG_I("NVS", "Ripreso profilo '%s'.", g_profilo.nome);
          avvia_running();
        } else {
          g_setpoint = g_temp_standby;
          entra_stato(Stato::STANDBY);
        }
        break;
      }

      // Timeout risposta HA
      if (millis() - g_ts_richiesta_profilo >= PROFILO_REQUEST_TIMEOUT_MS) {
        LOG_W("FSM", "Timeout risposta HA - verifica NVS.");
        if (caricaProfilo(g_profilo)) { avvia_running(); }
        else { g_setpoint = g_temp_standby; entra_stato(Stato::STANDBY); }
      }
      break;

    // ----------------------------------------------------------
    // RUNNING — profilo attivo, calcola setpoint, controlla relè
    // ----------------------------------------------------------
    case Stato::RUNNING:
      mantieni_connessione();

      if (g_eventi.stop_ricevuto) {
        g_eventi.stop_ricevuto = false;
        termostato_spegni_tutto(g_termostato);
        cancellaProfilo();
        memset(&g_profilo, 0, sizeof(g_profilo));
        g_setpoint = g_temp_standby;
        if (connettivita_mqtt_connesso()) mqtt_pubblica_ack("fermato", "");
        entra_stato(Stato::STANDBY);
        break;
      }

      if (g_eventi.temp_standby_valida) {
        g_temp_standby = g_eventi.temp_standby_ricevuta;
        g_eventi.temp_standby_valida = false;
      }

      // Calcola setpoint corrente e controlla i relè
      g_setpoint = calcolaSetpoint(g_profilo, secs_dall_avvio());
      applica_controllo_con_fallback(ora, g_setpoint,
                 isteresi(), min_cycle(), DEFAULT_CUTOFF_ANTICIP_FRIGO);

      // Verifica completamento profilo (notifica una sola volta)
      if (!g_completato_notif) {
        float tot = durata_tot_ore();
        if (tot > 0.0f && (secs_dall_avvio() / 3600.0f) >= tot) {
          g_completato_notif = true;
          // Blocca il setpoint all'ultima temperatura del profilo
          g_setpoint = g_profilo.steps[g_profilo.num_steps - 1].temp;
          if (connettivita_mqtt_connesso())
            mqtt_pubblica_ack("profilo_completato", g_profilo.nome);
          LOG_I("FSM", "Profilo '%s' completato -> HOLDING a %.1f grC",
                g_profilo.nome, g_setpoint);
          entra_stato(Stato::HOLDING);
          break;
        }
      }
      break;

    // ----------------------------------------------------------
    // STANDBY — nessun profilo; mantiene temperatura standard
    // ----------------------------------------------------------
    case Stato::STANDBY:
      mantieni_connessione();

      // Nuovo profilo ricevuto da HA
      if (g_eventi.profilo_ricevuto) {
        g_eventi.profilo_ricevuto = false;
        if (parsaProfiloJSON(g_eventi.profilo_json, g_profilo)) {
          salvaProfilo(g_profilo);
          if (connettivita_mqtt_connesso())
            mqtt_pubblica_ack("avviato", g_profilo.nome);
          avvia_running();
          break;
        } else if (connettivita_mqtt_connesso()) {
          mqtt_pubblica_ack("errore_parsing", "");
          LOG_E("PROFILO", "Errore parsing JSON.");
        }
      }

      // Aggiornamento temperatura standby da MQTT
      if (g_eventi.temp_standby_valida) {
        g_temp_standby = g_eventi.temp_standby_ricevuta;
        g_eventi.temp_standby_valida = false;
        g_setpoint = g_temp_standby;
      }

      applica_controllo_con_fallback(ora, g_setpoint,
                 DEFAULT_ISTERESI, DEFAULT_MIN_CYCLE_SEC, DEFAULT_CUTOFF_ANTICIP_FRIGO);
      break;

    // ----------------------------------------------------------
    // HOLDING — profilo completato; mantiene ultima temperatura
    //           fino a CMD stop o nuovo profilo
    // ----------------------------------------------------------
    case Stato::HOLDING:
      mantieni_connessione();

      if (g_eventi.stop_ricevuto) {
        g_eventi.stop_ricevuto = false;
        termostato_spegni_tutto(g_termostato);
        cancellaProfilo();
        memset(&g_profilo, 0, sizeof(g_profilo));
        g_setpoint = g_temp_standby;
        if (connettivita_mqtt_connesso()) mqtt_pubblica_ack("fermato", "");
        entra_stato(Stato::STANDBY);
        break;
      }

      if (g_eventi.profilo_ricevuto) {
        g_eventi.profilo_ricevuto = false;
        if (parsaProfiloJSON(g_eventi.profilo_json, g_profilo)) {
          salvaProfilo(g_profilo);
          if (connettivita_mqtt_connesso())
            mqtt_pubblica_ack("avviato", g_profilo.nome);
          avvia_running();
          break;
        } else if (connettivita_mqtt_connesso()) {
          mqtt_pubblica_ack("errore_parsing", "");
          LOG_E("PROFILO", "Errore parsing JSON.");
        }
      }

      // g_setpoint è già fissato all'ultima temp del profilo
      // da quando siamo entrati in HOLDING (vedi RUNNING -> profilo_completato)
      applica_controllo_con_fallback(ora, g_setpoint,
                 isteresi(), min_cycle(), DEFAULT_CUTOFF_ANTICIP_FRIGO);
      break;

    // ----------------------------------------------------------
    // ALLARME_SENSORE — relè off, attende 30s poi riavvia il dispositivo
    // Il profilo è già salvato in NVS: al riavvio verrà ripreso.
    // ----------------------------------------------------------
    case Stato::ALLARME_SENSORE:
      // Mantieni MQTT attivo per pubblicare heartbeat di allarme
      mantieni_connessione();

      // Al primo tick, tenta un power cycle prima di aspettare ALLARME_REBOOT_DELAY_MS.
      // Permette il ripristino in-field senza reboot se il sensore risponde dopo il ciclo.
      if (primo_tick && g_power_cycle_tentativi < SENSOR_POWER_CYCLE_MAX_ATTEMPTS) {
        g_power_cycle_tentativi++;
        LOG_W("ALLARME", "Power cycle sensore (tentativo %d/%d)...",
              g_power_cycle_tentativi, SENSOR_POWER_CYCLE_MAX_ATTEMPTS);
        if (sensor_power_cycle(g_sensore)) {
          g_temp_corrente         = g_sensore.ultima_temp_valida;
          g_temp_valida           = true;
          g_sensore_valid_streak  = SENSOR_VALID_STREAK_MIN;
          g_ts_sensore_instabile  = 0;
          g_power_cycle_tentativi = 0;
          aggiorna_temp_controllo(g_temp_corrente, true);
          LOG_I("ALLARME", "Power cycle riuscito: T=%.2f grC. Ripristino stato operativo.",
                g_temp_corrente);
          if (connettivita_mqtt_connesso())
            mqtt_pubblica_ack("sensore_ripristinato_power_cycle",
                              g_profilo.valido ? g_profilo.nome : "");
          if (g_profilo.valido) avvia_running();
          else { g_setpoint = g_temp_standby; entra_stato(Stato::STANDBY); }
          break;
        }
        LOG_W("ALLARME", "Power cycle fallito - attendo %lus poi riavvio.",
              ALLARME_REBOOT_DELAY_MS / 1000UL);
      }

      // Dopo ALLARME_REBOOT_DELAY_MS riavvia il dispositivo.
      // GPIO25 va in HiZ durante il boot: il sensore viene power-ciclato implicitamente.
      if (ora - g_ts_stato_ingresso >= ALLARME_REBOOT_DELAY_MS) {
        LOG_E("ALLARME", "Riavvio dispositivo...");
        if (connettivita_mqtt_connesso())
          mqtt_pubblica_allarme("riavvio_in_corso", 0);
        delay(500);  // flush MQTT
        ESP.restart();
      }
      break;

  }  // end switch

  duty_cycle_track(ora, g_termostato.riscaldatore_on, g_termostato.refrigeratore_on);

  // ---- Pubblicazione stato MQTT ogni STATUS_INTERVAL_MS ----
  if (ora - g_ts_ultimo_stato >= STATUS_INTERVAL_MS && connettivita_mqtt_connesso()) {
    g_ts_ultimo_stato = ora;

    // Aggiorna il timestamp NTP in NVS ogni heartbeat.
    // Se l'ESP32 si riavvia senza WiFi, puo' stimare il tempo trascorso
    // con errore massimo pari a un intervallo di heartbeat (~60s).
    if (ntp_ok() && g_profilo.valido) {
      time_t t_ora; time(&t_ora);
      g_profilo.ultimo_epoch_noto = (uint32_t)t_ora;
      salvaEpochNoto((uint32_t)t_ora);
    }

    bool attivo = (g_stato == Stato::RUNNING || g_stato == Stato::HOLDING);
    int   sidx   = -1;
    float ore_st = 0.0f, ore_tot = 0.0f;
    if (attivo) stepCorrente(g_profilo, secs_dall_avvio(), sidx, ore_st, ore_tot);

    const char* step_nome = (attivo && sidx >= 0)
                              ? g_profilo.steps[sidx].nome
                              : stato_str(g_stato);
    const char* prof_nome = g_profilo.valido ? g_profilo.nome : "nessuno";

    mqtt_pubblica_stato(g_temp_corrente, g_setpoint, step_nome, sidx,
                        ore_st, ore_tot,
                        g_termostato.riscaldatore_on,
                        g_termostato.refrigeratore_on,
                        g_sensore.in_allarme,
                        prof_nome, stato_str(g_stato));

    // T=---- quando il sensore è in fault o in fase di debounce (letture stabili insufficienti)
    bool _sensore_ok = g_temp_valida && (g_sensore_valid_streak >= SENSOR_VALID_STREAK_MIN);
    if (_sensore_ok) {
      LOG_I("STATUS", "[%s] T=%.2f grC Tc=%.2f grC SP=%.2f grC | %s | R=%d F=%d A=%d",
            stato_str(g_stato), g_temp_corrente, g_temp_controllo, g_setpoint, step_nome,
            g_termostato.riscaldatore_on,
            g_termostato.refrigeratore_on,
            g_sensore.in_allarme);
    } else {
      LOG_I("STATUS", "[%s] T=---- grC Tc=---- grC SP=%.2f grC | %s | R=%d F=%d A=1",
            stato_str(g_stato), g_setpoint, step_nome,
            g_termostato.riscaldatore_on,
            g_termostato.refrigeratore_on);
    }
  }

  // ---- Aggiornamento display ogni DISPLAY_UPDATE_MS ----
  if (ora - g_ts_ultimo_display >= DISPLAY_UPDATE_MS) {
    g_ts_ultimo_display = ora;

    bool disp_attivo = (g_stato == Stato::RUNNING || g_stato == Stato::HOLDING);
    bool disp_running = (g_stato == Stato::RUNNING);
    uint32_t disp_secs = secs_dall_avvio();
    if (disp_secs == 0 && disp_attivo) {
      // Fallback: se il tempo NTP non e' disponibile, usa il tempo trascorso
      // nello stato operativo corrente per evitare progress=0 fisso a display.
      disp_secs = (ora - g_ts_stato_ingresso) / 1000UL;
    }
    int  disp_sidx   = -1;
    float disp_ore_st = 0.0f, disp_ore_tot = 0.0f;
    float disp_prog_pct = 0.0f;
    float disp_giorni_next = -1.0f;
    if (disp_attivo)
      stepCorrente(g_profilo, disp_secs, disp_sidx, disp_ore_st, disp_ore_tot);

    if (disp_attivo && g_profilo.valido) {
      float tot_h = durata_tot_ore();
      if (tot_h > 0.0f) {
        disp_prog_pct = (disp_ore_tot / tot_h) * 100.0f;
        if (disp_prog_pct < 0.0f) disp_prog_pct = 0.0f;
        if (disp_prog_pct > 100.0f) disp_prog_pct = 100.0f;
      }
    }

    if (disp_running && g_profilo.valido && disp_sidx >= 0 && disp_sidx < g_profilo.num_steps) {
      float step_dur = g_profilo.steps[disp_sidx].is_ramp
                        ? g_profilo.steps[disp_sidx].ramp_hours
                        : g_profilo.steps[disp_sidx].duration_hours;
      float rem_h = step_dur - disp_ore_st;
      if (rem_h < 0.0f) rem_h = 0.0f;
      disp_giorni_next = rem_h / 24.0f;
    }

    const char* disp_step = (disp_attivo && disp_sidx >= 0)
                              ? g_profilo.steps[disp_sidx].nome
                              : "";

    DisplayData dd = {};
    dd.stato_fsm                = stato_str(g_stato);
    dd.temp_reale               = g_temp_corrente;
    dd.setpoint                 = g_setpoint;
    dd.step_nome                = disp_step;
    dd.ore_step                 = disp_ore_st;
    dd.ore_totali               = disp_ore_tot;
    dd.progresso_pct            = disp_prog_pct;
    dd.giorni_prossimo_step     = disp_giorni_next;
    dd.profilo_nome             = g_profilo.valido ? g_profilo.nome : "nessuno";
    dd.riscaldatore_on          = g_termostato.riscaldatore_on;
    dd.refrigeratore_on         = g_termostato.refrigeratore_on;
    dd.wifi_offline             = (WiFi.status() != WL_CONNECTED) || g_wifi_disconnesso_evento;
    dd.mqtt_offline             = !connettivita_mqtt_connesso();
    strncpy(dd.wifi_ssid, g_wifi_ssid, sizeof(dd.wifi_ssid) - 1);
    dd.wifi_ssid[sizeof(dd.wifi_ssid) - 1] = '\0';
    dd.ts_ingresso_allarme_ms   = g_ts_stato_ingresso;  // valido solo in ALLARME

    display_aggiorna(dd);
  }
}
