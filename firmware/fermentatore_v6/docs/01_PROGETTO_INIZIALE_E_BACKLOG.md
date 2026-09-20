# 01. Piano iniziale e backlog V6 (storico)

**Versione documento**: 2.0 (2026-09-19, contenuti dal 2026-05-17/25) · **Stato**: storico — la
pianificazione qui sotto ha guidato la costruzione di V6 ed è in gran parte completata, ma diversi
dettagli tecnici sono stati superati dall'implementazione reale (vedi §7). Per lo stato attuale del
sistema, non usare questo documento: usa [00_INDICE.md](00_INDICE.md) e i documenti correnti che
elenca.

**Fonde**: `V6_MASTER_PLAN.md` + `V6_EXECUTION_BACKLOG.md` + `V6_CHECKLIST_COMPATIBILITA_V4_V5.md`
(tutti del 2026-05-25) + le parti ancora rilevanti di `hass_spec/F1_V6_FIRMWARE_BRIEF.md`
(2026-05-17, il brief di implementazione originale per F1).

## 1. Perché questo documento

Prima di scrivere il firmware V6, il progetto è passato da una fase di pianificazione: un piano
architetturale (§2), un backlog a milestone (§3-4), una checklist di parità con V4/V5 (§5) e un
brief di implementazione dettagliato per F1, comprensivo di FSM, algoritmi di controllo e teoria
dell'autotuning (§6). Quattro documenti separati, scritti nello stesso periodo per lo stesso
obiettivo, sono stati accorpati qui perché il loro contenuto si sovrapponeva am­piamente. Il valore
che resta oggi è storico e di intento progettuale: spiega *perché* certe scelte sono state fatte.
Per *come funziona davvero* il sistema oggi, vedi §7 e i documenti correnti.

## 2. Visione e architettura originale

Scope dichiarato: consolidare tutto il materiale V6 dentro `fermentatore_v6`, un firmware unificato
per WROOM/S3, F1/F2 su due ESP32 separati, controllo runtime da Home Assistant con cambio modo
sicuro, autotuning integrato, pacchetto documentale operativo.

**Obiettivi di prodotto:**
1. Fonte unica di verità per versione/build/target con verifica automatica pre-commit.
2. Un solo firmware con modalità: `on_off`, `on_off_hyst_sym`, `on_off_hyst_asym`, `P`, `PI`, `PD`, `PID`.
3. Device separati F1/F2 con la stessa codebase e namespace isolato.
4. Autotuning integrato in `fermentatore_v6` (non più un progetto separato come `fermentatore_v5_autotuning`).
5. Configuratore e monitoraggio Home Assistant documentati end-to-end.

**Vincoli architetturali dichiarati non negoziabili:**
1. Cambio algoritmo solo tramite la sequenza stop → reset → apply → start.
2. Fail-safe: relè OFF su fault del sensore o errori critici.
3. Nessuna collisione MQTT/NVS/OTA tra F1 e F2.
4. Supporto iniziale obbligatorio su ESP32-WROOM e ESP32-S3 Super Mini.

**Layering previsto**: app (FSM, orchestrazione comandi, policy di sicurezza) · domain (profilo di
fermentazione, interpolazione del setpoint, stato del processo) · control (interfaccia comune ai 7
modi) · autotune (session manager, stima parametri, quality score, apply gate) · infra (WiFi/MQTT,
NVS, sensore, display, log) · board (pin map/feature flag per WROOM/S3).

**Contratto controller previsto**: `controller_init(config)`, `controller_apply_inputs(temp,
setpoint, now_ms)`, `controller_get_outputs()`, `controller_reset_state()`,
`controller_stop_outputs()`.

**Contratto autotune previsto**: `autotune_start(request)`, `autotune_tick(temp, now_ms)`,
`autotune_stop(reason)`, `autotune_get_state()`, `autotune_get_result()`, `autotune_apply(result_id,
quality_threshold)`.

**Prefisso MQTT previsto**: `fermentazione/v6/{fermenter_id}/ha/...` e
`fermentazione/v6/{fermenter_id}/esp/...` — confermato nell'implementazione reale, vedi
[04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md) per l'elenco esaustivo dei topic e dei payload.

