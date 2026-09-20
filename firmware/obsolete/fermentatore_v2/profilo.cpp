#include "profilo.h"
#include <Preferences.h>
#include <string.h>

// ============================================================
// Calcolo setpoint
// ============================================================

float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi) {
  if (!p.valido || p.num_steps == 0) return -999.0f;

  float ore_correnti = secondi_trascorsi / 3600.0f;
  float t_start = 0.0f;
  // La temp di partenza della prima rampa è la temp dello step 0
  float temp_prev = p.steps[0].temp;

  for (int i = 0; i < p.num_steps; i++) {
    float durata = p.steps[i].is_ramp
                     ? p.steps[i].ramp_hours
                     : p.steps[i].duration_hours;
    float t_end = t_start + durata;

    if (ore_correnti >= t_start && ore_correnti < t_end) {
      if (p.steps[i].is_ramp) {
        // Interpolazione lineare tra temperatura precedente e questa
        float frac = (ore_correnti - t_start) / durata;
        return temp_prev + frac * (p.steps[i].temp - temp_prev);
      } else {
        return p.steps[i].temp;
      }
    }

    temp_prev = p.steps[i].temp;
    t_start   = t_end;
  }

  // Profilo terminato: mantieni la temp dell'ultimo step
  return p.steps[p.num_steps - 1].temp;
}

void stepCorrente(const ProfiloFermentazione& p, uint32_t secondi_trascorsi,
                  int& idx_out, float& ore_step_out, float& ore_totali_out) {
  ore_totali_out = secondi_trascorsi / 3600.0f;

  if (!p.valido || p.num_steps == 0) {
    idx_out      = -1;
    ore_step_out = 0.0f;
    return;
  }

  float t_start        = 0.0f;
  float ore_correnti   = ore_totali_out;

  for (int i = 0; i < p.num_steps; i++) {
    float durata = p.steps[i].is_ramp
                     ? p.steps[i].ramp_hours
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
// ============================================================

bool parsaProfiloJSON(const char* json_str, ProfiloFermentazione& p) {
  // Buffer da 4 KB per gestire profili con molti step
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json_str);
  if (err) return false;

  memset(&p, 0, sizeof(p));

  strncpy(p.nome, doc["profilo"] | "sconosciuto", sizeof(p.nome) - 1);
  p.avvio_epoch             = doc["avvio_epoch"]           | 0;
  p.isteresi                = doc["isteresi"]              | 0.3f;
  p.min_cycle_minutes       = doc["min_cycle_minutes"]     | 5;
  p.allarme_timeout_minuti  = doc["allarme_timeout_minuti"]| 5;

  JsonArray steps = doc["steps"];
  if (steps.isNull()) return false;

  p.num_steps = 0;
  for (JsonObject s : steps) {
    if (p.num_steps >= MAX_STEPS) break;
    ProfiloStep& ps = p.steps[p.num_steps];
    strncpy(ps.nome, s["step"] | "Step", sizeof(ps.nome) - 1);
    ps.temp = s["temp"] | 20.0f;

    if (s.containsKey("ramp_hours")) {
      ps.is_ramp        = true;
      ps.ramp_hours     = s["ramp_hours"];
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
// NVS (Preferences) — persistenza attraverso reset
// ============================================================

static const char* NVS_NS = "ferm_v2";
static const char* NVS_KEY = "profilo";

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
