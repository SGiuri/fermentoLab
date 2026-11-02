#include <Arduino.h>
#include "display.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>

// ============================================================
// Oggetto display — SSD1306 128x64, I2C hardware, full buffer.
// Full buffer (suffisso _F_): clearBuffer+draw+sendBuffer senza flickering.
// I pin SDA/SCL vengono impostati da Wire.begin() in display_init().
// ============================================================
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C _oled(U8G2_R0, U8X8_PIN_NONE);

// ============================================================
// Layout costanti (pixel)
//
//  ┌──────────────────────────────────────────────────────────┐ y=0
//  │ [R|] [nome profilo scorrevole ...........]  [fiamma/neve]│ y=11  header
//  ├──────────────────────────────────────────────────────────┤ y=13  linea
//  │  18.4°C                      SP: 18.5°C                  │ y=30  temperature
//  │  Ferm. Prim.                              -2.3d          │ y=43  step+gg
//  │  [#########.............]  42%                           │ y=56  progress
//  └──────────────────────────────────────────────────────────┘ y=63
// ============================================================

// ────────────────────────────────────────────────────────────
// Helper: centra una stringa sull'asse X (128px)
// ────────────────────────────────────────────────────────────
static int center_x(const char* str) {
  int w = (int)_oled.getStrWidth(str);
  int x = (128 - w) / 2;
  return (x < 0) ? 0 : x;
}

// ────────────────────────────────────────────────────────────
// Helper: fiammella stilizzata 6×8 px (riscaldamento attivo)
// ────────────────────────────────────────────────────────────
static void draw_flame_symbol(int x, int y) {
  _oled.drawPixel(x + 3, y + 0);
  _oled.drawLine(x + 2, y + 1, x + 4, y + 1);
  _oled.drawLine(x + 1, y + 2, x + 5, y + 2);
  _oled.drawLine(x + 1, y + 3, x + 5, y + 3);
  _oled.drawLine(x + 1, y + 4, x + 5, y + 4);
  _oled.drawLine(x + 1, y + 5, x + 5, y + 5);
  _oled.drawLine(x + 2, y + 6, x + 4, y + 6);
  _oled.drawLine(x + 2, y + 7, x + 4, y + 7);
}

static void draw_cold_symbol(int x, int y) {
  _oled.drawVLine(x + 4, y + 1, 7);
  _oled.drawHLine(x + 1, y + 4, 7);
  _oled.drawLine(x + 2, y + 2, x + 6, y + 6);
  _oled.drawLine(x + 6, y + 2, x + 2, y + 6);
}

// ────────────────────────────────────────────────────────────
// Helper: icona relè unica (fiamma o fiocco) in alto a destra.
// ────────────────────────────────────────────────────────────
static void draw_relay_icon(bool risc, bool frigo) {
  if (!risc && !frigo) return;
  _oled.drawFrame(116, 0, 12, 12);
  if (risc) {
    draw_flame_symbol(119, 2);
  } else {
    draw_cold_symbol(117, 2);
  }
}

// ────────────────────────────────────────────────────────────
// Helper: testo scorrevole da destra a sinistra (ticker).
// ────────────────────────────────────────────────────────────
static void draw_scrolling_nome(const char* text, int clip_x, int clip_w, int y) {
  if (!text || !text[0]) return;
  _oled.setFont(u8g2_font_6x10_tf);
  int text_w = (int)_oled.getStrWidth(text);
  if (text_w <= clip_w) {
    _oled.drawStr(clip_x, y, text);
    return;
  }
  const int gap = 20;
  uint32_t cycle = (uint32_t)(text_w + gap);
  int offset = (int)((millis() / 45UL) % cycle);
  int draw_x = clip_x + clip_w - offset;
  _oled.setClipWindow(clip_x, y - 10, clip_x + clip_w - 1, y + 1);
  _oled.drawStr(draw_x, y, text);
  _oled.drawStr(draw_x + (int)cycle, y, text);
  _oled.setMaxClipWindow();
}

