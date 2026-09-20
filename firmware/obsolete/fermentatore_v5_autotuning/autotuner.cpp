#include "autotuner.h"
#include <math.h>

// ============================================================
// Implementazione relay-feedback (Åström-Hägglund)
// ============================================================

// Stato interno
static float         _setpoint           = 20.0f;
static float         _relay_amp          = 10.0f;   // % potenza applicata dal relay
static bool          _relay_on           = false;

// Rilevamento oscillazioni
// Un ciclo completo = da un cambio relay a quello successivo dello stesso tipo.
// Teniamo i timestamp dei "crossing" (il momento in cui la temperatura
// supera il setpoint in una delle due direzioni).

static const int     MAX_CROSSINGS = 20;
static unsigned long _crossing_ts[MAX_CROSSINGS];  // timestamp attraversamento setpoint [ms]
static float         _crossing_temp[MAX_CROSSINGS]; // temp in quel momento
static int           _n_crossings = 0;

// Massimi e minimi dell'ampiezza per ogni semi-periodo
static float         _peak_max = -1000.0f;  // massimo rilevato nel semi-periodo corrente
static float         _peak_min =  1000.0f;  // minimo rilevato nel semi-periodo corrente
static float         _amplitudes[MAX_CROSSINGS]; // amp dei semi-periodi
static int           _n_amplitudes = 0;

// Stato "sopra/sotto setpoint" al campione precedente
static bool          _was_above = false;
static bool          _first_sample = true;

// Risultati
static float         _kp_result = 0.0f;
static float         _ki_result = 0.0f;
static float         _kd_result = 0.0f;
static float         _Tu_result = 0.0f;
static float         _Au_result = 0.0f;

void autotuner_init(float setpoint, float relay_amplitude_pct) {
  _setpoint      = setpoint;
  _relay_amp     = relay_amplitude_pct;
  _relay_on      = false;
  _n_crossings   = 0;
  _n_amplitudes  = 0;
  _first_sample  = true;
  _was_above     = false;
  _peak_max      = -1000.0f;
  _peak_min      =  1000.0f;
  _kp_result     = 0.0f;
  _ki_result     = 0.0f;
  _kd_result     = 0.0f;
  _Tu_result     = 0.0f;
  _Au_result     = 0.0f;
}

bool autotuner_aggiorna(float temp_reale, bool& relay_on_out) {
  unsigned long ora = millis();
  bool above = (temp_reale > _setpoint);

  if (_first_sample) {
    _was_above   = above;
    _first_sample = false;
    // Avvia: se siamo sotto il setpoint, accendiamo il relay
    _relay_on    = !above;
    relay_on_out = _relay_on;
    return false;
  }

  // Aggiorna tracciamento dei picchi nel semi-periodo corrente
  if (temp_reale > _peak_max) _peak_max = temp_reale;
  if (temp_reale < _peak_min) _peak_min = temp_reale;

  // Rilevamento cambio di segno (crossing del setpoint)
  if (above != _was_above) {
    // Salva il crossing
    if (_n_crossings < MAX_CROSSINGS) {
      _crossing_ts[_n_crossings]   = ora;
      _crossing_temp[_n_crossings] = temp_reale;
    }

    // Ampiezza del semi-periodo appena concluso:
    //   se eravamo sopra, il picco rilevante è il massimo
    //   se eravamo sotto, il picco rilevante è il minimo
    float amp = _was_above ? (_peak_max - _setpoint) : (_setpoint - _peak_min);
    if (amp > 0.0f && _n_amplitudes < MAX_CROSSINGS) {
      _amplitudes[_n_amplitudes++] = amp;
    }

    // Resetta tracciamento picchi per il nuovo semi-periodo
    _peak_max = temp_reale;
    _peak_min = temp_reale;

    // Cambia stato relay: sopra setpoint → spegni; sotto setpoint → accendi
    _relay_on = !above;

    _n_crossings = min(_n_crossings + 1, MAX_CROSSINGS);

    _was_above = above;
  }

  relay_on_out = _relay_on;

  // Serve ≥3 cicli completi = ≥6 crossings (inizio + 6 cambi di segno).
  // Usiamo i crossing da 1 in poi (il crossing 0 è il punto di partenza).
  // Un ciclo completo = 2 crossing.
  // 3 cicli = 6 crossing (indici 0..5), ma il crossing 0 è il primo rilevato
  // dopo l'avvio → usiamo indici [0..5] dei crossing, quindi _n_crossings >= 7.
  if (_n_crossings < 7) return false;

  // ── Calcolo Tu (periodo medio) ──────────────────────────────────────────
  // Tu = tempo medio tra crossing dello stesso tipo, su ≥3 cicli.
  // Usiamo i crossing dello stesso verso: crossing 0, 2, 4, 6 (ogni 2)
  // per stimate i semi-periodi dello stesso tipo, poi Tu = 2 * semi-periodo medio.
  int n_periodi = 0;
  float sum_T = 0.0f;
  // Usa coppie di crossing omologhi: [0,2], [2,4], [4,6]
  for (int i = 0; i + 2 < min(_n_crossings, 7); i += 2) {
    float dt_s = (float)(_crossing_ts[i + 2] - _crossing_ts[i]) / 1000.0f;
    if (dt_s > 0.0f) { sum_T += dt_s; n_periodi++; }
  }
  if (n_periodi == 0) return false;
  float Tu = sum_T / (float)n_periodi;

  // ── Calcolo Au (ampiezza media) ─────────────────────────────────────────
  // Usa tutte le ampiezze rilevate (almeno 4: 2 sopra + 2 sotto)
  if (_n_amplitudes < 4) return false;
  float sum_Au = 0.0f;
  for (int i = 0; i < _n_amplitudes; i++) sum_Au += _amplitudes[i];
  float Au = sum_Au / (float)_n_amplitudes;
  if (Au < 0.01f) return false;  // oscilla troppo poco per misurare

  // ── Calcolo Ku e gain Ziegler-Nichols PI ───────────────────────────────
  // Il relay applica relay_amp% (0-100), output PID è in unità [0-100].
  // Ku in unità output/%·°C → adimensionale percentuale:
  float Ku = (4.0f * _relay_amp) / (float(M_PI) * Au);

  // Ziegler-Nichols PI:
  //   kp = 0.45 * Ku
  //   Ti = Tu / 1.2
  //   ki = kp / Ti = 0.45 * Ku * 1.2 / Tu
  _kp_result = 0.45f * Ku;
  _ki_result = 0.45f * Ku * 1.2f / Tu;
  _kd_result = 0.0f;
  _Tu_result = Tu;
  _Au_result = Au;

  return true;
}

void autotuner_get_gains(float& kp_out, float& ki_out, float& kd_out) {
  kp_out = _kp_result;
  ki_out = _ki_result;
  kd_out = _kd_result;
}

float autotuner_get_Tu() { return _Tu_result; }
float autotuner_get_Au() { return _Au_result; }
