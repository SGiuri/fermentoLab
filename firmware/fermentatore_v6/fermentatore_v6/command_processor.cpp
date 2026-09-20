#include "command_processor.h"
#include "config.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static bool topic_ends_with(const char* topic, const char* suffix) {
  size_t tlen = strlen(topic);
  size_t slen = strlen(suffix);
  if (tlen < slen) return false;
  return strcmp(topic + (tlen - slen), suffix) == 0;
}

static CommandType resolve_command_type(const char* topic) {
  if (topic_ends_with(topic, "/start")) return CommandType::START;
  if (topic_ends_with(topic, "/stop")) return CommandType::STOP;
  if (topic_ends_with(topic, "/reboot")) return CommandType::REBOOT;
  if (topic_ends_with(topic, "/profile_set")) return CommandType::PROFILE_SET;
  if (topic_ends_with(topic, "/control_set")) return CommandType::CONTROL_SET;
  if (topic_ends_with(topic, "/autotune_start")) return CommandType::AUTOTUNE_START;
  if (topic_ends_with(topic, "/autotune_stop")) return CommandType::AUTOTUNE_STOP;
  if (topic_ends_with(topic, "/autotune_apply")) return CommandType::AUTOTUNE_APPLY;
  return CommandType::NONE;
}

static bool parse_control_mode(const char* mode_str, ControlMode& out) {
  if (strcmp(mode_str, "on_off") == 0) { out = ControlMode::ON_OFF; return true; }
  if (strcmp(mode_str, "on_off_hyst_sym") == 0) { out = ControlMode::ON_OFF_HYST_SYM; return true; }
  if (strcmp(mode_str, "on_off_hyst_asym") == 0) { out = ControlMode::ON_OFF_HYST_ASYM; return true; }
  if (strcmp(mode_str, "p")  == 0 || strcmp(mode_str, "P")   == 0) { out = ControlMode::P;       return true; }
  if (strcmp(mode_str, "pi") == 0 || strcmp(mode_str, "PI")  == 0) { out = ControlMode::PI_CTRL;  return true; }
  if (strcmp(mode_str, "pd") == 0 || strcmp(mode_str, "PD")  == 0) { out = ControlMode::PD_CTRL;  return true; }
  if (strcmp(mode_str, "pid")== 0 || strcmp(mode_str, "PID") == 0) { out = ControlMode::PID_CTRL; return true; }
  return false;
}

static bool guadagno_valido(float v, float max_v) {
  return isfinite(v) && v >= 0.0f && v <= max_v;
}

