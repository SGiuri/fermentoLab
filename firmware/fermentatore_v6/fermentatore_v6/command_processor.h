#pragma once
#include <Arduino.h>
#include "controller.h"

enum class CommandType : uint8_t {
  NONE = 0,
  START,
  STOP,
  REBOOT,
  PROFILE_SET,
  CONTROL_SET,
  AUTOTUNE_START,
  AUTOTUNE_STOP,
  AUTOTUNE_APPLY
};

struct CommandRequest {
  CommandType type;
  char request_id[48];
  uint32_t ts;            // epoch secondi; 0 = assente (accetta anche stringa numerica)

  // control_set
  bool has_control_mode;
  ControlMode requested_mode;
  bool has_pid_params;
  float kp_heat;
  float ki_heat;
  float kd_heat;
  float kp_cool;
  float ki_cool;
  float kd_cool;

  // control_set: isteresi on/off [°C]; "isteresi" imposta simmetrica, caldo e freddo insieme
  bool has_isteresi;
  float isteresi;
  bool has_isteresi_caldo;
  float isteresi_caldo;
  bool has_isteresi_freddo;
  float isteresi_freddo;

  // profile_set: false = salva il profilo senza avviare (default true, comportamento storico)
  bool avvia;

  // autotune_apply
  float require_quality_score_gte;
};

bool parse_command(const char* topic,
                   const char* payload,
                   CommandRequest& out,
                   char* reason_buf,
                   size_t reason_buf_size);
const char* command_type_to_string(CommandType type);
