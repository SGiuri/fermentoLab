#include "profilo.h"
#include "config.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char* NVS_NS  = NVS_NS_FERM;
static const char* NVS_KEY = "profilo";
static const char* NVS_KEY_PROGRESSO = "progresso_s";

// ============================================================
// Calcolo setpoint
// ============================================================

float calcolaSetpoint(const ProfiloFermentazione& p, uint32_t secondi_trascorsi) {
  if (!p.valido || p.num_steps == 0) return -999.0f;

  float ore_correnti = secondi_trascorsi / 3600.0f;
  float t_start      = 0.0f;
  float temp_prev    = p.steps[0].temp;

  for (int i = 0; i < p.num_steps; i++) {
    const float durata = p.steps[i].is_ramp ? p.steps[i].ramp_hours
                                            : p.steps[i].duration_hours;
    const float t_end  = t_start + durata;

    if (ore_correnti >= t_start && ore_correnti < t_end) {
      if (p.steps[i].is_ramp) {
        const float frac = (ore_correnti - t_start) / durata;
        return temp_prev + frac * (p.steps[i].temp - temp_prev);
      } else {
        return p.steps[i].temp;
      }
    }

    temp_prev = p.steps[i].temp;
    t_start   = t_end;
  }

  // Profilo terminato: mantieni la temperatura dell'ultimo step.
  return p.steps[p.num_steps - 1].temp;
}

float calcolaDurataTotaleOre(const ProfiloFermentazione& p) {
  if (!p.valido || p.num_steps == 0) return 0.0f;
  float totale = 0.0f;
  for (int i = 0; i < p.num_steps; i++) {
    totale += p.steps[i].is_ramp ? p.steps[i].ramp_hours
                                 : p.steps[i].duration_hours;
  }
  return totale;
}

// ============================================================
// Parsing JSON
// Compatibile con il payload completo di profile_set (contiene anche
// schema_version/request_id che vengono ignorati).
// ============================================================

static bool rifiuta(char* reason_buf, size_t reason_buf_size, const char* motivo) {
  if (reason_buf && reason_buf_size > 0) {
    snprintf(reason_buf, reason_buf_size, "%s", motivo);
  }
  return false;
}

// I nomi finiscono non escapati nel JSON di esp/state: virgolette o backslash
// renderebbero invalido l'intero payload e cieco il monitoraggio HA.
static void sanifica_nome(char* s) {
  for (; *s; s++) {
    if (*s == '"' || *s == '\\' || (uint8_t)*s < 0x20) *s = '\'';
  }
}

static uint32_t leggi_uint32(JsonVariantConst v) {
  if (v.is<const char*>()) return (uint32_t)strtoul(v.as<const char*>(), nullptr, 10);
  return v | (uint32_t)0;
}

bool parsaProfiloJSON(const char* json_str,
                      ProfiloFermentazione& p,
                      char* reason_buf,
                      size_t reason_buf_size) {
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, json_str)) {
    return rifiuta(reason_buf, reason_buf_size, "invalid_profile_json");
  }

  JsonArrayConst steps = doc["steps"].as<JsonArrayConst>();
  if (steps.isNull()) return rifiuta(reason_buf, reason_buf_size, "missing_steps");
  if (steps.size() == 0) return rifiuta(reason_buf, reason_buf_size, "empty_steps");
  if (steps.size() > MAX_STEPS) return rifiuta(reason_buf, reason_buf_size, "too_many_steps");

  const uint32_t avvio = leggi_uint32(doc["avvio_epoch"]);
  if (avvio != 0 && avvio < EPOCH_MINIMO_VALIDO) {
    return rifiuta(reason_buf, reason_buf_size, "invalid_avvio_epoch");
  }

  static ProfiloFermentazione tmp;  // static: 1.3 KB fuori dallo stack del callback MQTT
  memset(&tmp, 0, sizeof(tmp));

  strncpy(tmp.nome, doc["profilo"] | "sconosciuto", sizeof(tmp.nome) - 1);
  sanifica_nome(tmp.nome);
  tmp.avvio_epoch = avvio;

  for (JsonObjectConst s : steps) {
    ProfiloStep& ps = tmp.steps[tmp.num_steps];
    strncpy(ps.nome, s["step"] | "Step", sizeof(ps.nome) - 1);
    sanifica_nome(ps.nome);

    if (!s["temp"].is<float>()) {
      return rifiuta(reason_buf, reason_buf_size, "step_temp_missing");
    }
    ps.temp = s["temp"].as<float>();
    if (!isfinite(ps.temp) || ps.temp < DEFAULT_MIN_SETPOINT_C || ps.temp > DEFAULT_MAX_SETPOINT_C) {
      return rifiuta(reason_buf, reason_buf_size, "step_temp_out_of_range");
    }

    float ore = 0.0f;
    if (s.containsKey("ramp_hours")) {
      ps.is_ramp        = true;
      ore               = s["ramp_hours"].as<float>();
      ps.ramp_hours     = ore;
      ps.duration_hours = 0.0f;
    } else if (s.containsKey("duration_hours")) {
      ps.is_ramp        = false;
      ore               = s["duration_hours"].as<float>();
      ps.duration_hours = ore;
      ps.ramp_hours     = 0.0f;
    } else {
      return rifiuta(reason_buf, reason_buf_size, "step_duration_missing");
    }
    if (!isfinite(ore) || ore <= 0.0f || ore > PROFILO_STEP_MAX_ORE) {
      return rifiuta(reason_buf, reason_buf_size, "step_duration_invalid");
    }
    tmp.num_steps++;
  }

  tmp.valido = true;
  p = tmp;
  if (reason_buf && reason_buf_size > 0) reason_buf[0] = '\0';
  return true;
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
  const size_t len = prefs.getBytes(NVS_KEY, &p, sizeof(ProfiloFermentazione));
  const uint32_t progresso = prefs.getUInt(NVS_KEY_PROGRESSO, 0);
  prefs.end();
  if (len != sizeof(ProfiloFermentazione) || !p.valido) return false;
  if (p.num_steps <= 0 || p.num_steps > MAX_STEPS) return false;
  // Il progresso è persistito a parte, più di frequente del blob intero (vedi handle_persistenza):
  // prendere il più avanzato dei due, non può mai tornare indietro.
  if (progresso > p.secondi_trascorsi) p.secondi_trascorsi = progresso;
  return true;
}