bool parse_command(const char* topic,
                   const char* payload,
                   CommandRequest& out,
                   char* reason_buf,
                   size_t reason_buf_size) {
  memset(&out, 0, sizeof(out));
  out.type = resolve_command_type(topic);
  out.require_quality_score_gte = AUTOTUNE_REQUIRED_QUALITY_SCORE;
  out.avvia = true;

  if (out.type == CommandType::NONE) {
    snprintf(reason_buf, reason_buf_size, "unknown_command_topic");
    return false;
  }

  // Filtro: estrae solo i campi comuni. Il profilo completo (fino a MAX_STEPS step)
  // viene parsato a parte da parsaProfiloJSON; prima un documento da 1 KB
  // rifiutava come invalid_json i profili con più di ~10 step.
  StaticJsonDocument<256> filtro;
  filtro["schema_version"] = true;
  filtro["request_id"] = true;
  filtro["ts"] = true;
  filtro["control_mode"] = true;
  filtro["params"] = true;
  filtro["avvia"] = true;
  filtro["require_quality_score_gte"] = true;

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filtro));
  if (err) {
    snprintf(reason_buf, reason_buf_size, "invalid_json");
    return false;
  }

  const char* schema = doc["schema_version"] | "";
  if (strcmp(schema, SCHEMA_VERSION) != 0) {
    snprintf(reason_buf, reason_buf_size, "invalid_schema_version");
    return false;
  }

  const char* req_id = doc["request_id"] | "";
  if (strlen(req_id) == 0) {
    snprintf(reason_buf, reason_buf_size, "missing_request_id");
    return false;
  }
  strncpy(out.request_id, req_id, sizeof(out.request_id) - 1);

  JsonVariantConst ts = doc["ts"];
  if (ts.is<const char*>()) {
    out.ts = (uint32_t)strtoul(ts.as<const char*>(), nullptr, 10);
  } else {
    out.ts = ts | (uint32_t)0;
  }

  if (out.type == CommandType::CONTROL_SET) {
    const char* mode = doc["control_mode"] | "";
    if (strlen(mode) == 0) {
      snprintf(reason_buf, reason_buf_size, "missing_control_mode");
      return false;
    }
    out.has_control_mode = parse_control_mode(mode, out.requested_mode);
    if (!out.has_control_mode) {
      snprintf(reason_buf, reason_buf_size, "unsupported_control_mode");
      return false;
    }

    JsonObjectConst p = doc["params"];
    if (!p.isNull()) {
      out.has_pid_params = p.containsKey("kp_heat") && p.containsKey("ki_heat") && p.containsKey("kd_heat") &&
                           p.containsKey("kp_cool") && p.containsKey("ki_cool") && p.containsKey("kd_cool");
      if (out.has_pid_params) {
        out.kp_heat = p["kp_heat"].as<float>();
        out.ki_heat = p["ki_heat"].as<float>();
        out.kd_heat = p["kd_heat"].as<float>();
        out.kp_cool = p["kp_cool"].as<float>();
        out.ki_cool = p["ki_cool"].as<float>();
        out.kd_cool = p["kd_cool"].as<float>();
        const bool validi = guadagno_valido(out.kp_heat, 1000.0f) && guadagno_valido(out.ki_heat, 100.0f) &&
                            guadagno_valido(out.kd_heat, 100000.0f) &&
                            guadagno_valido(out.kp_cool, 1000.0f) && guadagno_valido(out.ki_cool, 100.0f) &&
                            guadagno_valido(out.kd_cool, 100000.0f);
        if (!validi) {
          snprintf(reason_buf, reason_buf_size, "invalid_pid_params");
          return false;
        }
      }

      struct { const char* chiave; bool* presente; float* valore; } isteresi[] = {
        {"isteresi", &out.has_isteresi, &out.isteresi},
        {"isteresi_caldo", &out.has_isteresi_caldo, &out.isteresi_caldo},
        {"isteresi_freddo", &out.has_isteresi_freddo, &out.isteresi_freddo},
      };
      for (auto& x : isteresi) {
        if (!p.containsKey(x.chiave)) continue;
        const float v = p[x.chiave].as<float>();
        if (!isfinite(v) || v < ISTERESI_MIN_C || v > ISTERESI_MAX_C) {
          snprintf(reason_buf, reason_buf_size, "invalid_isteresi");
          return false;
        }
        *x.presente = true;
        *x.valore = v;
      }
    }
  }

  if (out.type == CommandType::PROFILE_SET) {
    out.avvia = doc["avvia"] | true;
  }

  if (out.type == CommandType::AUTOTUNE_APPLY) {
    out.require_quality_score_gte = doc["require_quality_score_gte"] | AUTOTUNE_REQUIRED_QUALITY_SCORE;
  }

  snprintf(reason_buf, reason_buf_size, "ok");
  return true;
}

const char* command_type_to_string(CommandType type) {
  switch (type) {
    case CommandType::START: return "start";
    case CommandType::STOP: return "stop";
    case CommandType::REBOOT: return "reboot";
    case CommandType::PROFILE_SET: return "profile_set";
    case CommandType::CONTROL_SET: return "control_set";
    case CommandType::AUTOTUNE_START: return "autotune_start";
    case CommandType::AUTOTUNE_STOP: return "autotune_stop";
    case CommandType::AUTOTUNE_APPLY: return "autotune_apply";
    default: return "none";
  }
}
