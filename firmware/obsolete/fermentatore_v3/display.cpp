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
//  │ [STATO FSM]                         [R☐] [F☐]           │ y=11  header
//  ├──────────────────────────────────────────────────────────┤ y=13  linea
//  │  18.4°C                      SP: 18.5°C                  │ y=36  temperature
//  │  Ammonic. Primaria                                        │ y=49  step
//  │  Stp: 2.4h   Tot: 24.1h                                  │ y=62  tempi
//  └──────────────────────────────────────────────────────────┘ y=63
// ============================================================

// ────────────────────────────────────────────────────────────
// Helper: stringa sicura (tronca a max_chars, aggiunge NUL)
// ────────────────────────────────────────────────────────────
static void tronca(const char* src, char* out, uint8_t max_chars) {
  if (!src) { out[0] = '\0'; return; }
  strncpy(out, src, max_chars);
  out[max_chars] = '\0';
}

// ────────────────────────────────────────────────────────────
// Helper: centra una stringa sull'asse X (128px)
// ────────────────────────────────────────────────────────────
static int center_x(const char* str) {
  int w = (int)_oled.getStrWidth(str);
  int x = (128 - w) / 2;
  return (x < 0) ? 0 : x;
}

// ────────────────────────────────────────────────────────────
// Helper: formatta ore decimali in hh:mm (tempo trascorso)
// ────────────────────────────────────────────────────────────
static void format_hhmm(float ore, char* out, size_t out_sz) {
  if (ore < 0.0f) ore = 0.0f;
  uint32_t minuti = (uint32_t)(ore * 60.0f);
  uint32_t hh = minuti / 60UL;
  uint32_t mm = minuti % 60UL;
  snprintf(out, out_sz, "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
}

// ────────────────────────────────────────────────────────────
// Helper: abbrevia etichette note per migliorare leggibilita'
// ────────────────────────────────────────────────────────────
static void abbrevia_nome(const char* src, char* out, uint8_t max_chars) {
  if (!src || !src[0]) { out[0] = '\0'; return; }

  if (strcmp(src, "Fermentazione primaria") == 0) {
    strncpy(out, "Ferm. Prim.", max_chars);
  } else if (strcmp(src, "Fermentazione secondaria") == 0) {
    strncpy(out, "Ferm. Sec.", max_chars);
  } else if (strcmp(src, "Maturazione") == 0) {
    strncpy(out, "Maturaz.", max_chars);
  } else if (strcmp(src, "Diacetyl rest") == 0) {
    strncpy(out, "Dia. Rest", max_chars);
  } else if (strcmp(src, "Cold crash") == 0) {
    strncpy(out, "ColdCr.", max_chars);
  } else {
    // Abbreviazione generica parola-per-parola (fallback)
    uint8_t w = 0;
    const char* p = src;
    while (*p && w < max_chars) {
      while (*p == ' ') p++;
      if (!*p) break;

      const char* start = p;
      uint8_t len = 0;
      while (*p && *p != ' ') { p++; len++; }

      // Mantieni parole corte, accorcia parole lunghe (>7)
      uint8_t keep = (len > 7) ? 4 : len;
      for (uint8_t i = 0; i < keep && w < max_chars; i++) out[w++] = start[i];
      if (len > 7 && w < max_chars) out[w++] = '.';
      if (*p == ' ' && w < max_chars) out[w++] = ' ';
    }
    // Rimuovi spazio finale
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
// Helper: icone relè (caldo/freddo) dentro box 11x12
// ────────────────────────────────────────────────────────────
static void draw_hot_symbol(int x, int y) {
  // Radiatore stilizzato: due barre orizzontali + tre colonne
  _oled.drawHLine(x + 1, y + 1, 7);
  _oled.drawHLine(x + 1, y + 7, 7);
  _oled.drawVLine(x + 2, y + 2, 5);
  _oled.drawVLine(x + 4, y + 2, 5);
  _oled.drawVLine(x + 6, y + 2, 5);
}

static void draw_cold_symbol(int x, int y) {
  // Fiocco di neve stilizzato: croce + diagonali
  _oled.drawVLine(x + 4, y + 1, 7);
  _oled.drawHLine(x + 1, y + 4, 7);
  _oled.drawLine(x + 2, y + 2, x + 6, y + 6);
  _oled.drawLine(x + 6, y + 2, x + 2, y + 6);
}

static void draw_off_slash(int x, int y, int w, int h) {
  _oled.drawLine(x + 1, y + h - 2, x + w - 2, y + 1);
}

// ────────────────────────────────────────────────────────────
// Helper: indicatori relè — due box in alto a destra
//  ON  = icona normale
//  OFF = icona barrata
// ────────────────────────────────────────────────────────────
static void draw_relays(bool risc, bool frigo) {
  // Riscaldatore (radiatore): x=107..117, y=0..11
  _oled.drawFrame(107, 0, 11, 12);
  draw_hot_symbol(108, 2);
  if (!risc) {
    draw_off_slash(107, 0, 11, 12);
  }

  // Refrigeratore (fiocco neve): x=118..127, y=0..11
  _oled.drawFrame(118, 0, 10, 12);
  draw_cold_symbol(118, 2);
  if (!frigo) {
    draw_off_slash(118, 0, 10, 12);
  }
}

// ────────────────────────────────────────────────────────────
// Schermata BOOT (WiFi / MQTT / Richiesta profilo)
// ────────────────────────────────────────────────────────────
static void draw_boot(const char* linea1, const char* linea2) {
  // Titolo
  _oled.setFont(u8g2_font_7x13_tf);
  const char* titolo = "FERMENTATORE V3";
  _oled.drawStr(center_x(titolo), 20, titolo);
  _oled.drawHLine(10, 23, 108);

  // Stato
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
  // Header invertito
  _oled.drawBox(0, 0, 128, 16);
  _oled.setDrawColor(0);
  _oled.setFont(u8g2_font_7x13_tf);
  const char* label = "!! ALLARME !!";
  _oled.drawStr(center_x(label), 13, label);
  _oled.setDrawColor(1);

  // Messaggio
  _oled.setFont(u8g2_font_6x10_tf);
  const char* msg = "Sensore offline";
  _oled.drawStr(center_x(msg), 32, msg);

  // Countdown riavvio
  long rimasto_ms = (long)ALLARME_REBOOT_DELAY_MS
                    - (long)(millis() - d.ts_ingresso_allarme_ms);
  int secs = (rimasto_ms > 0) ? (int)(rimasto_ms / 1000) : 0;
  char buf[24];
  snprintf(buf, sizeof(buf), "Riavvio in %ds", secs);
  _oled.drawStr(center_x(buf), 47, buf);

  // Temperatura corrente (piccola, in fondo)
  snprintf(buf, sizeof(buf), "T: %.1f%cC", d.temp_reale, (char)0xB0);
  _oled.drawStr(center_x(buf), 62, buf);
}

// ────────────────────────────────────────────────────────────
// Schermata OPERATIVA (RUNNING / STANDBY / HOLDING)
//
// Zona temperature (y=30):
//   Sinistra: temperatura reale  (font grande 9×15)
//   Destra:   setpoint           (font piccolo 6×10, allineato a destra)
// Zona step (y=43):  nome step o messaggio stato
// Zona tempi (y=56): ore step / ore totali  (solo in RUNNING/HOLDING)
// ────────────────────────────────────────────────────────────
static void draw_operational(const DisplayData& d) {
  char buf[32];
  char stato_buf[24];
  char prof_short[9];

  // ── Header ──────────────────────────────────────────────
  _oled.setFont(u8g2_font_6x10_tf);
  const bool is_running = (strcmp(d.stato_fsm, "RUNNING") == 0);
  const bool is_holding = (strcmp(d.stato_fsm, "HOLDING") == 0);
  if (is_running) {
    const char spinner[4] = {'|', '/', '-', '\\'};
    abbrevia_nome(d.profilo_nome, prof_short, 8);
    snprintf(stato_buf, sizeof(stato_buf), "R%c", spinner[(millis() / 250UL) % 4]);
    _oled.drawStr(0, 11, stato_buf);
    _oled.drawStr(14, 11, prof_short);

    if (d.giorni_prossimo_step >= 0.0f) {
      snprintf(stato_buf, sizeof(stato_buf), "-%.1fgg", d.giorni_prossimo_step);
      _oled.drawStr(66, 11, stato_buf);
    }
  } else if (is_holding) {
    abbrevia_nome(d.profilo_nome, prof_short, 8);
    snprintf(stato_buf, sizeof(stato_buf), "HOLD %s", prof_short);
    _oled.drawStr(0, 11, stato_buf);
  } else {
    _oled.drawStr(0, 11, d.stato_fsm);
  }
  draw_relays(d.riscaldatore_on, d.refrigeratore_on);
  _oled.drawHLine(0, 13, 128);

  // ── Temperatura reale (grande, sinistra) ────────────────
  _oled.setFont(u8g2_font_9x15_tf);
  snprintf(buf, sizeof(buf), "%.1f%cC", d.temp_reale, (char)0xB0);
  _oled.drawStr(0, 30, buf);

  // ── Setpoint (piccolo, allineato a destra) ──────────────
  _oled.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "SP:%.1f%cC", d.setpoint, (char)0xB0);
  int sp_x = 128 - (int)_oled.getStrWidth(buf);
  _oled.drawStr(sp_x, 30, buf);

  // ── Info step / stato ────────────────────────────────────
  if (is_running) {
    // Nome step abbreviato/troncato per 21 caratteri
    char step_buf[22];
    abbrevia_nome(d.step_nome, step_buf, 21);
    step_buf[21] = '\0';
    _oled.drawStr(0, 43, step_buf);

    // Barra avanzamento profilo + giorni mancanti al prossimo step
    draw_progress_bar(0, 48, 86, 10, d.progresso_pct);
    snprintf(buf, sizeof(buf), "%d%%", (int)(d.progresso_pct + 0.5f));
    _oled.drawStr(89, 56, buf);

  } else if (is_holding) {
    // Mostra nome profilo e "Completato"
    char prof_buf[22];
    abbrevia_nome(d.profilo_nome, prof_buf, 21);
    _oled.drawStr(0, 43, prof_buf);
    _oled.drawStr(0, 56, "Completato");

  } else {
    // STANDBY: nessun profilo attivo
    _oled.drawStr(0, 43, "Nessun profilo");
    if (d.wifi_offline) {
      _oled.drawStr(0, 56, "[!] WiFi offline");
    } else {
      snprintf(buf, sizeof(buf), "Target: %.1f%cC", d.setpoint, (char)0xB0);
      _oled.drawStr(0, 56, buf);
    }
  }
}

// ============================================================
// API pubblica
// ============================================================

void display_init() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  _oled.setI2CAddress(OLED_I2C_ADDR << 1);  // U8g2 usa indirizzo 8-bit
  _oled.begin();

  // Logo di avvio
  _oled.clearBuffer();
  _oled.setFont(u8g2_font_7x13_tf);
  const char* titolo = "FERMENTATORE V3";
  _oled.drawStr((128 - (int)_oled.getStrWidth(titolo)) / 2, 28, titolo);
  _oled.setFont(u8g2_font_6x10_tf);
  const char* sub = "Avvio...";
  _oled.drawStr((128 - (int)_oled.getStrWidth(sub)) / 2, 46, sub);
  _oled.sendBuffer();
}

void display_aggiorna(const DisplayData& d) {
  _oled.clearBuffer();

  const char* s = d.stato_fsm ? d.stato_fsm : "";

  if (strcmp(s, "ALLARME") == 0) {
    draw_allarme(d);

  } else if (strcmp(s, "RUNNING") == 0 ||
             strcmp(s, "STANDBY") == 0 ||
             strcmp(s, "HOLDING") == 0) {
    draw_operational(d);

  } else {
    // WIFI / MQTT / RICHIESTA_PROFILO
    const char* l1 = s;
    const char* l2 = "";
    if      (strcmp(s, "WIFI")              == 0) { l1 = "Connessione"; l2 = "WiFi..."; }
    else if (strcmp(s, "MQTT")              == 0) { l1 = "Connessione"; l2 = "MQTT..."; }
    else if (strcmp(s, "RICHIESTA_PROFILO") == 0) { l1 = "Richiesta";   l2 = "profilo..."; }
    draw_boot(l1, l2);
  }

  _oled.sendBuffer();
}
