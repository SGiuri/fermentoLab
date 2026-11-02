# 04. Contratto MQTT/Home Assistant — Fermentatore V6

**Versione documento**: 1.2 (2026-09-19, direttive originali 1.1 del 2026-09-17) · **Stato**: corrente
— è il riferimento esaustivo di ogni topic e payload. Spostato da `hass_spec/HASS_V6_SPEC.md`
(nessun percorso `hass_spec/packages` o `hass_spec/dashboards` è cambiato: solo i due documenti
`.md` si sono spostati in `docs/`, vedi [00_INDICE.md](00_INDICE.md)).

> Il testo sotto nasce come "direttive AI" per generare la configurazione Home Assistant da zero:
> quella configurazione **è stata generata e installata** (Lab, vedi
> [05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md)). Il documento resta qui perché lo schema di
> ogni payload e topic è ancora quello attivo; leggilo come contratto di riferimento, non come
> compito da eseguire. Per i diagrammi di sequenza e la pipeline di validazione lato ESP32 vedi
> [03_PROTOCOLLO_MQTT.md](03_PROTOCOLLO_MQTT.md).

## RUOLO E COMPITO (storico — testo originale delle direttive)

Sei un esperto di Home Assistant. Il tuo compito è generare la configurazione
HASS completa per integrare **fermentatore_v6** su due device separati:
- **F1** (`fermenter_id = f1`) — primo fermentatore
- **F2** (`fermenter_id = f2`) — secondo fermentatore

Devi produrre:
1. `configuration.yaml` — snippet entità MQTT (sensori, binary_sensor, button, number, select)
2. `lovelace_fermentatore_v6.yaml` — dashboard Lovelace con viste F1, F2, Overview, Autotuning
3. `automations_v6.yaml` — automazioni (allarmi email, alert offline, monitoring)
   **Nota:** le automazioni HA sono di monitoring, NON di recovery operativa.
   Il firmware si recupera autonomamente dal NVS senza bisogno di HA.
4. `scripts_v6.yaml` — script HASS (workflow cambio mode, autotune, invio profilo)

**Configurazione pronta** in `hass_spec/packages/`, due file per fermentatore (F2 generato da
F1 con `genera_f2.py`):
- `fermentatore_v6_{id}_allarmi.yaml` — compatibile con firmware 6.1.x e 6.2.x: temperatura fuori
  range (scostamento oltre soglia per N minuti e non in avvicinamento, confronto con l'istantanea
  di 30 minuti prima) e dati fermi (nessun messaggio su `esp/state` da 6 minuti).
- `fermentatore_v6_{id}_entita.yaml` — richiede firmware >= 6.2.0: entità MQTT con disponibilità e
  scadenza, sensori diagnostici, allarmi offline, sensore guasto e riavvio anomalo. Mantiene gli
  unique_id esistenti; le istruzioni di migrazione sono in testa al file.
- `fermentatore_v6_f1_laboratorio.yaml` e `fermentatore_v6_f1_collaudo.yaml` (2026-09-18/19, solo F1):
  modalità Didattica/Collaudo, preset, esperimenti, test automatici — vedi
  [05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md) e [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md).
- `fermentatore_v6_{id}_metriche.yaml` (2026-09-20): oscillazioni, accensioni e duty dei relè calcolati in
  HA dalle entità già pubblicate (`temp_reale`, `setpoint`, `riscaldatore`, `refrigeratore`, `errore`,
  modo e parametri attivi). Nessun campo nuovo nel contratto MQTT né modifiche al firmware.

Richiedono in `configuration.yaml`: `homeassistant: packages: !include_dir_named packages`.

> Il riferimento storico a una baseline `HASS_SPEC_v5_v5A.txt` non esiste più nel repository
> (probabilmente rimosso durante il riordino della documentazione V6): non cercarlo, la baseline v5
> reale è nel codice sotto `obsolete/fermentatore_v5`.

---

## 1. CONTRATTO MQTT V6 — TOPIC MAP

Il prefisso è parametrico su `{id}` = `f1` oppure `f2`.

### 1.1 HA → ESP32 (comandi)

| Topic                                          | QoS | Descrizione                     |
|------------------------------------------------|-----|---------------------------------|
| `fermentazione/v6/{id}/ha/cmd/start`           | 1   | Avvia controllo                 |
| `fermentazione/v6/{id}/ha/cmd/stop`            | 1   | Ferma controllo, vai STOPPED    |
| `fermentazione/v6/{id}/ha/cmd/reboot`          | 1   | Riavvia ESP32                   |
| `fermentazione/v6/{id}/ha/cmd/profile_set`     | 1   | Imposta profilo fermentazione   |
| `fermentazione/v6/{id}/ha/cmd/control_set`     | 1   | Imposta algoritmo + parametri   |
| `fermentazione/v6/{id}/ha/cmd/autotune_start`  | 1   | Avvia autotuning                |
| `fermentazione/v6/{id}/ha/cmd/autotune_stop`   | 1   | Ferma autotuning                |
| `fermentazione/v6/{id}/ha/cmd/autotune_apply`  | 1   | Applica risultati autotuning    |

**Non esiste più ping/pong in V6.** La diagnostica avviene via `/esp/state`
e `/esp/log`.

### 1.2 ESP32 → HA (telemetria)

