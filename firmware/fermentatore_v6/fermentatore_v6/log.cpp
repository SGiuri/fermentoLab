#include "log.h"
#include <stdarg.h>

static LogMqttFn _mqtt_fn = nullptr;

static const char* liv_serial(uint8_t liv) {
  switch (liv) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN ";
    case LOG_INFO:  return "INFO ";
    case LOG_DEBUG: return "DEBUG";
    default:        return "?    ";
  }
}

static const char* liv_mqtt(uint8_t liv) {
  switch (liv) {
    case LOG_ERROR: return "ERROR";
    case LOG_WARN:  return "WARN";
    case LOG_INFO:  return "INFO";
    case LOG_DEBUG: return "DEBUG";
    default:        return "?";
  }
}

// Escape minimale: sostituisce " e \ per JSON valido nel campo "msg".
static void json_escape(const char* src, char* dst, size_t dst_len) {
  size_t di = 0;
  for (size_t i = 0; src[i] != '\0' && di < dst_len - 2; i++) {
    if (src[i] == '"' || src[i] == '\\') {
      dst[di++] = '\\';
    }
    dst[di++] = src[i];
  }
  dst[di] = '\0';
}

void log_init(LogMqttFn fn) {
  _mqtt_fn = fn;
}

void _log_write(uint8_t livello, const char* modulo, const char* fmt, ...) {
  char msg[240];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  const unsigned long ts = millis();

#if LOG_SERIAL_LEVEL > LOG_NONE
  if (livello <= LOG_SERIAL_LEVEL) {
    Serial.printf("[%8lums] [%s] [%-8s] %s\n",
                  ts, liv_serial(livello), modulo, msg);
  }
#endif

#if LOG_MQTT_LEVEL > LOG_NONE
  if (livello <= LOG_MQTT_LEVEL && _mqtt_fn != nullptr) {
    char safe_msg[256];
    json_escape(msg, safe_msg, sizeof(safe_msg));
    char buf[360];
    snprintf(buf, sizeof(buf),
             "{\"t\":%lu,\"liv\":\"%s\",\"mod\":\"%s\",\"msg\":\"%s\"}",
             ts, liv_mqtt(livello), modulo, safe_msg);
    _mqtt_fn(buf);
  }
#endif
}
