// ============================================================
// Fermentatore V2 — Firmware ESP32
// Termostato autonomo con profilo trasmesso da Home Assistant.
//
// Librerie richieste (Arduino Library Manager):
//   - PubSubClient       (Nick O'Leary)
//   - ArduinoJson        (Benoit Blanchon)  v6.x
//   - OneWire            (Paul Stoffregen)
//   - DallasTemperature  (Miles Burton)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "profilo.h"
#include "termostato.h"
#include "sensor_watchdog.h"
#include "mqtt_handler.h"

// ============================================================
// Oggetti globali
// ============================================================
static ProfiloFermentazione g_profilo;
static StatoTermostato      g_termostato;
static SensorState          g_sensore;

static bool          g_processo_attivo       = false;
static bool          g_completato_notificato = false;  // evita doppio ACK fine profilo
static unsigned long g_ts_ultimo_stato   = 0;
static unsigned long g_ts_ultima_lettura = 0;
static float         g_temp_corrente     = 20.0f;
static float         g_setpoint_corrente = 20.0f;

// ============================================================
// Helpers
// ============================================================
static uint32_t secondi_trascorsi_avvio() {
  if (!g_processo_attivo || g_profilo.avvio_epoch == 0) return 0;
  time_t ora_epoch;
  time(&ora_epoch);
  if (ora_epoch < 1000000000L) return 0;  // NTP non ancora sincronizzato
  long diff = (long)ora_epoch - (long)g_profilo.avvio_epoch;
  return (diff > 0) ? (uint32_t)diff : 0;
}

// ============================================================
// Callback MQTT
// ============================================================
static void on_avvia_ricevuto(const char* json) {
  Serial.println("[MQTT] Comando AVVIA ricevuto.");

  if (parsaProfiloJSON(json, g_profilo)) {
    salvaProfilo(g_profilo);
    g_processo_attivo       = true;
    g_completato_notificato = false;
    g_setpoint_corrente = calcolaSetpoint(g_profilo, 0);
    Serial.printf("[OK] Profilo '%s' caricato (%d step).\n",
                  g_profilo.nome, g_profilo.num_steps);
    mqtt_pubblica_ack("avviato", g_profilo.nome);
  } else {
    Serial.println("[ERRORE] Parsing profilo JSON fallito.");
    mqtt_pubblica_ack("errore_parsing", "sconosciuto");
  }
}

static void on_stop_ricevuto() {
  Serial.println("[MQTT] Comando STOP ricevuto.");
  g_processo_attivo       = false;
  g_completato_notificato = false;
  termostato_spegni_tutto(g_termostato);
  cancellaProfilo();
  mqtt_pubblica_ack("fermato", g_profilo.nome);
  memset(&g_profilo, 0, sizeof(g_profilo));
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Fermentatore V2 — Avvio ===");

  // Relè in stato sicuro prima di qualsiasi altra operazione
  termostato_init(g_termostato);

  // Inizializza sensore DS18B20
  sensor_init(g_sensore);

  // Connessione WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connessione in corso");
  unsigned long t_wifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t_wifi < 20000UL) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connesso — IP: %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Timeout — continuo in modalità standalone.");
  }

  // Sincronizzazione NTP (necessaria per il calcolo dei tempi del profilo)
  configTime(NTP_OFFSET, NTP_DST, NTP_SERVER);
  Serial.println("[NTP] Sincronizzazione in corso...");

  // Inizializza client MQTT
  mqtt_init(on_avvia_ricevuto, on_stop_ricevuto);

  // Recupero profilo da NVS (sopravvivenza al reset)
  memset(&g_profilo, 0, sizeof(g_profilo));
  if (caricaProfilo(g_profilo)) {
    g_processo_attivo = true;
    Serial.printf("[NVS] Profilo '%s' ripreso da memoria non volatile.\n",
                  g_profilo.nome);
  } else {
    Serial.println("[NVS] Nessun profilo attivo. In attesa di AVVIA.");
  }

  Serial.println("=== Setup completato ===");
}