| Topic                                          | retain | Frequenza / Trigger             |
|------------------------------------------------|--------|---------------------------------|
| `fermentazione/v6/{id}/esp/state`              | YES    | ogni 30 s e subito al cambio relè/FSM |
| `fermentazione/v6/{id}/esp/availability`       | YES    | `online` alla connessione, `offline` = LWT |
| `fermentazione/v6/{id}/esp/controllo`          | YES    | insieme a esp/state (firmware >= 6.3.0) |
| `fermentazione/v6/{id}/esp/diag`               | YES    | insieme a esp/state (firmware >= 6.4.2) |
| `fermentazione/v6/{id}/esp/ack`                | no     | dopo ogni comando               |
| `fermentazione/v6/{id}/esp/alarm`              | no     | su eventi anomali (non più LWT) |
| `fermentazione/v6/{id}/esp/autotune_state`     | no     | aggiornamenti progresso autotune|
| `fermentazione/v6/{id}/esp/autotune_result`    | YES    | risultato finale autotune       |
| `fermentazione/v6/{id}/esp/log`                | no     | log diagnostico                 |

---

## 2. PAYLOAD SCHEMI

### 2.1 esp/state (heartbeat — retain=true)

```json
{
  "temp_reale":        20.35,
  "setpoint":          22.00,
  "control_mode":      "PID",
  "step_nome":         "Fermentazione primaria",
  "step_index":        0,
  "ore_step":          12.5,
  "ore_totali":        36.0,
  "riscaldatore":      true,
  "refrigeratore":     false,
  "allarme":           false,
  "profilo":           "IPA_2024",
  "stato_fsm":         "RUNNING",
  "pid_output":        42.3,
  "duty_riscaldatore": 42.3,
  "duty_refrigeratore": 0.0,
  "sensore_ok":        true,
  "temp_eta_s":        3,
  "ntp_sync":          true,
  "uptime_s":          86400,
  "reset_reason":      "poweron",
  "rssi":              -61,
  "heap_libero":       180000,
  "commutazioni":      42,
  "sensore_diag":      {"letture": 5210, "non_valide": 3, "scollegato": 1, "poweron_85": 0,
                        "fuori_scala": 0, "picco": 2, "vicino_rele": 1, "power_cycle": 0},
  "firmware":          "6.2.0",
  "hardware_target":   "esp32-wroom",
  "fermenter_id":      "f1"
}
```

Valori possibili `stato_fsm` (emessi dal firmware): `RUNNING | HOLDING | STOPPED |
AUTOTUNE | ALLARME_SENSORE`. Gli stati `WIFI_CONNESSIONE`, `MQTT_CONNESSIONE`,
`RICHIESTA_PROFILO` e `STANDBY` delle versioni precedenti non vengono più pubblicati.

Note sui campi (firmware 6.2.0):
- `riscaldatore` / `refrigeratore`: stato fisico del relè (non la richiesta del controllore).
- `temp_reale`: ultima lettura valida; `null` se il sensore non ha mai letto un valore valido.
- `sensore_ok`: `false` quando la temperatura non è utilizzabile e i relè sono forzati OFF.
  In HA mostrare la temperatura come sconosciuta in questo caso.
- `temp_eta_s`: età dell'ultima lettura valida in secondi (`-1` = mai).
- `sensore_diag` (firmware >= 6.4.1): contatori dal boot delle letture del DS18B20, che ripartono da zero a
  ogni riavvio. `letture` = conversioni completate; `non_valide` = scartate = `scollegato` (NaN o -127)
  + `poweron_85` + `fuori_scala` (fuori −10…40 °C) + `picco` (filtro anti-picco); `vicino_rele` = scartate
  entro 5 s da una commutazione di un relè (sottoinsieme di `non_valide`); `power_cycle` = cicli di
  alimentazione del sensore. Le letture scartate non arrivano mai in `temp_reale`: solo questi contatori
  le rendono visibili. Assente con firmware < 6.4.1.
- `stima` (firmware >= 6.5.0): regolazione stimata con sensore perso (`regolazione_stimata.h`).
  `pronta` = stima del carico disponibile (servono 2 h contigue di controllo normale); `attiva` = relè
  pilotati dalla stima perché il DS18B20 è perso; `scaduta` = superate 72 h cumulative, relè spenti;
  `freddo`/`caldo` = duty [%] che la stima applica (o applicherebbe) al setpoint corrente su finestre di
  40 min; `ore` = ore cumulative di regolazione stimata (sopravvivono ai riavvii software, si azzerano al
  ritorno del sensore); `u_freddo`/`u_caldo` [%], `t_media` [°C], `pendenza` [°C/h], `blocchi` (da 15 min)
  = apprendimento sulle ultime 2-4 h. Con `attiva` il profilo continua ad avanzare.
  Esempio: `"stima":{"pronta":true,"attiva":false,"scaduta":false,"freddo":8.0,"caldo":0.0,"ore":0.00,
  "u_freddo":8.0,"u_caldo":0.0,"t_media":19.25,"pendenza":-0.019,"blocchi":16}`. Con questo oggetto
  `esp/state` arriva a ~1050 byte (buffer 1280, MQTT 4096).
- `reset_reason`: `poweron | software | panic | task_wdt | int_wdt | wdt | brownout | external | deepsleep | other`.

Valori possibili `control_mode`: `on_off | on_off_hyst_sym | on_off_hyst_asym |
P | PI | PD | PID`

### 2.1b esp/controllo (firmware >= 6.3.0, retain=true)

Grandezze interne del controllore, per la didattica. Pubblicato insieme a `esp/state`.

```json
{
  "control_mode": "PID", "attivo": true, "temp": 18.06, "setpoint": 20.0,
  "errore": 1.938, "p": 19.38, "i": 23.39, "d": -0.82, "uscita": 41.95, "integrale": 2339.0,
  "duty_caldo": 41.9, "duty_freddo": 0.0, "rele_caldo": true, "rele_freddo": false,
  "isteresi": 0.8, "isteresi_caldo": 0.8, "isteresi_freddo": 0.8,
  "kp_caldo": 10.0, "ki_caldo": 0.01, "kd_caldo": 20000.0,
  "kp_freddo": 10.0, "ki_freddo": 0.01, "kd_freddo": 20000.0,
  "zona_morta": 2.5, "uscita_minima": 5.0, "finestra_s": 600, "uptime_s": 1213
}
```