// ────────────────────────────────────────────────────────────
// Helper: abbrevia etichette note per migliorare leggibilità
// ────────────────────────────────────────────────────────────
static void abbrevia_nome(const char* src, char* out, uint8_t max_chars) {
  if (!src || !src[0]) { out[0] = '\0'; return; }

  if      (strcmp(src, "Fermentazione primaria")   == 0) { strncpy(out, "Ferm. Prim.", max_chars); }
  else if (strcmp(src, "Fermentazione secondaria") == 0) { strncpy(out, "Ferm. Sec.", max_chars); }
  else if (strcmp(src, "Maturazione")              == 0) { strncpy(out, "Maturaz.", max_chars); }
  else if (strcmp(src, "Diacetyl rest")            == 0) { strncpy(out, "Dia. Rest", max_chars); }
  else if (strcmp(src, "Cold crash")               == 0) { strncpy(out, "ColdCr.", max_chars); }
  else {
    uint8_t w = 0;
    const char* p = src;
    while (*p && w < max_chars) {
      while (*p == ' ') p++;
      if (!*p) break;
      const char* start = p;
      uint8_t len = 0;
      while (*p && *p != ' ') { p++; len++; }
      uint8_t keep = (len > 7) ? 4 : len;
      for (uint8_t i = 0; i < keep && w < max_chars; i++) out[w++] = start[i];
      if (len > 7 && w < max_chars) out[w++] = '.';
      if (*p == ' ' && w < max_chars) out[w++] = ' ';
    }
    if (w > 0 && out[w - 1] == ' ') w--;
    out[w] = '\0';
    return;
  }
  out[max_chars] = '\0';
}

static void draw_progress_bar(int x, int y, int w, int h, float pct) {
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  _oled.drawFrame(x, y, w, h);
  int fill = (int)((pct * (float)(w - 2)) / 100.0f);
  if (fill > 0) _oled.drawBox(x + 1, y + 1, fill, h - 2);
}

// ────────────────────────────────────────────────────────────
// Schermata BOOT (WiFi / MQTT / stati transitori)
// ────────────────────────────────────────────────────────────
static void draw_boot(const char* linea1, const char* linea2) {
  _oled.setFont(u8g2_font_7x13_tf);
  const char* titolo = "FERMENTATORE V6";
  _oled.drawStr(center_x(titolo), 20, titolo);
  _oled.drawHLine(10, 23, 108);

  _oled.setFont(u8g2_font_6x10_tf);
  _oled.drawStr(center_x(linea1), 40, linea1);
  if (linea2 && linea2[0]) {
    _oled.drawStr(center_x(linea2), 55, linea2);
  }
}

// ────────────────────────────────────────────────────────────
// Schermata ALLARME — barra invertita + countdown riavvio
// ────────────────────────────────────────────────────────────
static void draw_allarme(const DisplayData& d) {
  _oled.drawBox(0, 0, 128, 16);
  _oled.setDrawColor(0);
  _oled.setFont(u8g2_font_7x13_tf);
  const char* label = "!! ALLARME !!";
  _oled.drawStr(center_x(label), 13, label);
  _oled.setDrawColor(1);

  _oled.setFont(u8g2_font_6x10_tf);
  const char* msg = d.stima_attiva ? "Sensore off: STIMA" : "Sensore offline";
  _oled.drawStr(center_x(msg), 32, msg);

  long rimasto_ms = (long)SENSOR_REBOOT_DELAY_MS
                    - (long)(millis() - d.ts_ingresso_allarme_ms);
  int secs = (rimasto_ms > 0) ? (int)(rimasto_ms / 1000) : 0;
  char buf[24];
  snprintf(buf, sizeof(buf), "Riavvio in %ds", secs);
  _oled.drawStr(center_x(buf), 47, buf);

  if (d.temp_valida) {
    snprintf(buf, sizeof(buf), "T: %.1f\xb0""C", d.temp_reale);
  } else {
    snprintf(buf, sizeof(buf), "T: --.-\xb0""C");
  }
  _oled.drawStr(center_x(buf), 62, buf);
}

