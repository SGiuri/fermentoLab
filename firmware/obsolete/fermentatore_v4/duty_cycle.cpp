#include "duty_cycle.h"

#include "config.h"

namespace {

struct DutyBucket {
  uint32_t indice_minuto;
  uint32_t osservato_ms;
  uint32_t risc_on_ms;
  uint32_t frigo_on_ms;
};

static DutyBucket g_buckets[DUTY_CYCLE_WINDOW_MS / DUTY_CYCLE_BUCKET_MS];
static unsigned long g_last_track_ms = 0;
static bool g_tracking_attivo = false;

static void reset_bucket(DutyBucket& bucket, uint32_t indice_minuto) {
  bucket.indice_minuto = indice_minuto;
  bucket.osservato_ms  = 0;
  bucket.risc_on_ms    = 0;
  bucket.frigo_on_ms   = 0;
}

static bool indice_valido(uint32_t indice_corrente, uint32_t indice_bucket) {
  return indice_bucket <= indice_corrente && (indice_corrente - indice_bucket) < (DUTY_CYCLE_WINDOW_MS / DUTY_CYCLE_BUCKET_MS);
}

static float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

}  // namespace

void duty_cycle_init() {
  for (size_t i = 0; i < (DUTY_CYCLE_WINDOW_MS / DUTY_CYCLE_BUCKET_MS); i++) {
    reset_bucket(g_buckets[i], 0);
  }
  g_last_track_ms = 0;
  g_tracking_attivo = false;
}

void duty_cycle_track(unsigned long now_ms, bool risc_on, bool frigo_on) {
  if (!g_tracking_attivo) {
    g_last_track_ms = now_ms;
    g_tracking_attivo = true;
    return;
  }

  if (now_ms < g_last_track_ms) {
    duty_cycle_init();
    g_last_track_ms = now_ms;
    g_tracking_attivo = true;
    return;
  }

  unsigned long start_ms = g_last_track_ms;
  while (start_ms < now_ms) {
    uint32_t indice_minuto = start_ms / DUTY_CYCLE_BUCKET_MS;
    unsigned long prossimo_confine = (indice_minuto + 1UL) * DUTY_CYCLE_BUCKET_MS;
    unsigned long end_ms = (prossimo_confine < now_ms) ? prossimo_confine : now_ms;
    uint32_t delta_ms = (uint32_t)(end_ms - start_ms);

    DutyBucket& bucket = g_buckets[indice_minuto % (DUTY_CYCLE_WINDOW_MS / DUTY_CYCLE_BUCKET_MS)];
    if (bucket.indice_minuto != indice_minuto) {
      reset_bucket(bucket, indice_minuto);
    }

    bucket.osservato_ms += delta_ms;
    if (risc_on) bucket.risc_on_ms += delta_ms;
    if (frigo_on) bucket.frigo_on_ms += delta_ms;

    start_ms = end_ms;
  }

  g_last_track_ms = now_ms;
}

DutyCycleStats duty_cycle_stats(unsigned long now_ms) {
  DutyCycleStats stats = {};
  uint32_t indice_corrente = now_ms / DUTY_CYCLE_BUCKET_MS;

  for (size_t i = 0; i < (DUTY_CYCLE_WINDOW_MS / DUTY_CYCLE_BUCKET_MS); i++) {
    const DutyBucket& bucket = g_buckets[i];
    if (!indice_valido(indice_corrente, bucket.indice_minuto)) continue;

    stats.osservazione_ms += bucket.osservato_ms;
    stats.duty_riscaldamento += (float)bucket.risc_on_ms;
    stats.duty_refrigerazione += (float)bucket.frigo_on_ms;
  }

  if (stats.osservazione_ms > 0) {
    stats.duty_riscaldamento = clamp01(stats.duty_riscaldamento / (float)stats.osservazione_ms);
    stats.duty_refrigerazione = clamp01(stats.duty_refrigerazione / (float)stats.osservazione_ms);
  }

  stats.sufficiente = stats.osservazione_ms >= DUTY_CYCLE_WINDOW_MS;
  stats.stimato     = stats.osservazione_ms >= DUTY_CYCLE_ESTIMATE_MIN_MS;
  return stats;
}

bool duty_cycle_applica(StatoTermostato& s, unsigned long now_ms,
                        bool prefer_risc, bool prefer_frigo,
                        uint32_t min_cycle_sec,
                        DutyCycleStats* stats_out) {
  DutyCycleStats stats = duty_cycle_stats(now_ms);
  if (stats_out) *stats_out = stats;

  if (!stats.stimato) {
    return false;
  }

  float duty_risc = stats.duty_riscaldamento;
  float duty_frigo = stats.duty_refrigerazione;

  bool usa_risc = prefer_risc;
  bool usa_frigo = prefer_frigo;
  if (!usa_risc && !usa_frigo) {
    usa_risc = duty_risc > duty_frigo;
    usa_frigo = duty_frigo > duty_risc;
  }

  unsigned long fase_ms = now_ms % DUTY_CYCLE_APPLY_WINDOW_MS;
  bool risc_target = usa_risc && duty_risc > 0.0f && fase_ms < (unsigned long)(duty_risc * DUTY_CYCLE_APPLY_WINDOW_MS);
  bool frigo_target = usa_frigo && duty_frigo > 0.0f && fase_ms < (unsigned long)(duty_frigo * DUTY_CYCLE_APPLY_WINDOW_MS);

  if (risc_target && frigo_target) {
    if (duty_risc >= duty_frigo) frigo_target = false;
    else risc_target = false;
  }

  return termostato_imposta(s, risc_target, frigo_target, min_cycle_sec);
}