**KPI dichiarati**: errore medio e overshoot; stabilità degli attuatori e tasso di commutazioni
anomale; affidabilità di rete/MQTT e recovery dai guasti; success rate dell'autotuning e
miglioramento post-apply. Il collaudo automatico ([06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)) misura
oggi la maggior parte di questi KPI, tranne l'ultimo (autotuning disabilitato, §6).

**Rischi principali dichiarati e mitigazioni**: cambio di modo in stato non sicuro → enforcement
hard su STOPPED; parametri HA incoerenti → validazione stretta e `reason_code` granulari;
divergenza WROOM/S3 → astrazione di board; autotuning in condizioni non stabili → precheck e
quality gate.

## 3. Roadmap a fasi (dichiarata)

| Fase | Output | Gate |
|---|---|---|
| A — Fondazioni SSOT e tooling | Manifest stabile, generate/verify funzionanti, hook pre-commit | `generate_build_info.ps1`/`verify_build_info.ps1` verdi |
| B — Core control runtime | 7 modalità di controllo, workflow stop/reset/apply/start | Unit test logici verdi su tutti i modi |
| C — Runtime MQTT/HASS | Parser e validator, ack/nack con `reason_code` | Contract test su payload validi/invalidi |
| D — F1/F2 + board support | Identity model F1/F2, board profile WROOM/S3 | Build matrix: WROOM-F1/F2, S3-F1/F2 |
| E — Autotuning integrato | Sessioni da HA, apply con quality gate | Apply solo in STOPPED e sopra soglia |
| F — QA, docs e release | Test funzionali + soak + fault injection, guide HA, release | Go/No-Go completo, rollback validato |

## 4. Backlog dettagliato (milestone M1-M7)