// ============================================================
// Loop principale
// ============================================================
void loop() {
  unsigned long ora = millis();

  // --- Gestione connessione MQTT ---
  if (WiFi.status() == WL_CONNECTED && !mqtt_connesso()) {
    Serial.println("[MQTT] Riconnessione...");
    if (!mqtt_connetti()) {
      delay(MQTT_RECONNECT_DELAY_MS);
      return;
    }
    Serial.println("[MQTT] Connesso.");
  }
  mqtt_loop();

  // --- Lettura sensore e controllo termostato ---
  if (ora - g_ts_ultima_lettura >= SENSOR_READ_INTERVAL_MS) {
    g_ts_ultima_lettura = ora;

    uint32_t timeout_allarme = (g_profilo.allarme_timeout_minuti > 0)
                                 ? g_profilo.allarme_timeout_minuti : 5;

    bool lettura_ok = sensor_leggi(g_sensore, g_temp_corrente, timeout_allarme);

    // Transizione: sensore entrato in allarme
    if (sensor_allarme_nuovo(g_sensore)) {
      Serial.println("[ALLARME] Sensore DS18B20 offline — relè spenti.");
      termostato_spegni_tutto(g_termostato);
      int durata_min = (int)((millis() - g_sensore.ts_primo_errore_ms) / 60000UL);
      mqtt_pubblica_allarme("sensore_offline", durata_min);
    }

    // Transizione: sensore ripristinato
    if (sensor_ripristinato(g_sensore)) {
      Serial.println("[OK] Sensore DS18B20 ripristinato.");
      mqtt_pubblica_allarme("sensore_ripristinato", 0);
    }

    // Controllo termostato (solo se processo attivo e sensore valido)
    if (g_processo_attivo && !g_sensore.in_allarme) {
      uint32_t secs = secondi_trascorsi_avvio();
      g_setpoint_corrente = calcolaSetpoint(g_profilo, secs);

      float isteresi     = (g_profilo.isteresi > 0.0f) ? g_profilo.isteresi : 0.3f;
      uint32_t min_cycle = (g_profilo.min_cycle_minutes > 0) ? g_profilo.min_cycle_minutes : 5;

      termostato_aggiorna(g_termostato, g_temp_corrente,
                           g_setpoint_corrente, isteresi, min_cycle);
    }
  }

  // --- Pubblicazione stato MQTT ogni STATUS_INTERVAL_MS ---
  if (ora - g_ts_ultimo_stato >= STATUS_INTERVAL_MS) {
    g_ts_ultimo_stato = ora;

    if (mqtt_connesso()) {
      int   step_idx   = -1;
      float ore_step   = 0.0f;
      float ore_totali = 0.0f;

      if (g_processo_attivo) {
        stepCorrente(g_profilo, secondi_trascorsi_avvio(),
                     step_idx, ore_step, ore_totali);

        // Notifica HA una sola volta al termine di tutti gli step
        if (!g_completato_notificato) {
          float durata_tot = 0.0f;
          for (int i = 0; i < g_profilo.num_steps; i++)
            durata_tot += g_profilo.steps[i].is_ramp
                          ? g_profilo.steps[i].ramp_hours
                          : g_profilo.steps[i].duration_hours;
          if (durata_tot > 0.0f && ore_totali >= durata_tot) {
            g_completato_notificato = true;
            mqtt_pubblica_ack("profilo_completato", g_profilo.nome);
            Serial.printf("[FINE] Profilo '%s' completato dopo %.1fh\n",
                          g_profilo.nome, ore_totali);
          }
        }
      }

      const char* step_nome  = (step_idx >= 0 && g_processo_attivo)
                                 ? g_profilo.steps[step_idx].nome
                                 : "Fermo";
      const char* prof_nome  = g_processo_attivo ? g_profilo.nome : "nessuno";

      mqtt_pubblica_stato(g_temp_corrente, g_setpoint_corrente,
                           step_nome, step_idx,
                           ore_step, ore_totali,
                           g_termostato.riscaldatore_on,
                           g_termostato.refrigeratore_on,
                           g_sensore.in_allarme,
                           prof_nome);

      Serial.printf("[STATUS] T=%.2f°C SP=%.2f°C Step=%s Risc=%d Frigo=%d Alarm=%d\n",
                    g_temp_corrente, g_setpoint_corrente, step_nome,
                    g_termostato.riscaldatore_on,
                    g_termostato.refrigeratore_on,
                    g_sensore.in_allarme);
    }
  }
}