void cancellaProfilo() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
}

void salvaProgresso(uint32_t secondi_trascorsi) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putUInt(NVS_KEY_PROGRESSO, secondi_trascorsi);
  prefs.end();
}

// ============================================================
// NVS — stato FSM per recovery autonomo al boot
// Chiavi separate: "last_fsm", "last_step_idx", "ore_save"
// ============================================================

static const char* NVS_KEY_FSM  = "last_fsm";
static const char* NVS_KEY_STEP = "last_step_idx";
static const char* NVS_KEY_ORE  = "ore_save";

void salvaStatoFsm(const char* fsm_str, int step_index, float ore_trascorse) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putString(NVS_KEY_FSM, fsm_str);
  prefs.putInt(NVS_KEY_STEP, step_index);
  prefs.putFloat(NVS_KEY_ORE, ore_trascorse);
  prefs.end();
}

bool caricaStatoFsm(char* fsm_buf, size_t buf_size, int& step_index, float& ore_trascorse) {
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  String fsm = prefs.getString(NVS_KEY_FSM, "");
  step_index   = prefs.getInt(NVS_KEY_STEP, 0);
  ore_trascorse = prefs.getFloat(NVS_KEY_ORE, 0.0f);
  prefs.end();
  if (fsm.length() == 0) return false;
  strncpy(fsm_buf, fsm.c_str(), buf_size - 1);
  fsm_buf[buf_size - 1] = '\0';
  return true;
}

void cancellaStatoFsm() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.remove(NVS_KEY_FSM);
  prefs.remove(NVS_KEY_STEP);
  prefs.remove(NVS_KEY_ORE);
  prefs.end();
}

// ============================================================
// NVS — configurazione controllo (modo + parametri)
// ============================================================

static const char*    NVS_KEY_CTRL      = "ctrl_cfg";
static const uint32_t CTRL_CFG_MAGIC    = 0x46364343UL;  // "F6CC"
static const uint16_t CTRL_CFG_VERSIONE = 1;

struct ConfigControlloNvs {
  uint32_t         magic;
  uint16_t         versione;
  uint16_t         dimensione;
  ControllerConfig config;
};

static bool valore_ok(float v, float lo, float hi) {
  return isfinite(v) && v >= lo && v <= hi;
}

static bool config_controllo_valida(const ControllerConfig& c) {
  if ((uint8_t)c.mode > (uint8_t)ControlMode::PID_CTRL) return false;
  return valore_ok(c.hysteresis_sym_c, 0.01f, 5.0f) &&
         valore_ok(c.hysteresis_heat_c, 0.01f, 5.0f) &&
         valore_ok(c.hysteresis_cool_c, 0.01f, 5.0f) &&
         valore_ok(c.kp_heat, 0.0f, 1000.0f) && valore_ok(c.ki_heat, 0.0f, 100.0f) &&
         valore_ok(c.kd_heat, 0.0f, 100000.0f) &&
         valore_ok(c.kp_cool, 0.0f, 1000.0f) && valore_ok(c.ki_cool, 0.0f, 100.0f) &&
         valore_ok(c.kd_cool, 0.0f, 100000.0f) &&
         valore_ok(c.output_deadband_pct, 0.0f, 100.0f) &&
         valore_ok(c.min_effective_output_pct, 0.0f, 100.0f);
}

void salvaConfigControllo(const ControllerConfig& config) {
  ConfigControlloNvs blob;
  memset(&blob, 0, sizeof(blob));
  blob.magic      = CTRL_CFG_MAGIC;
  blob.versione   = CTRL_CFG_VERSIONE;
  blob.dimensione = (uint16_t)sizeof(ControllerConfig);
  blob.config     = config;

  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putBytes(NVS_KEY_CTRL, &blob, sizeof(blob));
  prefs.end();
}

bool caricaConfigControllo(ControllerConfig& config) {
  ConfigControlloNvs blob;
  Preferences prefs;
  prefs.begin(NVS_NS, true);
  const size_t len = prefs.getBytes(NVS_KEY_CTRL, &blob, sizeof(blob));
  prefs.end();

  if (len != sizeof(blob)) return false;
  if (blob.magic != CTRL_CFG_MAGIC || blob.versione != CTRL_CFG_VERSIONE) return false;
  if (blob.dimensione != sizeof(ControllerConfig)) return false;
  if (!config_controllo_valida(blob.config)) return false;
  config = blob.config;
  return true;
}