| ID | Pri | Milestone | Task | Dipendenze | Output verificabile |
| --- | --- | --- | --- | --- | --- |
| V6-001 | P0 | M1 | Stabilizzare `fermentatore_v6/v6_manifest.json` | - | Manifest validato con schema e target list |
| V6-002 | P0 | M1 | Rendere idempotente `generate_build_info.ps1` | V6-001 | `generated/v6_build_info.h/json` coerenti |
| V6-003 | P0 | M1 | Hard fail di `verify_build_info.ps1` su mismatch | V6-002 | Exit code 1 su mismatch, 0 su allineamento |
| V6-004 | P1 | M1 | Hook pre-commit al nuovo path V6 | V6-003 | Commit bloccato se verify fallisce |
| V6-005 | P0 | M1 | Consolidare docs e backlog in `fermentatore_v6/docs` | V6-001 | Nessun file V6 operativo rimasto in root |
| V6-010 | P0 | M2 | API controller comune completa | V6-003 | Interfaccia init/apply/get/reset/stop stabile |
| V6-011 | P1 | M2 | Engine on_off | V6-010 | Test logici passati su soglia setpoint |
| V6-012 | P1 | M2 | Engine on_off_hyst_sym | V6-011 | Test banda simmetrica passati |
| V6-013 | P1 | M2 | Engine on_off_hyst_asym | V6-012 | Test banda heat/cool indipendenti passati |
| V6-014 | P1 | M2 | Engine P | V6-010 | Output proporzionale clampato |
| V6-015 | P1 | M2 | Engine PI | V6-014 | Anti-windup verificato |
| V6-016 | P1 | M2 | Engine PD | V6-014 | Derivativo filtrato verificato |
| V6-017 | P1 | M2 | Engine PID | V6-015,V6-016 | PID completo con reset stato |
| V6-018 | P0 | M2 | Enforce workflow stop→reset→apply→start | V6-011..V6-017 | Cambio mode rifiutato fuori STOPPED |
| V6-020 | P0 | M3 | Freeze topic contract V6 | V6-005 | Topic list definitiva in docs |
| V6-021 | P0 | M3 | Parser `profile_set` con validation strict | V6-020 | Payload invalidi respinti con `reason_code` |
| V6-022 | P0 | M3 | Parser `control_set` con matrix parametri | V6-018,V6-020 | Compatibilità mode/params verificata |
| V6-023 | P1 | M3 | Ack/Nack standardizzati | V6-021,V6-022 | `request_id` correlato in tutte le risposte |
| V6-024 | P1 | M3 | Payload `esp/state` esteso | V6-017 | Include firmware/hw/id/mode/diagnostica |
| V6-025 | P1 | M3 | `esp/capabilities` per discovery HASS | V6-022 | Catalogo mode/parametri pubblicato |
| V6-030 | P0 | M4 | Identity model F1/F2 | V6-001 | `fermenter_id` persistente e configurabile |
| V6-031 | P0 | M4 | Namespace MQTT/NVS/OTA per `fermenter_id` | V6-030 | Zero collisioni tra due device |
| V6-032 | P0 | M4 | Board profile WROOM | V6-010 | Pin safety e boot safe verificati |
| V6-033 | P0 | M4 | Board profile S3 Super Mini | V6-010 | Pin safety e boot safe verificati |
| V6-034 | P1 | M4 | Build matrix 4 target | V6-031,V6-032,V6-033 | WROOM-F1/F2, S3-F1/F2 buildano |
| V6-040 | P0 | M5 | Test safety relay/fault sensore | V6-018 | Relay OFF su fault critici |
| V6-041 | P1 | M5 | Test comparativo mode controllo | V6-017 | Tabella overshoot/settling/stability |
| V6-042 | P1 | M5 | Soak 24-48h + fault injection rete | V6-034,V6-040 | Report reconnect/recovery |
| V6-043 | P0 | M5 | Hardening reason_code e diagnostica | V6-023,V6-024 | Troubleshooting remoto sufficiente |
| V6-044 | P0 | M5 | Release prep + rollback drill | V6-042,V6-043 | Go/No-Go con rollback provato |
| V6-050 | P0 | M6 | Porting autotuning in V6 | V6-017 | Modulo autotune interno a `fermentatore_v6` |
| V6-051 | P0 | M6 | FSM autotune lifecycle | V6-050,V6-018 | idle/running/result/apply/abort stabili |
| V6-052 | P0 | M6 | Comandi MQTT autotune start/stop/apply | V6-051,V6-022 | Ack e reason_code autotune dedicati |
| V6-053 | P1 | M6 | Guardrail autotune | V6-051 | Timeout, precheck, quality gate attivi |
| V6-054 | P1 | M6 | Persistenza risultati autotune in NVS | V6-052 | `result_id` e parametri storicizzati |
| V6-055 | P1 | M6 | Benchmark pre/post autotune | V6-054,V6-041 | Report delta performance |
| V6-060 | P0 | M7 | Guida configuratore HASS | V6-025 | Procedura setup comandi completa |
| V6-061 | P0 | M7 | Guida monitoraggio HASS | V6-024,V6-043 | KPI, alert e dashboard operativi |
| V6-062 | P1 | M7 | Blueprint F1/F2 separati | V6-060,V6-061 | Template riusabili per due canali |
| V6-063 | P1 | M7 | Validazione e2e guide su testbed | V6-062,V6-052 | Checklist firmata e issue list ridotta |
| V6-064 | P0 | M7 | Docs pack release-ready | V6-063 | Pacchetto operativo pronto al rollout |

**Sequenza settimanale dichiarata**: sett. 1 M1 + freeze input M3 · sett. 2 M2 completo · sett. 3 M3
+ inizio M4 · sett. 4 M4 completo + avvio M5 · sett. 5 M5 completo · sett. 6 M6 completo · sett. 7
M7 completo + Go/No-Go finale.

**Go/No-Go checklist dichiarata**: build matrix 4 target verde · contract test MQTT/HASS verdi ·
safety test su tutti i modi verdi · soak test completato e analizzato · autotuning validato con
quality gate (poi disabilitato, §6) · guide HA validate su F1 e F2 (F2 non ancora costruito, vedi
[10_PUNTI_APERTI.md](10_PUNTI_APERTI.md)) · rollback drill eseguito con successo.

**Stato al 2026-09-19**: M1-M5 sostanzialmente completate per F1 (firmware 6.3.0, in collaudo). M6
(autotuning) implementata e poi disattivata (§6). M7 è andata oltre il piano originale: non solo
guide, ma una dashboard didattica e una suite di collaudo automatico (vedi
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md), [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)), non
previste qui. F2 resta solo un'identità software.

## 5. Checklist di compatibilità V6 vs V4/V5 (dichiarata al 2026-05-25)