- `errore` = setpoint − temperatura [°C]; `p`, `i`, `d`, `uscita` in % (positivo = caldo, negativo = freddo).
- Modi on/off: `p`, `i`, `d` = 0 e `uscita` = +100 / −100 / 0 secondo il relè richiesto.
- `uscita` è P+I+D limitata a ±100 %, prima di zona morta (sotto la quale il relè resta spento) e uscita
  minima (valore sotto cui il duty viene alzato). Il relè segue poi la finestra PWM di `finestra_s`.
- `d` è calcolato sulla misura (−Kd·dT/dt) con filtro di 300 s.
- Controllo fermo o sensore non utilizzabile: `attivo` false e termini a 0.

### 2.1c esp/diag (firmware >= 6.4.2, retain=true)

Diagnostica dei reset e dei tempi del loop. Pubblicato insieme a `esp/state`, in un topic a parte perché
`esp/state` è già vicino al limite del buffer (test host `stato_json`: meno di 900 byte).

```json
{
  "reset": {"valido": true, "hw": 16, "hw_nome": "RTCWDT_RTC_RESET", "rtc_azzerato": false,
            "giro_ms": 1204, "comm_ms": 412, "comm_rele": "freddo", "comm_stato": "off",
            "sezione": "controllo", "loop_max_ms": 85, "loop_max_sez": "display"},
  "loop": {"max_ms": 12, "max_sez": "rete", "lenti_1s": 0, "lenti_10s": 0, "setup_ms": 8123},
  "uptime_s": 1213, "firmware": "6.4.2", "fermenter_id": "f1"
}
```

- `reset` descrive **il reset che ha preceduto l'ultimo avvio**; non cambia finché l'ESP32 non si riavvia.
  - `hw`, `hw_nome`: codice hardware della ROM (`esp_rom_get_reset_reason`), lo stesso che la seriale stampa
    come `rst:0x10 (RTCWDT_RTC_RESET)`. Distingue i watchdog hardware (RTC, timer group) dal task watchdog del
    firmware, che in `esp/state.reset_reason` compare come `task_wdt`.
  - `giro_ms`: millisecondi tra l'ultimo giro del loop e l'avvio del nuovo firmware. `comm_ms`, `comm_rele`
    (`caldo`|`freddo`), `comm_stato` (`on`|`off`): la stessa cosa per l'ultima commutazione di un relè.
    Risoluzione di `giro_ms`: 100 ms; precisione dei tempi: circa ±10 % (clock RTC lento).
  - `sezione`: dove si trovava il firmware (`seriale`, `rete`, `sensore`, `display`, `controllo`, `uscite`,
    `heartbeat`, `nvs`, `boot`; `nessuna` = tra due giri; `ignota` = dato non integro).
  - `loop_max_ms`, `loop_max_sez`: massimo intervallo tra due giri del loop nel boot precedente e sezione più lenta.
  - `valido` false: la memoria RTC non era integra (vera perdita di alimentazione): gli altri campi non contano.
    `rtc_azzerato` true: il contatore RTC è ripartito da zero (reset del dominio RTC): `giro_ms` e `comm_ms` sono -1.
  - `giro_ms` e `comm_ms` valgono -1 se non calcolabili (nessuna commutazione, contatore azzerato).
- `loop` riguarda il **boot corrente**: `max_ms` è il massimo intervallo tra due giri dal boot, `max_sez` la
  sezione responsabile, `lenti_1s` e `lenti_10s` contano i giri oltre 1 s e 10 s, `setup_ms` la durata di `setup()`
  (WiFi e MQTT del boot compresi). Un caricamento OTA compare come un giro lento nella sezione `rete`.
- Firmware < 6.4.2: il topic non esiste, le entità `sensor.f1_v6_reset_*` e `sensor.f1_v6_loop_*` restano sconosciute.

### 2.2 esp/ack

```json
{
  "schema_version": "1.0",
  "fermenter_id":   "f1",
  "request_id":     "f1-ctrl-0001",
  "command":        "control_set",
  "cmd":            "control_set",
  "status":         "ok",
  "reason_code":    null,
  "profilo":        "IPA_2024",
  "uptime_s":       3600
}
```

In caso di errore: `"status": "error"` e `"reason_code": "<codice_errore>"`.
Eventi non legati a un comando: `"status": "event"`, `"command": "profile_complete"` e
`"event": "profilo_completato"` (uguale a `reason_code`).

Codici introdotti in 6.2.0: `possible_retained_ignored`, `stale_command`, `duplicate_ignored`
(status ok), `mode_already_active` (status ok), `invalid_pid_params`, `autotune_disabled`,
`profile_applied`, `profile_stored`, `too_many_steps`, `step_temp_out_of_range`,
`step_temp_missing`, `step_duration_missing`, `step_duration_invalid`, `invalid_avvio_epoch`.

### 2.3 esp/alarm

```json
{
  "tipo":           "sensore_offline",
  "durata_minuti":  5,
  "uptime_s":       3600
}
```

Dal firmware 6.2.0 il LWT **non** è più su questo topic: vedi `esp/availability`
(retained, payload `offline`). `esp32_offline` non viene più pubblicato.

Valori `tipo`: `sensore_instabile` (nessuna lettura valida da 20 s, relè OFF) |
`sensore_offline` (dopo `durata_minuti`) | `recovery_sensore` | `riavvio_in_corso` |
`riavvio_sospeso` (sensore guasto dopo 3 riavvii: resta in allarme senza riavviare).
Dal firmware 6.5.0: `regolazione_stimata` (sensore perso, relè pilotati dalla stima del carico) |
`fine_regolazione_stimata` (sensore tornato) | `regolazione_stimata_non_disponibile` (sensore perso
senza stima: relè OFF come prima) | `regolazione_stimata_scaduta` (oltre 72 h: relè OFF). Con
`regolazione_stimata` attiva, `sensore_instabile` non significa più "relè OFF".

