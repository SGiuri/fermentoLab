// ============================================================
// Fermentatore V5 — Firmware ESP32
// Termostato autonomo a macchina a stati.
//
// NOVITÀ RISPETTO A V4:
//   - Controllo PID bidirezionale su PWM (sostituisce bang-bang + isteresi).
//   - Attuatori asimmetrici: cinghia riscaldante 30W (lenta) e pompa glicole
//     (veloce/potente) → gain PID separati per riscaldamento e raffreddamento.
//   - Ciclo PWM configurabile (default 60s); impulso minimo pompa 5s.
//   - Eliminato il modulo duty_cycle (fallback storico).
//     La connettività WiFi/MQTT non è più un prerequisito per il controllo:
//     il PID funziona autonomamente senza rete.
//   - Supporto OTA via hotspot personale (identico a V4).
//
// FLUSSO DI AVVIO: identico a V4.
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
static bool                 g_primo_tick         = true;
static unsigned long        g_ts_stato_ingresso  = 0;

static ProfiloFermentazione g_profilo    = {};
static StatoTermostato      g_termostato = {};
static SensorState          g_sensore    = {};
static EventiMQTT           g_eventi     = {};

static float  g_temp_corrente    = 20.0f;
static float  g_setpoint         = DEFAULT_STANDBY_TEMP;
static float  g_temp_standby     = DEFAULT_STANDBY_TEMP;
static bool   g_completato_notif = false;
static bool   g_temp_valida      = true;
// Debounce streak sensore: richiede N letture consecutive per considerare il sensore stabile.
// Inizializzato a SENSOR_VALID_STREAK_MIN: nessun debounce al primo avvio.
static uint8_t g_sensore_valid_streak = SENSOR_VALID_STREAK_MIN;
// Power cycle DS18B20: timestamp primo segno di instabilità (0 = stabile) e contatore tentativi.
static unsigned long g_ts_sensore_instabile  = 0;
static uint8_t       g_power_cycle_tentativi = 0;

// WiFi reconnect state
static unsigned long g_ts_wifi_perso           = 0;
static unsigned long g_ts_ultima_riconnessione = 0;
static int           g_tentativi_riconn        = 0;
static unsigned long g_ts_ciclo_riconn         = 0;

static bool g_via_hotspot = false;
// Flag: true dopo che il burst iniziale di 3 tentativi rapidi su hotspot è esaurito;
// in seguito si usa il ciclo standard (3 x 30s + 20 min).
static bool g_hotspot_burst_done = false;
// SSID della rete su cui si è connessi (vuota se mai connessi)
static char g_wifi_ssid[33] = {};

static unsigned long g_ts_ultimo_stato      = 0;
static unsigned long g_ts_ultima_lettura    = 0;
static unsigned long g_ts_richiesta_profilo = 0;
static unsigned long g_ts_ultimo_display    = 0;

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
    time(&epoch_corrente);
  } else if (g_profilo.ultimo_epoch_noto > 0) {
    LOG_W("NTP", "Offline - uso epoch NVS come fallback.");
    epoch_corrente = (time_t)g_profilo.ultimo_epoch_noto;
  } else {
    return 0;
  }

  long diff = (long)epoch_corrente - (long)g_profilo.avvio_epoch;
  return (diff > 0) ? (uint32_t)diff : 0;
}

// Helper: parametri PWM con fallback ai default se profilo non valido
static uint32_t pwm_period()   { return (g_profilo.valido && g_profilo.pwm_period_seconds > 0)   ? g_profilo.pwm_period_seconds   : (uint32_t)DEFAULT_PWM_PERIOD_S; }
static uint32_t min_pompa_on() { return (g_profilo.valido && g_profilo.min_pompa_on_seconds > 0) ? g_profilo.min_pompa_on_seconds : (uint32_t)DEFAULT_MIN_POMPA_ON_S; }

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
  // Aggiorna il PID con i gain del nuovo profilo e azzera l'integrale
  termostato_pid_init(g_termostato,
    g_profilo.kp_risc,  g_profilo.ki_risc,  g_profilo.kd_risc,
    g_profilo.kp_frigo, g_profilo.ki_frigo, g_profilo.kd_frigo);
  g_setpoint         = calcolaSetpoint(g_profilo, secs_dall_avvio());
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

  if (WiFi.status() == WL_CONNECTED) {
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
      LOG_W("WiFi", "Blackout WiFi - PID attivo. Serie 3 riconn. ogni 20 min.");
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
// Cambio stato
// ============================================================
static void entra_stato(Stato nuovo) {
  LOG_D("FSM", "%-18s -> %s", stato_str(g_stato), stato_str(nuovo));
  g_stato             = nuovo;
  g_ts_stato_ingresso = millis();
  g_primo_tick        = true;
  g_ts_ultimo_display = 0;  // forza refresh display immediato
}

// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  log_init(mqtt_pubblica_log);
  LOG_I("BOOT", "=== Fermentatore V5 - Avvio ===");

  display_init();

  // Hardware in stato sicuro prima di qualsiasi altra operazione
  termostato_init(g_termostato,
    DEFAULT_PID_KP_RISC,  DEFAULT_PID_KI_RISC,  DEFAULT_PID_KD_RISC,
    DEFAULT_PID_KP_FRIGO, DEFAULT_PID_KI_FRIGO, DEFAULT_PID_KD_FRIGO,
    DEFAULT_PWM_PERIOD_S);
  sensor_power_cycle_init();  // configura GPIO25 (PIN_SENSOR_PWR) come output, HIGH
  sensor_init(g_sensore);

  entra_stato(Stato::WIFI_CONNESSIONE);
}

