#include "uscite_rele.h"
#include "config.h"

struct StatoRele {
  uint8_t       pin;
  bool          acceso;
  bool          mai_acceso;     // dal boot: l'attesa cambio direzione non si applica
  unsigned long ts_acceso_ms;
  unsigned long ts_spento_ms;
};

static StatoRele g_heat = {PIN_RISCALDATORE, false, true, 0, 0};
static StatoRele g_cool = {PIN_REFRIGERATORE, false, true, 0, 0};
static uint32_t  g_commutazioni = 0;

static void scrivi_rele(StatoRele& r, bool acceso, unsigned long now_ms) {
  if (r.acceso == acceso) return;
  r.acceso = acceso;
  if (acceso) {
    r.ts_acceso_ms = now_ms;
    r.mai_acceso = false;
  } else {
    r.ts_spento_ms = now_ms;
  }
  // Relays are ACTIVE LOW.
  digitalWrite(r.pin, acceso ? RELAY_ON : RELAY_OFF);
  g_commutazioni++;
}

// Duty → stato desiderato nella fase corrente della finestra.
static bool duty_desiderato(float duty_pct, unsigned long now_ms) {
  if (!(duty_pct > 0.0f)) return false;  // gestisce anche NaN
  const float on_ms_f = ((float)PID_DUTY_WINDOW_MS * duty_pct) / 100.0f;
  const unsigned long on_ms = (on_ms_f >= (float)PID_DUTY_WINDOW_MS)
                              ? PID_DUTY_WINDOW_MS
                              : (unsigned long)on_ms_f;
  if (on_ms < RELE_IMPULSO_MIN_MS) return false;
  if (on_ms > PID_DUTY_WINDOW_MS - RELE_MIN_OFF_MS) return true;
  return (now_ms % PID_DUTY_WINDOW_MS) < on_ms;
}

static void aggiorna_rele(StatoRele& r,
                          const StatoRele& opposto,
                          bool desiderato,
                          bool spegni_subito,
                          unsigned long now_ms) {
  if (r.acceso) {
    if (!desiderato && (spegni_subito || now_ms - r.ts_acceso_ms >= RELE_MIN_ON_MS)) {
      scrivi_rele(r, false, now_ms);
    }
    return;
  }

  if (!desiderato) return;
  if (opposto.acceso) return;                                          // interlock
  if (now_ms - r.ts_spento_ms < RELE_MIN_OFF_MS) return;               // anti cicli brevi
  if (!opposto.mai_acceso && now_ms - opposto.ts_spento_ms < RELE_CAMBIO_DIREZIONE_MS) return;  // caldo/freddo non in lotta
  scrivi_rele(r, true, now_ms);
}

void uscite_init() {
  pinMode(PIN_RISCALDATORE, OUTPUT);
  pinMode(PIN_REFRIGERATORE, OUTPUT);
  digitalWrite(PIN_RISCALDATORE, RELAY_OFF);
  digitalWrite(PIN_REFRIGERATORE, RELAY_OFF);

  // Timer "spento" al boot: nessuna accensione prima dei tempi minimi,
  // protegge da cicli rapidi se la scheda si riavvia ripetutamente.
  const unsigned long now_ms = millis();
  g_heat.acceso = false;
  g_heat.mai_acceso = true;
  g_heat.ts_spento_ms = now_ms;
  g_cool.acceso = false;
  g_cool.mai_acceso = true;
  g_cool.ts_spento_ms = now_ms;
}

void uscite_aggiorna(const RichiestaUscite& richiesta,
                     bool forza_spegnimento,
                     float temp_c,
                     float setpoint_c,
                     unsigned long now_ms) {
  // NaN nel duty: il controller segnala anche heat_on/cool_on, il ramo booleano
  // accenderebbe il relè al 100%. Qualunque valore non numerico spegne tutto.
  if (forza_spegnimento || isnan(temp_c) || isnan(setpoint_c) ||
      isnan(richiesta.heat_duty_pct) || isnan(richiesta.cool_duty_pct)) {
    scrivi_rele(g_heat, false, now_ms);
    scrivi_rele(g_cool, false, now_ms);
    return;
  }

  bool want_heat = richiesta.heat_on;
  bool want_cool = richiesta.cool_on;

  // Famiglia PID: duty percentuale; famiglia on/off: booleani.
  if (richiesta.heat_duty_pct > 0.0f || richiesta.cool_duty_pct > 0.0f) {
    want_heat = duty_desiderato(richiesta.heat_duty_pct, now_ms);
    want_cool = duty_desiderato(richiesta.cool_duty_pct, now_ms);
  }

  // Divieti di sicurezza: nessun algoritmo deve scaldare sopra il setpoint
  // o raffreddare sotto, oltre la banda. Proteggono da bug e setpoint errati.
  const bool vieta_heat = (temp_c >= SICUREZZA_TEMP_MAX_C) ||
                          (temp_c > setpoint_c + SICUREZZA_BANDA_C);
  const bool vieta_cool = (temp_c <= SICUREZZA_TEMP_MIN_C) ||
                          (temp_c < setpoint_c - SICUREZZA_BANDA_C);

  // Safety interlock: never drive heat and cool together.
  const bool conflitto = want_heat && want_cool;

  aggiorna_rele(g_heat, g_cool, want_heat && !vieta_heat && !conflitto,
                vieta_heat || conflitto, now_ms);
  aggiorna_rele(g_cool, g_heat, want_cool && !vieta_cool && !conflitto,
                vieta_cool || conflitto, now_ms);
}

bool uscite_riscaldatore_acceso() {
  return g_heat.acceso;
}

bool uscite_refrigeratore_acceso() {
  return g_cool.acceso;
}

uint32_t uscite_commutazioni() {
  return g_commutazioni;
}