### 2.4 esp/autotune_state

```json
{
  "state":              "running",
  "phase":              "cool",
  "phase_index":        1,
  "phase_total":        3,
  "phase_relay":        "cool",
  "phase_target_temp_c": 4.0,
  "cycles_done":        2,
  "progress":           62.5,
  "temp":               4.1,
  "ts":                 1770000100
}
```

Valori `state`: `idle | running | completed | error | aborted`
Valori `phase`: `heat | cool`

Campi `phase_*` presenti solo quando `state == "running"`. Per sessioni
con un'unica fase (legacy), `phase_index == 0` e `phase_total == 1`.

### 2.5 esp/autotune_result (retain=true)

```json
{
  "result_id":     "f1-2026-05-26T12:40:00Z",
  "schema_version": "1.0",
  "quality_score": 0.83,

  "heat": {
    "target_temp_c":  22.0,
    "quality_score":  0.87,
    "kp": 4.875,
    "ki": 0.00425,
    "kd": 0.0,
    "Tu_s": 143.2,
    "Au_C": 0.847
  },

  "cool_phases": [
    {
      "target_temp_c":  12.0,
      "quality_score":  0.82,
      "kp": 2.1,
      "ki": 0.0018,
      "kd": 0.0,
      "Tu_s": 210.5,
      "Au_C": 0.91
    },
    {
      "target_temp_c":  4.0,
      "quality_score":  0.79,
      "kp": 1.4,
      "ki": 0.0012,
      "kd": 0.0,
      "Tu_s": 380.2,
      "Au_C": 1.15
    }
  ],

  "cool_recommended": {
    "strategy":      "conservative",
    "quality_score": 0.79,
    "kp": 1.4,
    "ki": 0.0012,
    "kd": 0.0
  },

  "kp_heat": 4.875,
  "ki_heat": 0.00425,
  "kd_heat": 0.0,
  "kp_cool": 1.4,
  "ki_cool": 0.0012,
  "kd_cool": 0.0,
  "Tu_s":    143.2,
  "Au_C":    0.847
}
```

**Backward compatibility**: i campi flat `kp_heat`/`ki_heat`/`kp_cool`/`ki_cool` sono
always presenti a livello root e riflettono `heat.kp` e `cool_recommended.kp/ki`.
Il comando `autotune_apply` usa solo questi campi flat — nessuna modifica necessaria.

Strategie `cool_recommended`:
- `conservative` — usa il Kp/Ki più basso tra tutte le fasi cool (default)
- `warmest` — usa i parametri della fase cool a temperatura più alta
- `average` — media pesata per quality_score

---

## 3. PAYLOAD COMANDI (HA → ESP)

Tutti i comandi richiedono i campi comuni: `schema_version`, `request_id`, `ts`.
In HASS generali dinamicamente con template Jinja2.

Regole di consegna (firmware 6.2.0):
- **Mai `retain: true` sui comandi.** Un comando retained viene riconsegnato a ogni riconnessione.
  Il firmware ignora i comandi ricevuti entro 10 s dalla sottoscrizione senza `ts` recente
  (`possible_retained_ignored`). Per rimuovere un retained rimasto sul broker pubblicare un
  payload vuoto con retain sullo stesso topic.
- `ts` numerico in secondi (`{{ now() | as_timestamp | int }}`, senza virgolette). Con orologio
  sincronizzato i comandi con `ts` più vecchio di 10 minuti sono rifiutati (`stale_command`).
- Stesso `request_id` **e** stesso `ts`: duplicato ignorato. Con `ts: 0` non c'è deduplica
  (gli id fissi come `f1-stop` restano validi). `stop` è sempre eseguito.

### 3.1 ha/cmd/stop

```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-stop-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "reason": "mode_change"
}
```

### 3.2 ha/cmd/start

```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-start-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}"
}
```

### 3.3 ha/cmd/control_set (esempio PID)

```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-ctrl-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "control_mode": "pid",
  "params": {
    "kp_heat": 5.0,
    "ki_heat": 0.005,
    "kd_heat": 0.0,
    "kp_cool": 1.0,
    "ki_cool": 0.001,
    "kd_cool": 0.0,
    "pwm_period_s": 60,
    "min_pompa_on_s": 5
  }
}
```

`control_set` con un **modo diverso** è accettato **solo in stato STOPPED**. Seguire la
sequenza: **stop → attendi ack ok → control_set → attendi ack ok → start**.
Con lo **stesso modo** già attivo (firmware >= 6.2.0) i parametri si aggiornano al volo
(`reason_code: mode_already_active`).

Parametri opzionali di isteresi (firmware >= 6.3.0), in °C da 0,1 a 3,0, salvati in NVS:
`"isteresi"` (simmetrica: imposta anche caldo e freddo), `"isteresi_caldo"`, `"isteresi_freddo"`.
Esempio: `"params": {"isteresi": 0.8}`. Fuori limite: `invalid_isteresi`.
`pwm_period_s` e `min_pompa_on_s` nell'esempio sopra sono ignorati: finestra e tempi minimi dei
relè sono costanti del firmware.

### 3.4 ha/cmd/profile_set

```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-prof-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "profilo": "IPA_2024",
  "avvio_epoch": 0,
  "steps": [
    { "step": "Fermentazione", "temp": 22.0, "duration_hours": 168 },
    { "step": "Cold crash",    "temp": 3.0,  "duration_hours": 48  }
  ]
}
```

`avvio_epoch = 0` → ESP usa l'ora NTP dell'istante di ricezione (se NTP non è sincronizzato,
alla prima sincronizzazione). Prima del firmware 6.2.0 il profilo restava al primo step.
Step con rampa: usare `"ramp_hours"` al posto di `"duration_hours"`.
Campo opzionale `"avvia": false` → salva il profilo senza avviare il controllo (default `true`).

