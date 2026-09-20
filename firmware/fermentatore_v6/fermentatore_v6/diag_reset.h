#pragma once
#include <Arduino.h>

// ============================================================
// Diagnostica dei reset (firmware >= 6.4.2)
//
// Serve a capire PERCHÉ l'ESP32 si riavvia, senza cavo USB collegato:
//  - codice hardware del reset, lo stesso che la ROM stampa sulla seriale ("rst:0x10 (RTCWDT_RTC_RESET)");
//    distingue i watchdog hardware (RTC/timer group) dal task watchdog del firmware;
//  - quanto tempo prima del reset girava ancora il loop (un blocco software sarebbe di decine di secondi,
//    un disturbo elettrico di frazioni di secondo);
//  - quanto tempo prima del reset è scattato l'ultimo relè, e quale (correlazione con la pompa);
//  - in quale sezione del loop si trovava il firmware;
//  - il massimo intervallo tra due giri del loop, e quale sezione lo ha causato (anche senza reset).
//
// I dati del boot precedente stanno in memoria RTC (RTC_NOINIT: sopravvive a watchdog, brownout e riavvii
// software, non a una vera perdita di alimentazione) e si confrontano con il contatore RTC, che continua a
// contare attraverso questi reset. Pubblicati su esp/diag.
// ============================================================

enum DiagSezione : uint8_t {
  SEZ_NESSUNA = 0,   // tra la fine di un giro e l'inizio del successivo
  SEZ_BOOT,          // setup()
  SEZ_SERIALE,
  SEZ_RETE,          // WiFi, OTA, MQTT (connessione e messaggi in arrivo)
  SEZ_SENSORE,       // lettura DS18B20, validazione, power-cycle
  SEZ_DISPLAY,
  SEZ_CONTROLLO,     // profilo, setpoint, calcolo del controllore
  SEZ_USCITE,        // stadio relè
  SEZ_HEARTBEAT,     // pubblicazione MQTT
  SEZ_NVS,           // persistenza (scrittura in flash)
  SEZ_IGNOTA,        // dato RTC non integro
  SEZ_N
};

// Fotografia del reset precedente, letta una volta al boot.
struct DiagReset {
  bool        valida;         // memoria RTC integra: false dopo una vera perdita di alimentazione
  uint8_t     hw_codice;      // esp_rom_get_reset_reason(0): 1 poweron, 3 sw, 7-9 wdt, 15 brownout, 16 rtc...
  const char* hw_nome;        // nome della ROM, es. "RTCWDT_RTC_RESET"
  bool        rtc_azzerato;   // il contatore RTC è ripartito da zero (reset del dominio RTC): i tempi non valgono
  int32_t     giro_ms;        // ms tra l'ultimo giro del loop e l'avvio di questo boot; -1 = non calcolabile
  int32_t     comm_ms;        // ms tra l'ultima commutazione di un relè e l'avvio di questo boot; -1 = nessuna
  char        comm_rele;      // 'C' caldo, 'F' freddo, 0 = nessuna
  bool        comm_acceso;    // la commutazione era un'accensione
  uint8_t     sezione;        // DiagSezione in cui si trovava il firmware
  uint32_t    loop_max_ms;    // massimo intervallo tra due giri del loop nel boot precedente
  uint8_t     loop_max_sez;   // sezione più lenta in quel giro
};

// Contatori del boot corrente.
struct DiagLoop {
  uint32_t max_ms;            // massimo intervallo tra due giri del loop dal boot
  uint8_t  max_sez;           // sezione più lenta in quel giro
  uint32_t lenti_1s;          // giri più lunghi di 1 s
  uint32_t lenti_10s;         // giri più lunghi di 10 s
  uint32_t setup_ms;          // durata di setup() (WiFi e MQTT del boot inclusi)
};

// Primo comando di setup(): legge i dati del boot precedente PRIMA che qualunque altra cosa (uscite_init)
// li sovrascriva, poi inizia una nuova registrazione.
void diag_reset_init();
// Ultima riga di setup().
void diag_reset_boot_completato();
// Inizio di una sezione del loop (chiude quella precedente e ne misura la durata).
void diag_reset_sezione(DiagSezione s);
// Fine di un giro del loop: misura l'intervallo dal giro precedente e aggiorna il segnale di vita in RTC.
void diag_reset_giro();
// Da uscite_rele: commutazione di un relè ('C' caldo, 'F' freddo).
void diag_reset_commutazione(char rele, bool acceso);

const DiagReset& diag_reset_precedente();
const DiagLoop&  diag_reset_loop();
const char* diag_reset_nome_sezione(uint8_t s);
const char* diag_reset_nome_hw(uint32_t codice);
const char* diag_reset_nome_rele(char rele);  // "caldo" | "freddo" | ""
