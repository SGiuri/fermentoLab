# Changelog – Fermentatore V4

Tutte le modifiche rilevanti sono documentate qui in ordine cronologico inverso.
Formato: [Semantic Versioning](https://semver.org/)

---

## [v4.4.0] - 2026-05-19
### Added
- **Power cycle GPIO-controllato del DS18B20** (`PIN_SENSOR_PWR = GPIO25`).
  Il VDD del sensore ora è alimentato tramite GPIO25 anziché diretto su 3V3,
  permettendo al firmware di spegnere/riaccendere il sensore via software.
  Beneficio implicito: ogni `ESP.restart()` power-cicla il sensore (GPIO25 è
  in input/HiZ durante il boot ESP32).
- `sensor_power_cycle_init()` in `sensor_watchdog.cpp`: configura GPIO25 come output
  e porta il sensore in alimentazione. Chiamata in `setup()` prima di `sensor_init()`.
- `sensor_power_cycle(SensorState&)` in `sensor_watchdog.cpp`: spegne il VDD per 500ms,
  riaccende, attende 750ms (prima conversione), verifica lettura. Bloccante ~1.45s.
  Ritorna `true` se il sensore risponde con un valore plausibile.
- `SENSOR_POWER_CYCLE_TIMEOUT_MS 120000UL`: se il sensore rimane instabile per 2 minuti
  (non raggiunge `SENSOR_VALID_STREAK_MIN`), viene eseguito un power cycle proattivo.
- `SENSOR_POWER_CYCLE_MAX_ATTEMPTS 3`: numero massimo di tentativi per episodio di guasto.
  Se tutti falliscono, il firmware entra normalmente in `ALLARME_SENSORE` e poi riavvia.
- In `ALLARME_SENSORE`: al primo tick viene tentato un power cycle; se riesce, il firmware
  torna allo stato operativo precedente senza attendere il reboot di 30s.

## [v4.3.7] - 2026-05-19

### Fixed

- **Rimbalzo fallback sensore**: quando il DS18B20 restituisce valori numericamente
  plausibili ma fisicamente errati (es. 8 °C invece di 21 °C), questi superano i controlli
  di `temp_valida()` e non triggherano ancora `oscillazione_incoerente()` (serve almeno
  una precedente lettura spiked). Il risultato era un loop ogni ~5 s tra "Fallback attivato"
  e "Fallback disattivato" che rendeva il duty-cycle instabile.
- Aggiunto `SENSOR_VALID_STREAK_MIN 5` in `config.h`: il fallback rimane attivo finché
  il sensore non fornisce almeno N letture consecutive valide (5 × 5 s = 25 s di stabilità).
- `g_sensore_valid_streak` (uint8_t) in `fermentatore_v4.ino`: si azzera a ogni lettura
  invalida, si incrementa a ogni lettura valida. `fallback_duty_attivo()` torna `false`
  solo quando streak ≥ `SENSOR_VALID_STREAK_MIN`.
- **LOG STATUS con sensore KO**: il messaggio STATUS ora mostra `T=---- grC Tc=---- grC A=1`
  quando il sensore è in fault o in fase di debounce, invece di pubblicare la temperatura
  sbagliata letta dalla sonda (es. 7.88 °C quando la fermentazione è a 21 °C).

## [v4.3.6] - 2026-05-14

### Fixed

- Riduzione dell'undershoot di temperatura lato frigo (escursione tipica ~1°C sotto SP)
- Aggiunto parametro `anticip_frigo` in `termostato_aggiorna()`: il frigo ora si spegne a
  `setpoint + anticip_frigo` (default 0.30 °C) invece di `setpoint`, lasciando all'inerzia
  termica dell'evaporatore il compito di completare la discesa verso SP
- Aggiunto `DEFAULT_CUTOFF_ANTICIP_FRIGO 0.3f` in `config.h`
- Aggiornata firma di `applica_controllo_con_fallback()` e tutti i suoi call-site in
  `fermentatore_v4.ino` per propagare il nuovo parametro (RUNNING, STANDBY, HOLDING)
- Il riscaldatore mantiene lo spegnimento al raggiungimento esatto del setpoint (inertia
  osservata < 0.35 °C, non richiede anticipazione)

## [v4.3.5] - 2026-05-14

### Fixed

- Rilevamento caduta connessione WiFi ora event-driven anziché basato su polling di `WiFi.status()`
- `WiFi.status()` può restare `WL_CONNECTED` per decine di secondi dopo che l'AP è sparito; il driver 802.11 invece aggiorna il proprio stato in ~500ms (beacon loss)
- Aggiunto `volatile bool g_wifi_disconnesso_evento`: settato dalla callback `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` (task WiFi), azzerato da `ARDUINO_EVENT_WIFI_GOT_IP`
- `mantieni_connessione()`: usa `WiFi.status() == WL_CONNECTED && !g_wifi_disconnesso_evento` come guardia, garantendo rilevamento rapido della disconnessione
- `dd.wifi_offline`: ora `(WiFi.status() != WL_CONNECTED) || g_wifi_disconnesso_evento` — l'icona WiFi a display lampeggia entro ~1s dalla caduta dell'AP

## [v4.3.4] - 2026-05-14

### Added
- Display RUNNING: icona WiFi a ventaglio (8×8 px, 3 archi + punto) in posizione x=119, y=49 — sempre visibile, lampeggia a 500 ms se WiFi assente o MQTT disconnesso
- `DisplayData.mqtt_offline`: nuovo campo bool per distinguere caduta MQTT da caduta WiFi
- Barra avanzamento profilo ristretta da 86 px a 80 px per far posto all'icona; percentuale spostata a x=82

## [v4.3.3] - 2026-05-14

### Fixed

- `format_giorni_intelligente`: soglia corretta da `< -1.0f` a `< 1.0f` — i valori di `giorni_prossimo_step` sono sempre positivi (countdown), quindi il branch ore:minuti era irraggiungibile e mostrava sempre `-X.Xd`
- Calcoli interni corretti per valori positivi (rimossi i segni negati nelle conversioni ore/minuti)

## [v4.3.2] - 2026-05-14

### Changed / Fixed

- Display: formattazione intelligente del tempo rimanente al prossimo step
- Formato visualizzazione del tempo rimanente:
  - >= 1.0d: formato giorni ("-2.3d")
  - < 1.0d e >= 1 minuto: formato ore:minuti ("-7h12", "-0h56")
  - < 1 minuto: formato minuti'secondi ("-56'23", "-5'12")

## [v4.3.1] - 2026-05-13

### Changed / Fixed

- Termostato v4: aggiunto lockout incrociato configurabile tra riscaldamento e raffreddamento per evitare commutazioni troppo ravvicinate dopo lo spegnimento dell'attuatore opposto
- Controllo termico: introdotta una temperatura filtrata dedicata al termostato, lasciando invariata la temperatura reale pubblicata su MQTT e mostrata a display
- `termostato_spegni_tutto()`: ora aggiorna anche il timestamp di spegnimento del riscaldatore, così il lockout resta coerente dopo stop e fail-safe

## [v4.3.0] - 2026-05-12
### Added
- `g_wifi_ssid[33]`: variabile globale che memorizza l'SSID della rete al momento della connessione
- `DisplayData.wifi_ssid[33]`: campo aggiunto alla struttura per mostrare l'SSID sul display OLED
- Schermata di boot MQTT ora mostra l'SSID a cui si è connessi (invece di "...")
- Log MQTT al broker ora include l'SSID: `"Connesso al broker. Rete: %s"`
- `HOTSPOT_RECONNECT_INTERVAL_MS 5000UL` aggiunto in `config.h`

### Changed
- `mantieni_connessione()`: logica hotspot completamente riscritta
  - Fase 1 (burst): 3 tentativi rapidi a t=0s, t+5s, t+10s
  - Fase 2 (fallback): offline NVS senza `WiFi.disconnect()`, attesa 20 min
  - Fase 3: ciclo standard (3 × 30s, pausa 20 min, ripete indefinitamente)
  - Sostituisce `g_hotspot_offline` (offline definitivo) con `g_hotspot_burst_done`
- `g_hotspot_offline` rimosso e sostituito da `g_hotspot_burst_done`

## [v4.2.0] - 2026-05-12
### Changed
- Display OLED: icona relay singola 12×12px in posizione x=116 (fiamma se riscalda, cristallo se raffredda, vuota se spento) — sostituisce le precedenti due icone separate `[R☐][F☐]`
- Display OLED: nome profilo in header ora scorre da destra a sinistra (ticker ~22 px/s, gap 20px) se non entra nella zona disponibile; statico se ci sta
- Display OLED: giorni al prossimo step spostati dalla riga header al body (riga y=43, allineato a destra, formato `"-X.Xd"`)
- Schermata di boot OLED: riga 2 mostra l'SSID connesso non appena disponibile

### Added
- `DISPLAY_DEMO.txt`: mockup ASCII di tutte le schermate OLED (boot, running, holding, standby, allarme)

## [v4.1.1] - 2026-05-12
### Fixed
- Rimossa protezione **min ON time** dal termostato: causava il frigo forzatamente acceso
  anche con T dentro la banda (bug critico introdotto in v4.1.0)
- Causa radice: se HA inviava `"min_cycle_minutes": 5` (formato legacy), il backward-compat
  lo convertiva in 300 s → il frigo restava ON per 5 minuti ignorando la temperatura
- Solo la protezione **min OFF time** è mantenuta (impedisce riaccensione rapida della pompa)
- Rimossi campi `ultimo_on_risc_ms` e `ultimo_on_frigo_ms` da `StatoTermostato`

## [v4.1.0] - 2026-05-12
### Changed
- Protezione tempi minimi relè: unità cambiata da **minuti a secondi** (`min_cycle_sec`),
  default 5 s (adatto a pompa acquario + chiller, non a compressore)
- Aggiunta protezione **min ON time** (5 s) per entrambi i relay (riscaldatore e frigo):
  impedisce spegnimento anticipato prima che il tempo minimo sia trascorso
- Aggiunta protezione **min OFF time** anche per il riscaldatore (simmetrica al frigo)
- `StatoTermostato` esteso con `ultimo_on_risc_ms`, `ultimo_off_risc_ms`, `ultimo_on_frigo_ms`
- Isteresi default aggiornata da `0.3 °C` a `0.5 °C`; se il payload MQTT del profilo
  contiene il campo `"isteresi"`, quel valore viene usato al posto del default
- Backward compat JSON: accettato sia `"min_cycle_sec"` che `"min_cycle_minutes"` (×60)

### Fixed
- `FIRMWARE_VERSION` allineata a `4.1.0` in `config.h`

## [v4.0.1] - 2026-05-12
### Added
- `FIRMWARE_VERSION` definita in `config.h` (pattern `4.x.x`)
- Versione firmware mostrata nel display OLED allo splash screen di avvio
- Campo `"firmware"` aggiunto al payload JSON dell'heartbeat MQTT (`mqtt_pubblica_stato`)
- Istruzione AI locale (`versioning.instructions.md`) per aggiornamento automatico della versione ad ogni modifica sostanziale

## [v4.0.0] - 2026-05-11

### Added

- Versione iniziale fermentatore_v4: gestione profilo fermentazione con step costanti e rampe
- Strutture `ProfiloStep` e `ProfiloFermentazione` con supporto NVS
- Calcolo setpoint (`calcolaSetpoint`) e step corrente (`stepCorrente`)
- Parsing profilo da JSON MQTT (`parsaProfiloJSON`)
- Persistenza NVS: `salvaProfilo`, `caricaProfilo`, `cancellaProfilo`, `salvaEpochNoto`
- Watchdog sensore con timeout configurabile
- Supporto duty cycle e display
- Connettività Wi-Fi e MQTT con Home Assistant