Validazione: 1..20 step, `temp` obbligatoria in 0..35 °C, `duration_hours`/`ramp_hours`
obbligatoria e compresa tra 0 (escluso) e 8760. Un profilo non valido viene rifiutato per intero.

### 3.5 ha/cmd/autotune_start

> **Firmware 6.2.0: autotune disabilitato** (`AUTOTUNE_ABILITATO 0`). L'implementazione non
> pilota i relè e non misura il processo; `autotune_start` e `autotune_apply` rispondono
> `autotune_disabled`. Il resto di questa sezione descrive il contratto previsto.

**Modalità fasi esplicite** (raccomandata):
```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-at-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "target_mode": "pid",
  "phases": [
    {
      "relay":            "heat",
      "target_temp_c":    20.0,
      "relay_amplitude_c": 2.0,
      "max_cycles":       5,
      "max_duration_s":   3600
    },
    {
      "relay":            "cool",
      "target_temp_c":    12.0,
      "relay_amplitude_c": 2.0,
      "max_cycles":       5,
      "max_duration_s":   5400
    },
    {
      "relay":            "cool",
      "target_temp_c":    4.0,
      "relay_amplitude_c": 1.5,
      "max_cycles":       5,
      "max_duration_s":   7200
    }
  ],
  "cool_recommendation": "conservative",
  "safety_limits": {
    "max_temp_c":          35.0,
    "min_temp_c":          0.0,
    "max_total_duration_s": 28800
  }
}
```

**Modalità auto** (il firmware sceglie le temperature in autonomia):
```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-at-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "target_mode": "pid",
  "auto_phases": true,
  "auto_config": {
    "heat_temp_c":   null,
    "cool_temps_c":  null,
    "cool_steps":    2,
    "cool_min_c":    2.0,
    "cool_max_c":    14.0
  },
  "cool_recommendation": "conservative",
  "safety_limits": {
    "max_temp_c":          35.0,
    "min_temp_c":          0.0,
    "max_total_duration_s": 28800
  }
}
```

Quando `auto_phases: true` il firmware calcola:
- `heat_temp_c = null` → usa `temp_standby` da NVS
- `cool_temps_c = null` → distribuisce `cool_steps` punti in [cool_min_c, cool_max_c]

Campo `relay_amplitude_c` (per fase esplicita): ampiezza del relay step.
Usa 2.0°C per la fase heat e 1.5–2.0°C per le fasi cool.
Valori più bassi = più cicli necessari ma parametri più precisi.

### 3.6 ha/cmd/autotune_apply

```json
{
  "schema_version": "1.0",
  "request_id": "{{ 'f1-apply-' ~ now().strftime('%H%M%S') }}",
  "ts": "{{ now() | as_timestamp | int }}",
  "result_id": "{{ states('sensor.f1_v6_autotune_result_id') }}",
  "require_quality_score_gte": 0.75
}
```

---

## 4. CONVENZIONI NAMING

| Tipo          | Pattern                         | Esempio F1                        |
|---------------|---------------------------------|-----------------------------------|
| unique_id     | `ferm_v6_{id}_{campo}`          | `ferm_v6_f1_temp_reale`           |
| entity name   | `"{ID} V6 {Descrizione}"`       | `"F1 V6 Temperatura"`             |
| automation id | `v6_{id}_{azione}`              | `v6_f1_notifica_allarme`          |
| script id     | `v6_{id}_{azione}`              | `v6_f1_safe_mode_change`          |

---

## 5. ENTITÀ DA GENERARE (configuration.yaml)

Per **ogni fermenter_id** (`f1` e `f2`) generare le seguenti entità MQTT:

### 5.1 Sensori numerici (state_topic: esp/state)

- `temp_reale` — °C, device_class: temperature, state_class: measurement
- `setpoint` — °C, device_class: temperature, state_class: measurement
- `pid_output` — %, range -100..+100, state_class: measurement
- `duty_riscaldatore` — %, state_class: measurement
- `duty_refrigeratore` — %, state_class: measurement
- `ore_step` — h, state_class: measurement (NON total_increasing: si azzera)
- `ore_totali` — h, state_class: measurement (NON total_increasing: si azzera)

### 5.2 Sensori testo (state_topic: esp/state)

- `step_nome` — nome step corrente
- `profilo` — nome profilo attivo
- `stato_fsm` — stato FSM firmware
- `control_mode` — algoritmo controllo corrente
- `firmware` — versione firmware
- `hardware_target` — target hardware

### 5.3 Sensori autotune (state_topic: esp/autotune_state e esp/autotune_result)

**Da autotune_state (progress):**
- `autotune_state` — stato sessione autotune (idle/running/completed/error)
- `autotune_progress` — % avanzamento globale
- `autotune_phase` — fase corrente (heat/cool) — `value_json.phase`
- `autotune_phase_target` — temperatura target fase corrente (°C) — `value_json.phase_target_temp_c`

**Da autotune_result (retain — risultati finali):**
- `autotune_quality_score` — quality score complessivo
- `autotune_result_id` — result_id per autotune_apply
- `autotune_kp_heat` — kp riscaldatore (`kp_heat` flat, backward compat)
- `autotune_ki_heat` — ki riscaldatore (`ki_heat` flat)
- `autotune_kp_cool` — kp refrigeratore raccomandato (`kp_cool` flat)
- `autotune_ki_cool` — ki refrigeratore raccomandato (`ki_cool` flat)
- `autotune_heat_qs` — quality score fase heat (`heat.quality_score`)
- `autotune_cool_p1_kp` — Kp fase cool 1 (`cool_phases[0].kp`)
- `autotune_cool_p1_qs` — quality score fase cool 1 (`cool_phases[0].quality_score`)
- `autotune_cool_p2_kp` — Kp fase cool 2 (`cool_phases[1].kp`)
- `autotune_cool_p2_qs` — quality score fase cool 2 (`cool_phases[1].quality_score`)

