#pragma once
#include <Arduino.h>
#include "uscite_rele.h"

// ============================================================
// Regolazione stimata (firmware >= 6.5.0)
//
// Quando il DS18B20 non dà letture valide, invece di lasciare i relè spenti per ore il firmware li
// pilota "alla cieca" con il carico termico misurato nelle ultime ore di controllo normale,
// ipotizzando condizioni al contorno analoghe (stanza, fermentazione).
//
// Apprendimento, mentre il controllo è in RUNNING con temperatura utilizzabile: blocchi di
// STIMA_BLOCCO_S secondi con la frazione di tempo dei relè accesi e la temperatura media. Con almeno
// STIMA_BLOCCHI_MIN blocchi contigui (fino a STIMA_BLOCCHI_MAX) si ricava il bilancio
//
//   dT/dt = carico(T) - u_freddo*R_freddo + u_caldo*R_caldo
//   carico(T) = carico(T_media) + (T_media - T)/tau           (la stanza scambia calore con costante tau)
//
// dove dT/dt è la pendenza della temperatura sulla finestra (regressione sulle medie dei blocchi).
// Alla temperatura del setpoint serve una potenza netta di raffreddamento
//
//   p = u_freddo*R_freddo - u_caldo*R_caldo + pendenza + (T_media - setpoint)/tau     [°C/h]
//
// che diventa un duty del refrigeratore (p > 0) o del riscaldatore (p < 0), applicato su una finestra
// di STIMA_FINESTRA_MS. Il duty di base è quello misurato: R_freddo e R_caldo pesano solo sulle
// correzioni (pendenza e distanza dal setpoint), quindi un loro errore conta poco. Il setpoint segue il
// profilo: abbassarlo di 1 °C aumenta il raffreddamento quanto serve a tenere 1 °C in meno (leva
// manuale da remoto se il monitoraggio iSpindel mostra uno scostamento).
//
// La stima sopravvive ai riavvii software (memoria RTC: i riavvii automatici per allarme sensore) e,
// salvata in NVS ogni STIMA_NVS_INTERVALLO_MS, anche a un blackout. La regolazione stimata dura al
// massimo STIMA_DURATA_MAX_H ore cumulative (anche attraverso i riavvii), poi i relè restano spenti.
// ============================================================

struct StatoStima {
  bool  pronta;           // stima del carico disponibile
  bool  attiva;           // relè guidati dalla stima (sensore perso)
  bool  scaduta;          // superata la durata massima: relè spenti
  float duty_freddo_pct;  // duty che la stima applica (o applicherebbe) al setpoint corrente
  float duty_caldo_pct;
  float ore_attiva;       // ore cumulative di regolazione stimata dall'ultima lettura valida
  float u_freddo_pct;     // apprendimento: duty medio misurato del refrigeratore
  float u_caldo_pct;      //                 duty medio misurato del riscaldatore
  float t_media;          //                 temperatura media della finestra [°C]
  float pendenza_c_h;     //                 pendenza della temperatura sulla finestra [°C/h]
  uint8_t blocchi;        // blocchi contigui validi nella finestra di apprendimento
};

// Da chiamare in setup(): ripristina la stima dalla memoria RTC (riavvio software, watchdog, panic)
// oppure dalla NVS (accensione, brownout).
void stima_init();

// Da chiamare una volta per tick di controllo (CONTROL_INTERVAL_MS).
//   in_run       controllo in RUNNING
//   misura_ok    temperatura utilizzabile dal controllo normale
//   sensore_perso nessuna lettura valida da più di SENSOR_MAX_ETA_MS
// Ritorna true se le uscite vanno pilotate con *richiesta (regolazione stimata attiva).
bool stima_aggiorna(unsigned long now_ms,
                    bool in_run,
                    bool misura_ok,
                    bool sensore_perso,
                    float temp_c,
                    float setpoint_c,
                    bool rele_caldo,
                    bool rele_freddo,
                    RichiestaUscite* richiesta);

const StatoStima& stima_stato();

// Duty [%] che la stima applicherebbe a un setpoint (funzione pura, esposta per i test).
void stima_calcola_duty(float u_freddo, float u_caldo, float t_media, float pendenza_c_h,
                        float setpoint_c, float* duty_freddo_pct, float* duty_caldo_pct);