// ============================================================
// loop() — macchina a stati
// ============================================================
void loop() {
  unsigned long ora = millis();

  bool primo_tick = g_primo_tick;
  g_primo_tick    = false;
  g_eventi.ping_ricevuto = false;

  // ---- OTA handle ----
  if (g_via_hotspot && WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  // ---- CMD: test offline ----
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
      delay(500);
    }
    ESP.restart();
  }

  // ---- CMD: riavvio remoto ----
  if (g_eventi.reboot_ricevuto) {
    g_eventi.reboot_ricevuto = false;
    termostato_spegni_tutto(g_termostato);
    LOG_W("CMD", "Ricevuto cmd/reboot remoto.");
    if (connettivita_mqtt_connesso()) {
      mqtt_pubblica_ack("riavvio_remoto", g_profilo.valido ? g_profilo.nome : "");
      mqtt_pubblica_allarme("riavvio_in_corso", 0);
    }
    delay(500);
    ESP.restart();
  }

  // ---- Lettura sensore (ogni SENSOR_READ_INTERVAL_MS) ----
  if (ora - g_ts_ultima_lettura >= SENSOR_READ_INTERVAL_MS) {
    g_ts_ultima_lettura = ora;

    uint32_t timeout_min = (g_profilo.valido && g_profilo.allarme_timeout_minuti > 0)
                             ? g_profilo.allarme_timeout_minuti : 5;
    g_temp_valida = sensor_leggi(g_sensore, g_temp_corrente, timeout_min);

    // Streak debounce: un guasto azzera; una lettura valida incrementa fino al minimo.
    if (g_temp_valida) {
      if (g_sensore_valid_streak < SENSOR_VALID_STREAK_MIN) g_sensore_valid_streak++;
    } else {
      g_sensore_valid_streak = 0;
    }

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
        LOG_I("SENSORE", "Power cycle riuscito: T=%.2f grC.", g_temp_corrente);
      } else {
        g_ts_sensore_instabile = ora;  // reset timer per il prossimo tentativo
        LOG_W("SENSORE", "Power cycle fallito (%d/%d).",
              g_power_cycle_tentativi, SENSOR_POWER_CYCLE_MAX_ATTEMPTS);
      }
    }

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
        WiFi.disconnect(true, true);
        delay(200);

        bool _test_no_wifi = false;
        {
          Preferences _prefs;
          _prefs.begin(NVS_NS_FERM, false);
          _test_no_wifi = _prefs.getBool(NVS_KEY_TEST_NO_WIFI, false);
          if (_test_no_wifi) { _prefs.putBool(NVS_KEY_TEST_NO_WIFI, false); }
          _prefs.end();
        }

        if (_test_no_wifi) {
          g_via_hotspot = false;
          WiFi.begin(WIFI_SSID, "WRONG_TEST_PWD_XXXX");
          LOG_W("WiFi", "TEST_NO_WIFI attivo: password sbagliata intenzionale.");

        } else {
          LOG_I("WiFi", "Scansione reti (ricerca hotspot OTA '%s')...", HOTSPOT_SSID);
          int n = WiFi.scanNetworks();
          g_via_hotspot = false;
          for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == HOTSPOT_SSID) { g_via_hotspot = true; break; }
          }
          WiFi.scanDelete();

          if (g_via_hotspot) {
            WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
            LOG_I("WiFi", "Hotspot OTA trovato - connessione a '%s'...", HOTSPOT_SSID);
          } else {
            String pwd = getWifiPassword();
            if (pwd.isEmpty()) {
              LOG_E("WiFi", "MAC %s non in lista - aggiungilo in wifi_connection.ino",
                    WiFi.macAddress().c_str());
            }
            WiFi.begin(WIFI_SSID, pwd.c_str());
            LOG_I("WiFi", "Connessione a rete standard '%s' (MAC: %s)...",
                  WIFI_SSID, WiFi.macAddress().c_str());
          }
        }
      }

      if (WiFi.status() == WL_CONNECTED) {
        strncpy(g_wifi_ssid, WiFi.SSID().c_str(), 32);  // memorizza SSID connessa
        g_wifi_ssid[32] = '\0';
        LOG_I("WiFi", "Connesso a '%s' - IP: %s",
              g_wifi_ssid, WiFi.localIP().toString().c_str());
        configTime(NTP_OFFSET, NTP_DST, NTP_SERVER);

        if (g_via_hotspot) {
          ArduinoOTA.setHostname(OTA_HOSTNAME);
          ArduinoOTA.setPassword(OTA_PASSWORD);
          ArduinoOTA.onStart([]() {
            termostato_spegni_tutto(g_termostato);
            LOG_I("OTA", "Inizio aggiornamento OTA...");
          });
          ArduinoOTA.onEnd([]() { LOG_I("OTA", "Aggiornamento completato. Riavvio..."); });
          ArduinoOTA.onProgress([](unsigned int, unsigned int) {});
          ArduinoOTA.onError([](ota_error_t error) { LOG_E("OTA", "Errore OTA [%u]", error); });
          ArduinoOTA.begin();
          LOG_I("OTA", "Servizio OTA attivo - hostname: %s  IP: %s",
                OTA_HOSTNAME, WiFi.localIP().toString().c_str());
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
        for (int i = 0; i < 50 && !ntp_ok(); i++) delay(100);
        if (!ntp_ok()) LOG_W("NTP", "Non sincronizzato, continuo senza.");

        g_eventi = {};
        mqtt_richiedi_profilo_attivo();
        g_ts_richiesta_profilo = millis();
        LOG_I("PROFILO", "Richiesta profilo attivo -> HA.");
      }

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

      if (g_eventi.nessun_profilo) {
        g_eventi.nessun_profilo = false;
        if (g_eventi.temp_standby_valida) {
          g_temp_standby = g_eventi.temp_standby_ricevuta;
          g_eventi.temp_standby_valida = false;
        }
        LOG_I("FSM", "HA: nessun profilo attivo.");
        if (caricaProfilo(g_profilo)) {
          LOG_I("NVS", "Ripreso profilo '%s'.", g_profilo.nome);
          avvia_running();
        } else {
          g_setpoint = g_temp_standby;
          entra_stato(Stato::STANDBY);
        }
        break;
      }

      if (millis() - g_ts_richiesta_profilo >= PROFILO_REQUEST_TIMEOUT_MS) {
        LOG_W("FSM", "Timeout risposta HA - verifica NVS.");
        if (caricaProfilo(g_profilo)) { avvia_running(); }
        else { g_setpoint = g_temp_standby; entra_stato(Stato::STANDBY); }
      }
      break;

    // ----------------------------------------------------------
    // RUNNING — profilo attivo, calcola setpoint, PID PWM
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

      g_setpoint = calcolaSetpoint(g_profilo, secs_dall_avvio());
      termostato_aggiorna(g_termostato, g_temp_corrente, g_setpoint,
                          g_temp_valida, min_pompa_on(), pwm_period());

      if (!g_completato_notif) {
        float tot = durata_tot_ore();
        if (tot > 0.0f && (secs_dall_avvio() / 3600.0f) >= tot) {
          g_completato_notif = true;
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

      if (g_eventi.temp_standby_valida) {
        g_temp_standby = g_eventi.temp_standby_ricevuta;
        g_eventi.temp_standby_valida = false;
        g_setpoint = g_temp_standby;
      }

      // In standby usa i gain di default (nessun profilo caricato)
      termostato_aggiorna(g_termostato, g_temp_corrente, g_setpoint,
                          g_temp_valida, (uint32_t)DEFAULT_MIN_POMPA_ON_S,
                          (uint32_t)DEFAULT_PWM_PERIOD_S);
      break;

    // ----------------------------------------------------------
    // HOLDING — profilo completato; mantiene ultima temperatura
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

      // g_setpoint fisso all'ultima temp del profilo (da quando siamo entrati in HOLDING)
      termostato_aggiorna(g_termostato, g_temp_corrente, g_setpoint,
                          g_temp_valida, min_pompa_on(), pwm_period());
      break;

    // ----------------------------------------------------------
    // ALLARME_SENSORE — relè off, attende 30s poi riavvia
    // ----------------------------------------------------------
    case Stato::ALLARME_SENSORE:
      mantieni_connessione();

      // Al primo tick, tenta un power cycle prima di aspettare ALLARME_REBOOT_DELAY_MS.
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
        delay(500);
        ESP.restart();
      }
      break;

  }  // end switch

  // ---- Pubblicazione stato MQTT ogni STATUS_INTERVAL_MS ----
  if (ora - g_ts_ultimo_stato >= STATUS_INTERVAL_MS && connettivita_mqtt_connesso()) {
    g_ts_ultimo_stato = ora;

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
                        prof_nome, stato_str(g_stato),
                        g_termostato.pid_output,
                        g_termostato.duty_riscaldatore,
                        g_termostato.duty_refrigeratore);

    // T=---- quando il sensore è in fault o non ha ancora raggiunto la streak minima
    bool _sensore_ok = g_temp_valida && (g_sensore_valid_streak >= SENSOR_VALID_STREAK_MIN);
    if (_sensore_ok) {
      LOG_I("STATUS", "[%s] T=%.2f grC SP=%.2f grC PID=%.1f R=%.1f%% F=%.1f%% | %s",
            stato_str(g_stato), g_temp_corrente, g_setpoint,
            g_termostato.pid_output,
            g_termostato.duty_riscaldatore,
            g_termostato.duty_refrigeratore,
            step_nome);
    } else {
      LOG_I("STATUS", "[%s] T=---- grC SP=%.2f grC PID=---- R=%.1f%% F=%.1f%% | %s A=1",
            stato_str(g_stato), g_setpoint,
            g_termostato.duty_riscaldatore,
            g_termostato.duty_refrigeratore,
            step_nome);
    }
  }

  // ---- Aggiornamento display ogni DISPLAY_UPDATE_MS ----
  if (ora - g_ts_ultimo_display >= DISPLAY_UPDATE_MS) {
    g_ts_ultimo_display = ora;

    bool disp_attivo  = (g_stato == Stato::RUNNING || g_stato == Stato::HOLDING);
    bool disp_running = (g_stato == Stato::RUNNING);
    uint32_t disp_secs = secs_dall_avvio();
    if (disp_secs == 0 && disp_attivo) {
      disp_secs = (ora - g_ts_stato_ingresso) / 1000UL;
    }

    int   disp_sidx      = -1;
    float disp_ore_st    = 0.0f, disp_ore_tot = 0.0f;
    float disp_prog_pct  = 0.0f;
    float disp_giorni_next = -1.0f;
    if (disp_attivo)
      stepCorrente(g_profilo, disp_secs, disp_sidx, disp_ore_st, disp_ore_tot);

    if (disp_attivo && g_profilo.valido) {
      float tot_h = durata_tot_ore();
      if (tot_h > 0.0f) {
        disp_prog_pct = (disp_ore_tot / tot_h) * 100.0f;
        if (disp_prog_pct < 0.0f)   disp_prog_pct = 0.0f;
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
    dd.stato_fsm              = stato_str(g_stato);
    dd.temp_reale             = g_temp_corrente;
    dd.setpoint               = g_setpoint;
    dd.step_nome              = disp_step;
    dd.ore_step               = disp_ore_st;
    dd.ore_totali             = disp_ore_tot;
    dd.progresso_pct          = disp_prog_pct;
    dd.giorni_prossimo_step   = disp_giorni_next;
    dd.profilo_nome           = g_profilo.valido ? g_profilo.nome : "nessuno";
    dd.riscaldatore_on        = g_termostato.riscaldatore_on;
    dd.refrigeratore_on       = g_termostato.refrigeratore_on;
    dd.wifi_offline           = (WiFi.status() != WL_CONNECTED);
    strncpy(dd.wifi_ssid, g_wifi_ssid, sizeof(dd.wifi_ssid) - 1);
    dd.wifi_ssid[sizeof(dd.wifi_ssid) - 1] = '\0';
    dd.ts_ingresso_allarme_ms = g_ts_stato_ingresso;

    display_aggiorna(dd);
  }
}