### 5.4 Binary sensor (state_topic: esp/state)

- `riscaldatore` — device_class: heat, payload_on/off: "true"/"false" (lowercase JSON)
- `refrigeratore` — device_class: cold, payload_on/off: "true"/"false"
- `allarme` — device_class: problem

### 5.5 Binary sensor online (state_topic: esp/availability)

```yaml
- name: "{ID} V6 Online"
  unique_id: ferm_v6_{id}_online
  state_topic: "fermentazione/v6/{id}/esp/availability"
  payload_on: "online"
  payload_off: "offline"
  device_class: connectivity
```

La versione precedente leggeva `esp/alarm` senza retain: dopo il LWT restava `off` fino
all'allarme successivo e dopo un riavvio di HA era `unknown`.

Tutte le entità che leggono `esp/state` devono avere
`availability_topic: "fermentazione/v6/{id}/esp/availability"` e i sensori di misura
`expire_after: 120`: senza, HA mostra a tempo indefinito l'ultimo valore retained.

### 5.6 Pulsanti comando (button)

- `stop` — command_topic: ha/cmd/stop, payload: `{"schema_version":"1.0","request_id":"ha-stop","ts":0,"reason":"manual"}`
- `start` — command_topic: ha/cmd/start
- `reboot` — command_topic: ha/cmd/reboot

**Nota**: i pulsanti semplici possono usare un payload minimale.
Per i workflow con ACK usa gli **script** invece dei pulsanti diretti.

### 5.7 Select — algoritmo controllo

```yaml
select:
  - name: "{ID} V6 Control Mode"
    unique_id: ferm_v6_{id}_control_mode_select
    state_topic: "fermentazione/v6/{id}/esp/state"
    value_template: "{{ value_json.control_mode }}"
    options:
      - "on_off"
      - "on_off_hyst_sym"
      - "on_off_hyst_asym"
      - "P"
      - "PI"
      - "PD"
      - "PID"
    command_topic: "fermentazione/v6/{id}/ha/cmd/control_set"
    command_template: >-
      {"schema_version":"1.0","request_id":"ha-ctrl-{{ now().strftime('%H%M%S') }}",
       "ts":{{ now()|as_timestamp|int }},"control_mode":"{{ value }}","params":{}}
```

**IMPORTANTE**: il select da solo non è sufficiente per cambiare mode in modo
sicuro. Usare lo script `v6_{id}_safe_mode_change` per il workflow
stop→control_set→start. Il select può essere usato solo per visualizzazione
dello stato corrente o per inviare control_set in stato già STOPPED.

### 5.8 Number — temp standby

Usa `state_topic` su `esp/state` con `value_template` su `setpoint`
(come in v5). Range 0..35°C, step 0.5.
Il comando va su `ha/cmd/profile_set` con steps=[{step:"Standby",temp:{value},duration_hours:8760}]
oppure implementare un helper `input_number` + script dedicato.

---

## 6. DASHBOARD LOVELACE DA GENERARE

Creare una dashboard con 4 viste:

### Vista 1: Overview (entrambi i fermentatori)

- Header cards con F1 e F2 affiancati: temperatura, setpoint, FSM, online
- Sezione allarmi attivi
- Link rapidi alle viste F1/F2

### Vista 2: F1 V6 Fermentatore

Struttura (adatta da v5 in HASS_SPEC_v5_v5A.txt Sez.4b):

1. Riga stato: `sensor.f1_v6_stato_fsm`, `binary_sensor.f1_v6_online`, `binary_sensor.f1_v6_allarme`
2. Riga temperature: gauge `temp_reale` (0-35°C), entities con setpoint + control_mode
3. Gauge PID output (-100..+100) — solo se control_mode è P/PI/PD/PID
4. Riga duty: gauge duty_riscaldatore + gauge duty_refrigeratore
5. Riga attuatori: entity riscaldatore + entity refrigeratore (state_color: true)
6. Entities profilo: profilo, step_nome, ore_step, ore_totali
7. Riga firmware: firmware, hardware_target
8. Riga pulsanti comando: Stop, Start, Reboot
9. Select control mode
10. Grafico storico 24h: temp_reale + setpoint
11. Grafico storico 24h: pid_output + duty_riscaldatore + duty_refrigeratore

### Vista 3: F2 V6 Fermentatore

Identica a Vista 2 ma tutte le entity con prefisso `f2_`.

### Vista 4: Autotuning V6

Per F1 e F2 affiancati:
- Stato autotune (`sensor.{id}_v6_autotune_state`)
- Gauge progresso
- Quality score
- Risultati: kp_heat, ki_heat, kp_cool, ki_cool
- Pulsanti script: Avvia Autotune, Ferma Autotune, Applica Risultati
- Grafico storico temperatura durante il test (usa `sensor.{id}_v6_temp_reale`)
- Card markdown istruzioni workflow:
  1. Ferma il fermentatore (Stop)
  2. Avvia autotune_start
  3. Monitora stato e progresso
  4. Se `quality_score >= 0.75` → applica con Autotune Apply
  5. Riavvia con Start

---

## 7. AUTOMAZIONI DA GENERARE

### Automazione 1: Notifica allarme sensore F1

```yaml
id: v6_f1_notifica_allarme_sensore
alias: "V6 F1 — Allarme sensore"
trigger:
  - platform: mqtt
    topic: "fermentazione/v6/f1/esp/alarm"
condition:
  - condition: template
    value_template: "{{ trigger.payload_json.tipo == 'sensore_offline' }}"
action:
  - service: notify.mobile_app_<tuo_telefono>
    data:
      title: "Fermentatore V6 F1 — ALLARME"
      message: "Sensore DS18B20 offline da {{ trigger.payload_json.durata_minuti }} min"
```