Obiettivo originale: portare V6 a parità operativa reale con V4/V5 su Home Assistant via MQTT,
mantenendo il pinout WROOM e le procedure di recovery già testate. Riportata come scritta
all'epoca — **non aggiornata voce per voce**, ma nella sostanza raggiunta dal firmware 6.1.x-6.3.0
(comandi MQTT reali, persistenza NVS, recovery del sensore, modi di controllo multipli, autonomia
senza rete: tutti osservati funzionare su F1 nel collaudo, vedi
[06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)). Le voci sull'autotuning (§9) sono superate: l'autotuning è
disabilitato (§6).

- Pinout WROOM confermato e bloccato: DS18B20=33, SENSOR_PWR=25, HEAT=16, COOL=17, SDA=21, SCL=22
  — vedi [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) (valori diversi da quelli nel brief di
  implementazione originale, §7).
- Connettività MQTT reale (client WiFi + TLS, subscribe/publish, LWT, reconnect non bloccante) —
  raggiunto.
- Autonomia dopo istruzioni HA: `profile_set` reale, persistenza NVS di profilo/parametri/timestamp,
  ripristino da NVS al boot senza attendere HA — raggiunto.
- Continuità operativa senza internet: controllo termico locale anche a WiFi/MQTT caduti — raggiunto
  e verificato nel collaudo (test *Riavvio durante la regolazione*, *Mancanza di corrente*).
- Diagnostica: heartbeat con temperatura/setpoint/stato/modo/attuatori/firmware/hardware, ack con
  `request_id`/`status`/`reason_code`, allarmi per sensore offline/reboot/recovery/errore comando —
  raggiunto, esteso in 6.3.0 con `esp/controllo`.
- Recovery del sensore in parità con V4/V5: watchdog DS18B20, filtro oscillazioni incoerenti, streak
  di letture valide, power-cycle con tentativi limitati, riavvio ritardato su allarme persistente —
  raggiunto (`sensor_watchdog.cpp`).
- Controllo termico: on/off, isteresi simmetrica/asimmetrica, P/PI/PD/PID con guardrail, interlock
  heat/cool — raggiunto, con anti-windup e derivata diversi da come erano stati pensati (§7).
- Autotuning: implementato (M6) poi disabilitato — vedi §6.

## 6. Autotuning — storia e stato

**Stato attuale: disabilitato.** `#define AUTOTUNE_ABILITATO 0` in `config.h`: ogni comando
`ha/cmd/autotune_*` viene rifiutato con `reason_code: autotune_disabled`. Il codice FSM/relay-feedback
esiste ancora nel firmware ma non è raggiungibile da HA. Non c'è una nota nel CHANGELOG che spieghi
quando o perché è stato disattivato: verificare se serve riattivarlo o rimuoverlo (vedi
[10_PUNTI_APERTI.md](10_PUNTI_APERTI.md)).

**Principio progettato** (relay feedback, Åström–Hägglund): porta il sistema al target della fase,
poi commuta il relè bang-bang con un'ampiezza attorno al setpoint e misura le oscillazioni
picco-picco per un numero di cicli o un tempo massimo. Da periodo (`Tu`) e ampiezza (`Au`) misurati
calcola il guadagno ultimo `Ku = 4·ampiezza / (π·Au)` e i guadagni Ziegler-Nichols per un
controllore PI (`Kp = 0,45·Ku`, `Ki = 1,2·Kp / Tu`, `Kd = 0`). Una *quality score* combina i cicli
completati e la regolarità dei periodi.

**Sequenza multi-fase progettata**: fasi in ordine da un array ricevuto in `autotune_start`
(`FASE_INIT → FASE_PORTATA_A_SETPOINT → FASE_RELAY → FASE_CALCOLO → prossima fase o
completamento`), un FSM parallelo a quello principale (che resta in `STANDBY`), notifiche
`esp/autotune_state` ogni 5 s durante la fase relay. In modalità `auto_phases: true` il firmware
costruisce da solo una fase di riscaldamento e più fasi di raffreddamento a temperature scalate.
Strategia di raccomandazione finale: `conservative` (Kp minore, default), `warmest` (temperatura
operativa più comune), `average` (media pesata per quality score).

**Sicurezze progettate**: abort su superamento dei limiti di temperatura, timeout globale, rispetto
dei tempi minimi dei relè anche durante il relay feedback, abort esterno via `autotune_stop`.

## 7. Scostamenti tra il piano e la realtà