// ────────────────────────────────────────────────────────────
// Schermata OPERATIVA (RUNNING / STANDBY / HOLDING)
// ────────────────────────────────────────────────────────────
static void draw_operational(const DisplayData& d) {
  char buf[32];
  char stato_buf[24];

  // ── Header ──────────────────────────────────────────────
  _oled.setFont(u8g2_font_6x10_tf);
  const bool is_running = (strcmp(d.stato_fsm, "RUNNING") == 0);
  const bool is_holding = (strcmp(d.stato_fsm, "HOLDING") == 0);
  if (is_running) {
    const char spinner[4] = {'|', '/', '-', '\\'};
    snprintf(stato_buf, sizeof(stato_buf), "R%c", spinner[(millis() / 250UL) % 4]);
    _oled.drawStr(0, 11, stato_buf);
    draw_scrolling_nome(d.profilo_nome, 14, 101, 11);
  } else if (is_holding) {
    _oled.drawStr(0, 11, "HOLD");
    draw_scrolling_nome(d.profilo_nome, 26, 89, 11);
  } else {
    _oled.drawStr(0, 11, d.stato_fsm);
  }
  draw_relay_icon(d.riscaldatore_on, d.refrigeratore_on);
  _oled.drawHLine(0, 13, 128);

  // ── Temperatura reale (grande, sinistra) ────────────────
  _oled.setFont(u8g2_font_9x15_tf);
  if (d.temp_valida) {
    snprintf(buf, sizeof(buf), "%.1f\xb0""C", d.temp_reale);
  } else {
    snprintf(buf, sizeof(buf), "--.-\xb0""C");
  }
  _oled.drawStr(0, 30, buf);

  // ── Setpoint (piccolo, allineato a destra) ──────────────
  _oled.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "SP:%.1f\xb0""C", d.setpoint);
  int sp_x = 128 - (int)_oled.getStrWidth(buf);
  _oled.drawStr(sp_x, 30, buf);

  // ── Info step / stato ────────────────────────────────────
  if (is_running) {
    char step_buf[22];
    abbrevia_nome(d.step_nome, step_buf, 21);
    step_buf[21] = '\0';
    _oled.drawStr(0, 43, step_buf);
    if (d.giorni_prossimo_step >= 0.0f) {
      snprintf(buf, sizeof(buf), "-%.1fd", d.giorni_prossimo_step);
      int dx = 128 - (int)_oled.getStrWidth(buf);
      _oled.drawStr(dx, 43, buf);
    }
    draw_progress_bar(0, 48, 86, 10, d.progresso_pct);
    snprintf(buf, sizeof(buf), "%d%%", (int)(d.progresso_pct + 0.5f));
    _oled.drawStr(89, 56, buf);

  } else if (is_holding) {
    char prof_buf[22];
    abbrevia_nome(d.profilo_nome, prof_buf, 21);
    _oled.drawStr(0, 43, prof_buf);
    _oled.drawStr(0, 56, "Completato");

  } else {
    // STANDBY
    _oled.drawStr(0, 43, "Nessun profilo");
    if (d.wifi_offline) {
      _oled.drawStr(0, 56, "[!] WiFi offline");
    } else {
      snprintf(buf, sizeof(buf), "Target: %.1f\xb0""C", d.setpoint);
      _oled.drawStr(0, 56, buf);
    }
  }
}

// ============================================================
// API pubblica
// ============================================================

void display_init() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  _oled.setI2CAddress(OLED_I2C_ADDR << 1);
  _oled.begin();

  _oled.clearBuffer();
  _oled.setFont(u8g2_font_7x13_tf);
  const char* titolo = "FERMENTATORE V6";
  _oled.drawStr((128 - (int)_oled.getStrWidth(titolo)) / 2, 28, titolo);
  _oled.setFont(u8g2_font_6x10_tf);
  const char* sub = "Avvio...";
  _oled.drawStr((128 - (int)_oled.getStrWidth(sub)) / 2, 46, sub);
  _oled.sendBuffer();

  Serial.print("FERMENTATORE V6 v");
  Serial.println(FIRMWARE_VERSION);
}

void display_aggiorna(const DisplayData& d) {
  // Un disturbo sull'alimentazione (commutazione della pompa) può resettare l'SSD1306: riparte spento e
  // resta nero mentre il firmware continua a mandargli il buffer. Ogni DISPLAY_REINIT_MS si rimanda la
  // sequenza di inizializzazione (la RAM del display resta, il buffer viene rimandato subito sotto).
  static unsigned long ts_reinit = 0;
  if (millis() - ts_reinit >= DISPLAY_REINIT_MS) {
    ts_reinit = millis();
    _oled.initDisplay();
    _oled.setPowerSave(0);
  }

  _oled.clearBuffer();

  const char* s = d.stato_fsm ? d.stato_fsm : "";

  if (strcmp(s, "ALLARME") == 0) {
    draw_allarme(d);

  } else if (strcmp(s, "RUNNING") == 0 ||
             strcmp(s, "STANDBY") == 0 ||
             strcmp(s, "HOLDING") == 0) {
    draw_operational(d);

  } else {
    // WIFI / MQTT / AUTOTUNE / altri stati transitori
    const char* l1 = s;
    const char* l2 = "";
    if      (strcmp(s, "WIFI")     == 0) { l1 = "Connessione";   l2 = "WiFi..."; }
    else if (strcmp(s, "MQTT")     == 0) { l1 = "Connetto MQTT"; l2 = d.wifi_ssid[0] ? d.wifi_ssid : "..."; }
    else if (strcmp(s, "AUTOTUNE") == 0) { l1 = "Autotuning";    l2 = "in corso..."; }
    draw_boot(l1, l2);
  }

  _oled.sendBuffer();
}

const char* display_firmware_version() {
  return FIRMWARE_VERSION;
}
