#include "profilo.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>

static const char* NVS_NS  = NVS_NS_FERM;
static const char* NVS_KEY = "profilo";

// ============================================================
// Calcolo setpoint
// ============================================================

float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi) {
  if (!p.valido || p.num_steps == 0) return -999.0f;

  float ore_correnti = secondi_trascorsi / 3600.0f;
  float t_start      = 0.0f;
  float temp_prev    = p.steps[0].temp;

  for (int i = 0; i < p.num_steps; i++) {
    float durata = p.steps[i].is_ramp ? p.steps[i].ramp_hours
                                      : p.steps[i].duration_hours;
    float t_end = t_start + durata;

    if (ore_correnti >= t_start && ore_correnti < t_end) {
      if (p.steps[i].is_ramp) {
        float frac = (ore_correnti - t_start) / durata;
        return temp_prev + frac * (p.steps[i].temp - temp_prev);
      } else {
        return p.steps[i].temp;
      }
    }

    temp_prev = p.steps[i].temp;
    t_start   = t_end;
  }

  // Profilo terminato: mantieni temp dell'ultimo step
  return p.steps[p.num_steps - 1].temp;
}

// ============================================================
// Step corrente
// ============================================================

void stepCorrente(const ProfiloFermentazione& p, uint32_t secondi_trascorsi,
                  int& idx_out, float& ore_step_out, float& ore_totali_out) {
  ore_totali_out = secondi_trascorsi / 3600.0f;

  if (!p.valido || p.num_steps == 0) {
    idx_out = -1; ore_step_out = 0.0f;
    return;
  }

  float t_start      = 0.0f;
  float ore_correnti = ore_totali_out;

  for (int i = 0; i < p.num_steps; i++) {
    float durata = p.steps[i].is_ramp ? p.steps[i].ramp_hours
                                      : p.steps[i].duration_hours;
    float t_end = t_start + durata;

    if (ore_correnti >= t_start && ore_correnti < t_end) {
      idx_out      = i;
      ore_step_out = ore_correnti - t_start;
      return;
    }
    t_start = t_end;
  }

  // Profilo terminato
  idx_out      = p.num_steps - 1;
  ore_step_out = ore_totali_out;
}

// ============================================================
// Parsing JSON
// Compatibile con profili v4 (campi PID assenti → usa default config.h)
// ============================================================

bool parsaProfiloJSON(const char* json_str, ProfiloFermentazione& p) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json_str);
  if (err) return false;

  if (!doc.containsKey("steps") || !doc["steps"].is<JsonArray>()) return false;

  memset(&p, 0, sizeof(p));

  strncpy(p.nome, doc["profilo"] | "sconosciuto", sizeof(p.nome) - 1);
  p.avvio_epoch             = doc["avvio_epoch"]            | (uint32_t)0;
  p.isteresi                = doc["isteresi"]               | 0.3f;      // legacy, non usato
  p.min_cycle_minutes       = doc["min_cycle_minutes"]      | (uint32_t)5; // legacy, non usato
  p.allarme_timeout_minuti  = doc["allarme_timeout_minuti"] | (uint32_t)5;

  // Parametri PID riscaldatore (con default da config.h se assenti)
  p.kp_risc  = doc["kp_risc"]  | DEFAULT_PID_KP_RISC;
  p.ki_risc  = doc["ki_risc"]  | DEFAULT_PID_KI_RISC;
  p.kd_risc  = doc["kd_risc"]  | DEFAULT_PID_KD_RISC;

  // Parametri PID refrigeratore
  p.kp_frigo = doc["kp_frigo"] | DEFAULT_PID_KP_FRIGO;
  p.ki_frigo = doc["ki_frigo"] | DEFAULT_PID_KI_FRIGO;
  p.kd_frigo = doc["kd_frigo"] | DEFAULT_PID_KD_FRIGO;

  // Parametri PWM
  p.pwm_period_seconds   = doc["pwm_period_seconds"]   | (uint32_t)DEFAULT_PWM_PERIOD_S;
  p.min_pompa_on_seconds = doc["min_pompa_on_seconds"] | (uint32_t)DEFAULT_MIN_POMPA_ON_S;

  // Sanity check: evita valori fisicamente assurdi
  if (p.pwm_period_seconds < 10)  p.pwm_period_seconds   = DEFAULT_PWM_PERIOD_S;
  if (p.min_pompa_on_seconds < 1) p.min_pompa_on_seconds = DEFAULT_MIN_POMPA_ON_S;
  if (p.min_pompa_on_seconds > p.pwm_period_seconds / 2)
    p.min_pompa_on_seconds = p.pwm_period_seconds / 2;

  JsonArray steps = doc["steps"];
  p.num_steps = 0;
  for (JsonObject s : steps) {
    if (p.num_steps >= MAX_STEPS) break;
    ProfiloStep& ps = p.steps[p.num_steps];
    strncpy(ps.nome, s["step"] | "Step", sizeof(ps.nome) - 1);
    ps.temp = s["temp"] | 20.0f;

    if (s.containsKey("ramp_hours")) {
      ps.is_ramp        = true;
      ps.ramp_hours     = s["ramp_hours"].as<float>();
      ps.duration_hours = 0.0f;
    } else {
      ps.is_ramp        = false;
      ps.duration_hours = s["duration_hours"] | 24.0f;
      ps.ramp_hours     = 0.0f;
    }
    p.num_steps++;
  }

  p.valido = (p.num_steps > 0);
  return p.valido;
}

// ============================================================
// NVS — persistenza attraverso reset
// ============================================================

void salvaProfilo(const ProfiloFermentazione& p) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putBytes(NVS_KEY, &p, sizeof(ProfiloFermentazione));
  prefs.end();
}

bool caricaProfilo(ProfiloFermentazione& p) {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  size_t len = prefs.getBytes(NVS_KEY, &p, sizeof(ProfiloFermentazione));
  prefs.end();
  return (len == sizeof(ProfiloFermentazione) && p.valido);
}

void cancellaProfilo() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
}

void salvaEpochNoto(uint32_t epoch) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  ProfiloFermentazione tmp;
  size_t len = prefs.getBytes(NVS_KEY, &tmp, sizeof(tmp));
  if (len == sizeof(tmp) && tmp.valido) {
    tmp.ultimo_epoch_noto = epoch;
    prefs.putBytes(NVS_KEY, &tmp, sizeof(tmp));
  }
  prefs.end();
}