Elenco di dove l'implementazione reale differisce da quanto pianificato in questo documento —
utile per non fidarsi ciecamente dei dettagli tecnici sopra (validi come intento, non come
riferimento):

| Argomento | Piano originale (qui sopra / nel brief) | Realtà (6.3.0) |
|---|---|---|
| Identità F1/F2 | `fermenter_id` letto da NVS a runtime, provisioning via `nvs_gen.py` | `DEVICE_FERMENTER_ID` è una macro in `config.h`, impostata a compile-time da `configure_target.ps1` — vedi [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) |
| Pin GPIO (WROOM) | Brief: relè 26/27, DS18B20 su 4 | Reale: relè 16/17, DS18B20 su 33 (più `SENSOR_PWR` su 25 per il power-cycle) — vedi [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) |
| Struttura del codice | Moduli C stile ESP-IDF (`main/*.c`, `CMakeLists.txt`, `sdkconfig.defaults`) | Sketch Arduino (.ino + .cpp/.h), niente ESP-IDF/CMake; moduli reali: `controller`, `controller_onoff`, `uscite_rele`, `sensor_watchdog`, `command_processor`, `connettivita`, `profilo`, `display` — vedi [../../CLAUDE.md](../../../CLAUDE.md) |
| Algoritmo P/PI/PID | Integrale con clamp fisso a `100/Ki`, derivata su `(errore - errore_precedente)` o su misura non filtrata | `apply_pid_family` unico: anti-windup che blocca l'integrazione solo quando l'uscita è già satura nella direzione dell'errore, termine I limitato a ±50%, derivata sulla misura filtrata con costante 300 s, zona morta 2,5% e uscita minima 5% prima della finestra PWM di 600 s — vedi CHANGELOG 6.3.0 e [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md) (test `modo_p`/`modo_pi`/`modo_pid`) |
| Salvataggio NVS | Ogni 60 s durante RUNNING | Ogni 10 minuti (osservato nel firmware 6.x) |
| Autotuning | Modulo completo, integrato in HA con guide dedicate | Implementato ma disabilitato (`AUTOTUNE_ABILITATO 0`), §6 |
| Versione firmware | Placeholder `"f1_v6_1.0.0"` nel brief | Reale: `FIRMWARE_VERSION` in `config.h`, oggi `"6.3.0"`, aggiornata a ogni voce di CHANGELOG |
| Home Assistant | Guide di configurazione e monitoraggio | Realizzato molto di più del previsto: modalità Produzione/Didattica/Collaudo, dashboard didattica con spiegazioni dal vivo, esperimenti a gradino, suite di collaudo automatico su hardware reale — niente di tutto questo era nel piano di M7 |
| Baseline v5 per HA | Riferimento a `HASS_SPEC_v5_v5A.txt` | Il file non esiste nel repository (dangling reference); la baseline v5 reale è il codice in `obsolete/fermentatore_v5` |

## 8. Struttura di codice originariamente proposta (non seguita)

Per riferimento storico, il brief proponeva una struttura ESP-IDF con moduli in C:

```
firmware_f1_v6/
├── main/
│   ├── main.c / main.cpp        # entry point, init task
│   ├── config.h                 # costanti GPIO, topic base, default NVS
│   ├── nvs_manager.{c,h}        # read/write NVS (FERMENTER_ID, WiFi, broker)
│   ├── wifi_manager.{c,h}       # connessione WiFi, riconnessione
│   ├── mqtt_manager.{c,h}       # MQTT connect, subscribe, publish, LWT
│   ├── ds18b20.{c,h}            # lettura sensore OneWire
│   ├── relay_manager.{c,h}      # controllo GPIO relay con protezione
│   ├── fsm.{c,h}                # macchina a stati, transizioni
│   ├── profile_manager.{c,h}    # parsing JSON profilo, calcolo setpoint/step
│   ├── controller.{c,h}         # on_off, on_off_isteresi, P, PI, PID
│   ├── pwm_relay.{c,h}          # logica PWM per modalità P/PI/PID
│   └── heartbeat.{c,h}          # composizione e pubblicazione payload stato
├── CMakeLists.txt
└── sdkconfig.defaults
```

Non è la struttura reale (vedi §7 e [../../CLAUDE.md](../../../CLAUDE.md) per la mappa dei moduli
effettivi dello sketch Arduino).
