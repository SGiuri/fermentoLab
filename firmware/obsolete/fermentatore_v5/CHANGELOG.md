# Changelog – Fermentatore V5

Tutte le modifiche rilevanti sono documentate qui in ordine cronologico inverso.
Formato: [Semantic Versioning](https://semver.org/)

---

## [v5.3.0] - 2026-05-19
### Added
- **Port fix sensore da v4.3.7**: `SENSOR_VALID_STREAK_MIN 5` in `config.h`;
  `g_sensore_valid_streak` in `fermentatore_v5.ino`; richiede 5 letture consecutive
  valide (25s) prima di considerare il sensore stabile. Evita chattering
  quando DS18B20 restituisce valori in-range ma fisicamente errati.
- **Power cycle GPIO-controllato del DS18B20** (`PIN_SENSOR_PWR = GPIO25`).
  Il VDD del sensore ora è alimentato tramite GPIO25 anziché diretto su 3V3,
  permettendo al firmware di spegnere/riaccendere il sensore via software.
  Beneficio implicito: ogni `ESP.restart()` power-cicla il sensore (GPIO25 è
  in input/HiZ durante il boot ESP32).
- `sensor_power_cycle_init()` e `sensor_power_cycle(SensorState&)` in
  `sensor_watchdog.cpp` (identici a v4). Stesse costanti:
  `SENSOR_POWER_CYCLE_TIMEOUT_MS 120000UL`, `SENSOR_POWER_CYCLE_MAX_ATTEMPTS 3`.
- Power cycle proattivo in `loop()` e tentativo in `ALLARME_SENSORE` (identici a v4).
- Log STATUS mostra `T=---- grC PID=----` quando il sensore è instabile o in debounce
  (in precedenza mostrava la lettura errata, es. 7.88 °C).

## [v5.2.2] - 2026-05-18
### Changed
- Topic MQTT v5 ristrutturati: introdotti i prefissi `esp/` (telemetria ESP32→HA)
  e `ha/` (comandi HA→ESP32) sotto `fermentazione/v5/f1/`.
  - ESP→HA: `fermentazione/v5/f1/esp/{stato,ack,allarme,pong,richiedi_profilo,log}`
  - HA→ESP: `fermentazione/v5/f1/ha/{profilo,stop,temp_standby,ping,reboot,test_no_wifi}`
  - Sostituisce il vecchio schema piatto (`fermentazione/v5/f1/stato`, `cmd/…`).
  - Wildcard suggerite: `fermentazione/v5/f1/esp/#` e `fermentazione/v5/f1/ha/#`.
- `FIRMWARE_VERSION` aggiornato a `"5.2.2"` in `config.h`.
- `HASS_SPEC_v5_v5A.txt` aggiornato con i nuovi topic (tutti i topic_topic, command_topic
  e riferimenti nelle automazioni).

## [v5.2.1] - 2026-05-17
### Changed
- Topic MQTT v5 rinominati da `fermentazione/f1/...` a `fermentazione/v5/f1/...`
  per evitare sovrapposizioni con fermentatore_v4 in coesistenza sullo stesso broker.
- `FIRMWARE_VERSION` aggiornato a `"5.2.1"` in `config.h`.

## [v5.2.0] - 2026-05-12
### Added
- `g_wifi_ssid[33]`: variabile globale che memorizza l'SSID della rete al momento della connessione
- `DisplayData.wifi_ssid[33]`: campo aggiunto alla struttura per mostrare l'SSID sul display OLED
- Schermata di boot MQTT ora mostra l'SSID a cui si è connessi (invece di "...")
- Log MQTT al broker ora include l'SSID: `"Connesso al broker. Rete: %s"`
- `HOTSPOT_RECONNECT_INTERVAL_MS 5000UL` aggiunto in `config.h`
- `FIRMWARE_VERSION "5.2.0"` aggiunto in `config.h`

### Changed
- `mantieni_connessione()`: logica hotspot completamente riscritta
  - Fase 1 (burst): 3 tentativi rapidi a t=0s, t+5s, t+10s
  - Fase 2 (fallback): offline NVS senza `WiFi.disconnect()`, attesa 20 min
  - Fase 3: ciclo standard (3 × 30s, pausa 20 min, ripete indefinitamente)
  - Sostituisce `g_hotspot_offline` (offline definitivo) con `g_hotspot_burst_done`
- `g_hotspot_offline` rimosso e sostituito da `g_hotspot_burst_done`

## [v5.1.0] - 2026-05-12
### Changed
- Display OLED: icona relay singola 12×12px in posizione x=116 (fiamma se riscalda, cristallo se raffredda, vuota se spento) — sostituisce le precedenti due icone separate
- Display OLED: nome profilo in header ora scorre da destra a sinistra (ticker ~22 px/s, gap 20px) se non entra nella zona disponibile; statico se ci sta
- Display OLED: giorni al prossimo step spostati dalla riga header al body (riga y=43, allineato a destra, formato `"-X.Xd"`)
- Schermata di boot OLED: riga 2 mostra l'SSID connesso non appena disponibile

## [v5.0.0] - 2026-05-12
### Added
- Versione iniziale fermentatore_v5: controllo PID bidirezionale su PWM (sostituisce bang-bang + isteresi di V4)
- Gain PID asimmetrici: parametri separati per riscaldamento e raffreddamento
- Ciclo PWM configurabile (default 60s); impulso minimo pompa 5s
- Eliminato il modulo duty_cycle (il PID funziona autonomamente senza rete)
- Supporto OTA via hotspot personale (identico a V4)
- Connettività WiFi/MQTT con Home Assistant (identica a V4)
- Watchdog sensore, NVS persistence, FSM identica a V4