Generare la stessa automazione per F2.

### Automazione 2: Notifica ESP offline F1 + F2

Trigger: `binary_sensor.f1_v6_online` → OFF (LWT)
Action: notifica "Fermentatore V6 F1 offline!"

### Automazione 3: Risposta richiesta profilo (NON esiste più in V6)

**DIFFERENZA IMPORTANTE vs V5**: in V6 non c'è il topic `richiedi_profilo`
con handshake asincrono. Il profilo viene inviato con `ha/cmd/profile_set`
tramite script o UI. NON generare l'automazione di risposta automatica.

Invece, generare una **automazione di re-invio profilo su reconnect**:

```yaml
id: v6_f1_reinvia_profilo_su_reconnect
alias: "V6 F1 — Reinvia profilo su reconnect"
trigger:
  - platform: state
    entity_id: binary_sensor.f1_v6_online
    to: "on"
    for:
      seconds: 10
condition:
  - condition: template
    value_template: "{{ states('sensor.f1_v6_stato_fsm') in ['STANDBY', 'STOPPED'] }}"
action:
  - service: script.v6_f1_invia_profilo
```

(L'azione chiama lo script che invia il profilo attualmente configurato.)

### Automazione 4: Allerta quality score basso post-autotune

Trigger: `sensor.f1_v6_autotune_state` → "completed"
Condition: `sensor.f1_v6_autotune_quality_score | float < 0.75`
Action: notifica "Autotune F1 completato con quality_score basso: {score}"

---

## 8. SCRIPT HASS DA GENERARE

### Script 1: v6_f1_safe_mode_change

Sequenza sicura cambio algoritmo. Richiede variabile `control_mode` (string)
e payload params (object).

```yaml
v6_f1_safe_mode_change:
  alias: "V6 F1 — Cambio mode sicuro"
  fields:
    control_mode:
      description: "Algoritmo (on_off/pid/...)"
      default: "on_off_hyst_sym"
    params:
      description: "Oggetto parametri JSON"
      default: {}
  sequence:
    # Step 1: Stop
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/stop"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-stop-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }},"reason":"mode_change"}
        qos: 1
    # Step 2: Attendi ack ok (max 10s)
    - wait_for_trigger:
        - platform: mqtt
          topic: "fermentazione/v6/f1/esp/ack"
          value_template: "{{ value_json.status == 'ok' }}"
      timeout: "00:00:10"
      continue_on_timeout: false
    # Step 3: control_set
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/control_set"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-ctrl-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }},"control_mode":"{{ control_mode }}",
           "params":{{ params | tojson }}}
        qos: 1
    # Step 4: Attendi ack ok (max 10s)
    - wait_for_trigger:
        - platform: mqtt
          topic: "fermentazione/v6/f1/esp/ack"
          value_template: "{{ value_json.status == 'ok' }}"
      timeout: "00:00:10"
      continue_on_timeout: false
    # Step 5: Start
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/start"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-start-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }}}
        qos: 1
```

Generare la stessa struttura per F2 (`v6_f2_safe_mode_change`).

### Script 2: v6_f1_invia_profilo

Invia `profile_set` con il profilo configurato come `input_text.f1_v6_profilo_payload`.

```yaml
v6_f1_invia_profilo:
  alias: "V6 F1 — Invia profilo"
  sequence:
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/profile_set"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-prof-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }},
           "profilo":"IPA_2024","avvio_epoch":0,
           "steps":[
             {"step":"Fermentazione","temp":22.0,"duration_hours":168},
             {"step":"Cold crash","temp":3.0,"duration_hours":48}
           ]}
        qos: 1
```

**Nota**: in produzione il payload del profilo dovrebbe provenire da un
`input_text` o `input_select` gestito nella UI. Lasciare un commento nel
codice generato che indica dove sostituire il profilo hardcoded.

### Script 3: v6_f1_autotune_start

```yaml
v6_f1_autotune_start:
  alias: "V6 F1 — Avvia Autotuning"
  sequence:
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/autotune_start"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-at-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }},"target_mode":"pid",
           "safety_limits":{"max_temp_c":30.0,"min_temp_c":0.0,"max_duration_s":5400}}
        qos: 1
```

### Script 4: v6_f1_autotune_apply

```yaml
v6_f1_autotune_apply:
  alias: "V6 F1 — Applica risultati Autotune"
  sequence:
    - service: mqtt.publish
      data:
        topic: "fermentazione/v6/f1/ha/cmd/autotune_apply"
        payload: >-
          {"schema_version":"1.0","request_id":"f1-apply-{{ now().strftime('%H%M%S') }}",
           "ts":{{ now()|as_timestamp|int }},
           "result_id":"{{ states('sensor.f1_v6_autotune_result_id') }}",
           "require_quality_score_gte":0.75}
        qos: 1
```

Generare `v6_f1_autotune_stop` (pubblica su `ha/cmd/autotune_stop` con `reason: manual_abort`).

---

## 9. REGOLE CRITICHE DI IMPLEMENTAZIONE

1. **Booleani JSON**: i payload del firmware sono JSON nativi (`true`/`false` lowercase).
   Usare `payload_on: "true"` e `payload_off: "false"` nelle binary_sensor.

2. **state_class**: aggiungere solo ai sensori numerici di misura. NON aggiungere
   a sensori testo (profilo, step, fsm, control_mode, firmware, hardware_target).

3. **retain=true per esp/state**: HA riceve l'ultimo stato al riavvio senza
   aspettare il prossimo heartbeat (30 s). Proprio per questo servono `availability_topic`
   ed `expire_after` (§5.5): un valore retained può essere vecchio di giorni.

4. **retain=true per esp/autotune_result**: i risultati persistono anche dopo
   riavvio broker/HA — utile per leggere i parametri giorni dopo il test.

5. **Workflow cambio mode**: NON usare il select per cambiare direttamente mode
   durante RUNNING. Il firmware rifiuterà il control_set fuori da STOPPED.
   Usare sempre lo script `safe_mode_change`.

6. **Autonomia firmware (REQUISITO CRITICO)**: il firmware opera in completa
   autonomia da HA e MQTT. In caso di reboot, power cut o perdita di connessione:
   - Il profilo attivo è persistito in NVS (chiave `active_profile`) insieme a
     `avvio_epoch`, `last_fsm_state`, `last_step_index`, `ore_trascorse`
   - Al boot l'ESP32 controlla NVS **prima** di connettersi a WiFi: se trova
     `last_fsm_state == RUNNING` riprende immediatamente la fermentazione
   - La perdita di MQTT durante RUNNING non interrompe nulla; il firmware
     riconnette in background
   - **HA è un canale di monitoraggio/comando, non una dipendenza operativa**

   `avvio_epoch = 0` nel profile_set → l'ESP usa l'ora NTP al momento della
   ricezione come origine temporale. Questo si usa solo per il primo avvio;
   i restart successivi usano l'epoch NVS originale.

7. **Nessun ping/pong in V6**: rimosso. Per diagnostica: check `binary_sensor.online`
   e `sensor.firmware`/`sensor.hardware_target`.

8. **F1 e F2 completamente separati**: tutte le entità devono avere unique_id
   distinti (prefisso `ferm_v6_f1_` vs `ferm_v6_f2_`). Nessuna entità condivisa.

9. **Configurazione broker**: HiveMQ Cloud TLS porta 8883 (identica a v5).
   Stesse credenziali di v5 se disponibili, oppure aggiornare in secrets.

10. **Schema version**: tutti i payload comandi devono contenere
    `"schema_version": "1.0"` — il firmware rifiuta payload senza questo campo.

11. **Comandi mai retained** e `ts` numerico recente (§3).

12. **Allarme temperatura lato HA**: il firmware protegge dai guasti del sensore e del
    controllo, ma non vede un guasto di pompa, glicole o fascia. L'automazione
    "temperatura fuori range" del pacchetto (scostamento oltre soglia per N minuti e non in
    avvicinamento) è il controllo che copre questi casi.

13. **Automazione `v6_{id}_reinvia_profilo_su_reconnect`**: considera solo `RUNNING`. Con il
    sensore online funzionante scatta a ogni riconnessione e in `HOLDING` resetta lo stato HA a
    `idle` con una notifica fuorviante. Usare `states('sensor.{id}_v6_fsm') in ['RUNNING','HOLDING']`.
    Dal firmware 6.2.0 il modo di controllo è persistente: il reinvio di `control_set` non è più
    necessario (è comunque accettato con `mode_already_active`).

---

## 10. DELTA RISPETTO A V5 — RIEPILOGO

| Aspetto               | V5                                    | V6                                      |
|-----------------------|---------------------------------------|-----------------------------------------|
| Device                | singolo F1                            | F1 + F2 separati                        |
| Topic prefisso        | `fermentazione/v5/f1/esp/stato`       | `fermentazione/v6/{id}/esp/state`       |
| Cmd prefisso          | `fermentazione/v5/f1/ha/{cmd}`        | `fermentazione/v6/{id}/ha/cmd/{cmd}`    |
| Profilo inviato via   | `ha/profilo` (handshake richiedi)     | `ha/cmd/profile_set` (push diretto)     |
| Algoritmo controllo   | solo PID fisso                        | 7 mode selezionabili runtime            |
| Workflow cambio mode  | non applicabile                       | stop→control_set→start obbligatorio     |
| Autotuning            | firmware separato v5_autotuning       | integrato, comandi MQTT dedicati        |
| Ack structure         | `{"stato":"avviato",...}`             | `{"status":"ok","reason_code":...}`     |
| state payload         | `stato_fsm`, no control_mode          | aggiunge `control_mode`, `hardware_target`, `firmware` |
| Ping/pong             | `ha/ping` + `esp/pong`               | rimosso                                 |
| LWT                   | `esp/allarme` + tipo=esp32_offline    | `esp/alarm` + tipo=esp32_offline        |

---

## 11. RIFERIMENTI FILE NEL REPOSITORY

Aggiornato al 2026-09-19 — i tre file `HASS_CONFIGURATORE_V6.md`, `HASS_MONITORAGGIO_V6.md` e
`HASS_BLUEPRINTS_V6.md` citati nella versione originale di queste direttive non sono mai stati
scritti con questo nome (o sono stati rimossi prima del riordino della documentazione): il loro
contenuto vive oggi nei documenti sotto.

- `fermentatore_v6/fermentatore_v6/config.h` — topic contract definitivo v6
- [05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md) — workflow operativo, comandi, allarmi, dashboard (F1)
- [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md) — KPI misurati con criteri numerici sul banco reale
- [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) — pattern F1/F2 (stessa pin map, `fermenter_id` diverso)
- [01_PROGETTO_INIZIALE_E_BACKLOG.md §5](01_PROGETTO_INIZIALE_E_BACKLOG.md#5-checklist-di-compatibilità-v6-vs-v4v5-dichiarata-al-2026-05-25) — checklist di compatibilità V4/V5 (storica)
- `HASS_SPEC_v5_v5A.txt` non esiste nel repository — la baseline v5 reale è `obsolete/fermentatore_v5`

================================================================================
  FINE DIRETTIVE (storiche) — i 4 file YAML descritti in questo documento sono stati generati e
  sono in produzione da tempo. Per ogni dubbio su un topic o campo, fai riferimento a config.h
  (Sezione 11 di questo documento) e a `hass_spec/packages/*.yaml`.
================================================================================
