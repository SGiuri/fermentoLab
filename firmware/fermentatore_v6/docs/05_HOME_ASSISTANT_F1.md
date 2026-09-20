# 05. Home Assistant per F1 — configurazione, allarmi, dashboard

**Versione documento**: 1.0 (2026-09-19) · **Stato**: corrente · **Firmware F1**: 6.3.0 · **Istanza**: HA Lab
(vedi indice [00_INDICE.md](00_INDICE.md) per l'elenco completo dei documenti V6)

Questo documento descrive come Home Assistant controlla e sorveglia il fermentatore F1: modalità di
funzionamento, pacchetti di configurazione, allarmi, dashboard e procedure. Il contratto MQTT tra
firmware e HA è in [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md); il collaudo automatico è in
[06_COLLAUDO_F1.md](06_COLLAUDO_F1.md); i punti aperti in [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

## 1. Quadro generale

- **Broker**: HiveMQ Cloud, lo stesso della produzione. Tutto ciò che il Lab pubblica su
  `fermentazione/v6/f1/ha/cmd/*` arriva al fermentatore vero. Niente MQTT discovery su questo broker.
- **F1**: ESP32 WROOM con firmware 6.3.0, fermentatore con camicia a glicole (pompa 220 V da 10-15 W sul
  relè del freddo, chiller sempre acceso) e resistenza da 30-35 W sul relè del caldo.
- **F2**: nessun hardware. I pacchetti F2 esistono (generati da quelli F1) ma nel Lab è installato solo
  quello degli allarmi.
- **Sicurezze del firmware**, sempre attive qualunque cosa faccia HA: relè spenti se la lettura del sensore
  è più vecchia di 20 s; limiti assoluti 36 °C / −1 °C; setpoint limitato a 0-35 °C; relè acceso almeno 30 s,
  spento almeno 60 s, 60 s di attesa tra caldo e freddo; watchdog di 60 s; comandi rifiutati se vecchi di
  oltre 10 minuti, se ripetuti (stesso `request_id` e `ts`) o se arrivano retained alla riconnessione.
- **Persistenza**: dalla 6.2.0 modo di controllo, guadagni, isteresi e profilo sono in NVS. Dopo un riavvio
  F1 riparte da solo con lo stesso profilo, allo stesso punto del tempo.

## 2. Modalità (`input_select.f1_v6_modalita`)

| Modalità | Uso | Cosa è permesso | Email di scostamento |
|---|---|---|---|
| **Produzione** | Birra nel fermentatore | Avvio, pausa, stop dei profili. Parametri di controllo in sola lettura, esperimenti bloccati | Sì |
| **Didattica** | Fermentatore senza birra | Preset, parametri, cambio di algoritmo, gradini di setpoint | No, solo notifica in HA |
| **Collaudo** | Test automatici, senza birra | Solo il collaudo invia comandi; script didattici e avvio bloccati | Sì |

- Il blocco è **solo in Home Assistant**: gli script controllano la modalità prima di inviare comandi. Il
  firmware accetta qualunque comando valido.
- *Torna in Produzione* applica sempre il controllo di produzione: **on/off con isteresi 0,5 °C**
  (Kp 25, Ki 0,03, Kd 0 inviati ma non usati in on/off).
- Gli allarmi tecnici (offline, sensore, dati fermi, riavvii) mandano l'email in tutte le modalità.

## 3. Pacchetti (`hass_spec/packages`)

Nel Lab si trovano in `/config/packages/` (`homeassistant: packages: !include_dir_named packages`).

| File | Contenuto | Nel Lab |
|---|---|---|
| `fermentatore_v6_f1_entita.yaml` | Entità MQTT di `esp/state`, `esp/controllo` (errore, P, I, D, uscita, guadagni e isteresi attivi) e `esp/availability`; sensore dei riavvii spontanei; automazioni offline, online, sensore guasto, riavvio anomalo, riavvio spontaneo | sì |
| `fermentatore_v6_f1_allarmi.yaml` | Soglia e minuti di scostamento, istantanea ogni 30 min, "dati in arrivo", "in avvicinamento", allarme temperatura; automazioni fuori range, rientrata, dati fermi, dati ripresi | sì |
| `fermentatore_v6_f1_laboratorio.yaml` | Modalità, preset, gradino, soglie SP±h, "modifiche da applicare", metriche dell'esperimento, pagella degli algoritmi; script carica preset, applica, gradino, termina esperimento, passa a Didattica/Produzione | sì |
| `fermentatore_v6_f1_metriche.yaml` | Metriche dell'algoritmo su finestra di 2 h: accensioni all'ora, tempo acceso e duty misurato per relè, periodo e ampiezza dell'oscillazione, inversioni caldo↔freddo, scarto medio con segno (vedi §5) | sì (2026-09-20) |
| `fermentatore_v6_f1_collaudo.yaml` | Selezione dei test, temperatura di parcheggio, inoltro degli ack all'app di collaudo, controllo che l'app sia viva; script entra/esci, avvia, interrompi, salta, azzera, seleziona gruppo | sì |
| `fermentatore_v6_f2_*.yaml` | Copie F2 generate da `genera_f2.py` (il collaudo è escluso) | solo `_allarmi` |

Regole: si modificano solo i file F1 e si rigenerano gli F2 con `python genera_f2.py`. Gli `unique_id`
delle entità MQTT sono quelli storici: non cambiarli, altrimenti HA crea entità nuove e perde lo storico.

## 4. Allarmi

| Allarme | Condizione | Attesa | Email | Dove |
|---|---|---|---|---|
| Temperatura fuori range | In marcia, \|T − SP\| > soglia (1,5 °C) e non in avvicinamento (lo scostamento non è sceso di almeno 0,1 °C rispetto all'istantanea di 30 minuti prima) | 60 min (regolabile) | sì, non in Didattica | allarmi |
| Temperatura rientrata | Allarme spento da 10 minuti | — | no, chiude la notifica | allarmi |
| Nessun dato dall'ESP32 | Nessun `esp/state` da 5 minuti con un profilo attivo | +1 min | sì | allarmi |
| ESP32 offline | `esp/availability` = offline (LWT) | 3 min | sì | entità |
| Sensore non utilizzabile | Il firmware dichiara il DS18B20 non valido (relè spenti) | 2 min | sì | entità |
| Riavvio anomalo | Uptime < 180 s con causa panic, task_wdt, int_wdt, wdt o brownout | — | sì | entità |
| Riavvio spontaneo | Uptime tornato indietro, causa `poweron`, meno di 90 s tra l'ultimo messaggio e il boot (non un blackout) | — | sì | entità |
| Sensore offline (firmware) | `esp/alarm` con tipo `sensore_offline` (dopo 5 min di guasto) | — | sì | automazioni legacy |
| App di collaudo ferma | Collaudo in corso e stato dell'app non aggiornato da 10 minuti | — | no, notifica | collaudo |

`sensor.f1_v6_riavvii_spontanei` conta i riavvii non comandati. È stato introdotto dopo aver scoperto che
F1 si resetta allo spegnimento della pompa (vedi [COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630)).

## 5. Dashboard

| Dashboard | File | Viste |
|---|---|---|
| **F1 Laboratorio** (`/f1-laboratorio`) | `hass_spec/dashboards/f1_v6_laboratorio_dashboard.yaml` | Panoramica · Laboratorio · Esperimenti · Fermentazione · Diagnostica |
| **F1 Collaudo** (`/f1-collaudo`) | `f1_v6_collaudo_dashboard.yaml`, **generato** da `genera_collaudo.py` | Collaudo · Guida |
| F1 V6 (`/f1-v6`) | solo nel Lab | vecchia dashboard, da rimuovere dopo la validazione di F1 Laboratorio |

Card personalizzate richieste: mushroom, apexcharts-card, card-mod, state-switch.

**F1 Laboratorio**
- *Panoramica*: stato, modalità con i pulsanti di cambio (con conferma), misure, relè, grafico di 12 ore.
- *Laboratorio*: spiegazione dal vivo dell'algoritmo attivo con i numeri correnti (On/Off, isteresi, P,
  PI, PID); grafico di temperatura, setpoint, soglie SP±h e relè; grafico di P, I, D e uscita; metriche
  dell'algoritmo (oscillazione, accensioni, duty) con grafico di 12 ore; in Didattica preset e parametri con
  *Applica al fermentatore*; parametri attivi sempre in sola lettura.
- *Esperimenti*: gradino di setpoint con metriche (sovraelongazione, assestamento ±0,2 °C per 30 min,
  errore medio e a regime, commutazioni, accensioni e duty per relè, oscillazione), percorso didattico,
  pagella degli algoritmi, registro dei risultati.
- *Fermentazione*: avvio (nascosto in Collaudo), pausa, stop con conferma, profilo, allarme di scostamento,
  email di prova.
- *Diagnostica*: collegamento, sensore, sicurezze del firmware, statistiche delle 24 ore, riavvio.

**Preset didattici** (tarati con le simulazioni di 48 h su 20 L)

| Preset | Modo | Kp | Ki | Kd | Isteresi |
|---|---|---|---|---|---|
| On/Off, isteresi 0,5 °C (produzione) | on_off_hyst_sym | 25 | 0,03 | 0 | 0,5 |
| On/Off puro, senza isteresi | on_off | 25 | 0,03 | 0 | — |
| On/Off, isteresi 1,0 °C | on_off_hyst_sym | 25 | 0,03 | 0 | 1,0 |
| P, Kp 30 | P | 30 | 0 | 0 | — |
| PI consigliato | PI | 30 | 0,002 | 0 | — |
| PID | PID | 30 | 0,002 | 5000 | — |
| PI mal tarato | PI | 1 | 0,001 | 0 | — |

*Applica al fermentatore*: con lo stesso algoritmo i parametri cambiano al volo; con un algoritmo diverso
e il controllo in marcia lo script esegue stop → `control_set` → start, aspettando gli ack.

### Metriche dell'algoritmo

Pacchetto `fermentatore_v6_f1_metriche.yaml`, tutto calcolato in Home Assistant (nessuna modifica al
firmware). Misura tre cose che vanno tenute separate:

| Grandezza | Cosa misura | On/Off | P / PI / PID |
|---|---|---|---|
| **Oscillazione** | periodo e ampiezza picco-picco di T | ciclo limite sempre presente | P: smorzata. PI: oscillazioni lente all'inizio, poi nessuna. PID: meno sovraelongazione |
| **Accensioni all'ora** | quante volte scatta il relè | uguale alle oscillazioni all'ora | **al massimo 6 per relè** (finestra di 600 s), qualunque sia il duty |
| **Duty misurato** | quota di tempo acceso | cambia a ogni ciclo | è qui che gli algoritmi modulano |

Il punto didattico: in P/PI/PID il numero di accensioni lo fissa la finestra, non l'algoritmo. Il controllore
non commuta meno, cambia il duty.

| Entità | Significato |
|---|---|
| `sensor.f1_v6_oscillazioni` | stato: `fermo`, `in misura`, `oscilla`, `nessuna oscillazione`; l'attributo `dati` contiene il rilevatore |
| `sensor.f1_v6_periodo_oscillazione`, `_ampiezza_oscillazione`, `_oscillazioni_ora` | valori numerici (non disponibili se non oscilla) |
| `sensor.f1_v6_accensioni_ora_riscaldatore`, `_refrigeratore` | accensioni all'ora, media sulle ultime 2 h |
| `sensor.f1_v6_duty_misurato_riscaldatore`, `_refrigeratore` | tempo acceso / 2 h (diverso dal *Duty* istantaneo che il controllore richiede) |
| `sensor.f1_v6_tempo_medio_acceso_riscaldatore`, `_refrigeratore` | durata media di un'accensione |
| `sensor.f1_v6_inversioni_ora` | accensioni di un relè subito dopo l'altro (caldo↔freddo), all'ora |
| `sensor.f1_v6_scarto_medio_con_segno` | media SP−T sulle ultime 2 h: ≠ 0 nel P (errore a regime), ≈ 0 nel PI |
| `input_number.f1_v6_lab_soglia_osc` | escursione minima per contare un'oscillazione (0,15 °C) |

**Come funziona il rilevatore.** Filtro a zig-zag sulle letture di temperatura: un picco è registrato quando T
scende almeno della soglia sotto il massimo, una valle quando risale. Il periodo è la media degli intervalli
picco→picco e valle→valle (non dipende dall'asimmetria tra la fase di raffreddamento e quella di
riscaldamento), l'ampiezza è la media delle escursioni picco-valle. Serve almeno un'oscillazione completa
(3 estremi). Si azzera se cambiano setpoint, algoritmo o parametri, se il controllo si ferma o se mancano dati
per più di 10 minuti.

**Limiti noti**
- La finestra è di 2 ore fisse (`history_stats` accetta solo durate costanti): dopo un cambio di algoritmo le
  metriche a finestra mescolano il prima e il dopo fino a 2 ore. Per confronti puliti c'è l'esperimento a
  gradino, che misura dall'avvio.
- Serve un'oscillazione con escursione chiaramente maggiore del rumore: il DS18B20 quantizza a 0,0625 °C. Con
  escursioni sotto ~0,3 °C (on/off puro, isteresi 0,2) il rumore crea estremi in più e il periodo risulta
  sottostimato. Provato su simulazione a 20 L: periodo entro l'1 % senza rumore, entro il 2 % con rumore
  gaussiano di 0,03 °C per isteresi 0,5 e 1,0 °C, sottostimato del 34 % con isteresi 0,2 °C.
- Periodi oltre circa 6 ore non si distinguono da una deriva lenta.
- *Errore a regime* (media mobile di 60 minuti): ha senso per P, PI e PID; con l'on/off dipende dal punto del
  ciclo in cui ci si ferma, e nella pagella quelle righe non ricevono la stella.
- La **pagella** tiene le ultime 10 prove in memoria, una per configurazione e gradino: dopo un riavvio di
  Home Assistant può ripartire vuota (il registro del laboratorio conserva comunque ogni risultato).
- Verifiche: i template sono stati provati offline con Jinja su una simulazione a 20 L, poi installati nel Lab
  il 2026-09-20 (nessun errore nel log, entità con valori coerenti su F1 reale, card rese dal motore di
  template di HA). **Non ancora provati in HA**: un esperimento a gradino completo con la pagella (richiede
  la modalità Didattica, quindi non durante il collaudo) e il comportamento della pagella dopo un riavvio.

## 6. Procedure

**Avviare la birra (Produzione)**
1. Dashboard F1 Laboratorio → Panoramica: la modalità deve essere *Produzione*.
2. Vista Fermentazione: scegliere il profilo (o la temperatura costante) e premere **Avvia**.
3. Verificare nella stessa vista l'ack "profile_set: ok" e lo stato RUNNING.

**Usare il laboratorio (senza birra)**
1. *Passa a Didattica* (chiede conferma che non ci sia birra).
2. Scegliere un preset → *Carica preset* → *Applica al fermentatore*.
3. Vista Esperimenti: ampiezza del gradino → *Avvia gradino*; a fine prova *Termina esperimento*.
4. *Torna in Produzione* a fine lavoro.

**Collaudo**: vedi [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md).

## 7. Installazione e aggiornamento nel Lab

1. **Backup** prima di ogni modifica (Impostazioni → Sistema → Backup, o API Supervisor).
2. Copiare i pacchetti in `/config/packages/`, le dashboard YAML in `/config/`.
3. Registrare le dashboard in `configuration.yaml` sotto `lovelace: dashboards:` (`mode: yaml`).
4. **Verificare la configurazione** (Strumenti per sviluppatori → Controlla configurazione).
5. Ricaricare quello che basta: `input_boolean`, `input_select`, `input_number`, `script`, `template`,
   `automation`, `mqtt` si ricaricano senza riavvio. Anche i sensori `history_stats` e `statistics` (piattaforme
   in YAML, usati dal pacchetto `_metriche`) hanno il loro `reload`: **non serve riavviare HA**, e conviene non
   farlo con un collaudo in corso (vedi sotto). Ordine usato il 2026-09-20: `input_number.reload`,
   `history_stats.reload`, `statistics.reload`, `template.reload`.
   Consigliato escludere dal recorder `sensor.f1_v6_oscillazioni`, `sensor.f1_v6_inversioni` e
   `sensor.f1_v6_esperimento`: i loro attributi cambiano a ogni misura e non servono nello storico. Serve un riavvio di HA per aggiungere una dashboard in
   `configuration.yaml` o per cambiare le opzioni degli helper definiti lì.
6. Le dashboard in modalità YAML si aggiornano ricaricando la pagina del browser.

**Con un collaudo in corso** (verificato il 2026-09-20 sul motore `collaudo_motore.py`):
- **Non riavviare HA.** Se un'eccezione coglie il motore durante il buco, `tick()` chiude il test corrente come
  «fallito: errore interno del collaudo». Le ricariche non interrompono la connessione di AppDaemon.
- Il motore legge solo entità già esistenti (`E` in `collaudo_motore.py`): un pacchetto nuovo che non le
  modifica non lo tocca. `template.reload` rigenera anche `binary_sensor.f1_v6_dati_in_arrivo`, che il motore
  conta come «tempo senza dati» (criterio non critico, ≤ 5 min): lo sfarfallio dura meno di un secondo.
- Prima di `input_number.reload` controllare che gli helper con `initial` abbiano ancora il valore iniziale
  (soglia e minuti dell'allarme, temperatura costante…): la ricarica altrimenti li riporta al valore iniziale.
- I sensori trigger-based (`riavvii_spontanei`, `scostamento_precedente`) mantengono stato e attributi dopo
  `template.reload`; verificato confrontando le entità prima e dopo.

**Helper di controllo in `configuration.yaml`** (modificati il 2026-09-18): Kp 0-100 (passo 0,1), Ki 0-0,1
(passo 0,0001), Kd 0-20000 (passo 1), isteresi 0,1-3,0 °C. Senza `initial`: prima tornavano ai valori di
fabbrica a ogni riavvio di HA.

**Modifiche alla configurazione storica del Lab** (2026-09-18)
- Rimossa l'automazione *V6 F1 — Watchdog control_mode*: a ogni `esp/state` inviava il modo del selettore
  HA anche se nessuno l'aveva applicato.
- *V6 F1 — Reinvia profilo su reconnect*: in RUNNING/HOLDING ora registra solo il modo ripristinato da NVS,
  senza inviare `control_set`.
- Gli script `f1_v6_invia_profilo` e `f1_v6_avvia_costante` e `scripts_py/avvia_fermentazione_v6.py`
  inviano anche l'isteresi (firmware ≥ 6.3.0). Dal 2026-09-20 una copia (senza credenziali) dello script è nel repository: `homeassistant/scripts_py/avvia_fermentazione_v6.py`.
- Rimosse le configurazioni v3, v4, v5 e le dashboard obsolete (304 entità orfane eliminate).

**Backup del Lab**

| ID | Data | Prima di |
|---|---|---|
| `c5a39a1c` | 2026-09-18 | pacchetto allarmi |
| `b46d74dd` | 2026-09-18 | migrazione delle entità F1 nel pacchetto |
| `84dc757d` | 2026-09-18 | pulizia v3-v5 |
| `c67e2dfe` | 2026-09-18 | dashboard F1 Laboratorio |
| `8003f7b0` | 2026-09-18 | collaudo (AppDaemon, pacchetto, dashboard) |

## 8. Insidie note

- Lo script di avvio unificato ha **entity id** `script.f1_v6_avvia_unificato` ma si chiama come servizio
  `script.f1_v6_avvia_unified`: la dashboard usa il servizio.
- In `mqtt.yaml` restano `select.f1_v6_control_mode` e i pulsanti `button.f1_v6_start/stop/reboot`: mandano
  comandi diretti a F1 e **aggirano il blocco della Produzione**. Non sono in nessuna dashboard (vedi punti
  aperti).
- In on/off il firmware continua a pubblicare `isteresi` (valore di configurazione) anche se non la usa:
  per sapere come si comporta davvero bisogna guardare le commutazioni (test *On/Off senza isteresi*).
- Nell'on/off simmetrico con il glicole, l'inerzia del freddo porta l'acqua fino a SP − 0,5 e fa accendere la
  resistenza (osservato nella tenuta a 10 °C): caldo e freddo si alternano.
