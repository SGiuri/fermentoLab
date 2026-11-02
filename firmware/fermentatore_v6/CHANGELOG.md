# Changelog - Fermentatore V6

## [v6.5.1] - 2026-10-02
### Recupero del DS18B20 bloccato e display nero
- **Motivo**: dal 01/10 alle 16:00 il DS18B20 di F1 ha letto solo `-127` per 16 h: tre power-cycle, tre
  riavvii, poi `riavvio_sospeso` e relè spenti tutta la notte (la regolazione stimata non era ancora pronta:
  la 6.5.0 era stata caricata 20 minuti prima). Il 02/10 uno spegnimento della scheda lo ha recuperato
  subito. Due delle perdite del 01/10 sono iniziate entro 20 s dallo spegnimento della pompa. Al riavvio
  del 02/10 l'OLED è rimasto nero dopo la schermata di avvio.
- **Power-cycle del sensore**: VDD (GPIO25) **e DQ** a massa insieme per 3 s (`SENSOR_POWER_CYCLE_OFF_MS`),
  prima solo VDD per 500 ms. Con il pull-up di DQ su 3.3 V fisso (o attraverso i diodi di protezione) il
  DS18B20 restava alimentato in parassita dalla linea dati e non si resettava.
- **Tentativi senza limite**: i primi 3 power-cycle ogni 2 min come prima, poi uno ogni 10 min
  (`SENSOR_POWER_CYCLE_RETRY_MS`) anche dopo `riavvio_sospeso`. Fino alla 6.5.0 dopo il terzo il firmware
  smetteva di provare.
- **OLED**: ogni 60 s (`DISPLAY_REINIT_MS`) il firmware rimanda la sequenza di inizializzazione
  dell'SSD1306, così un display resettato da un disturbo di alimentazione torna a mostrare i dati.
- Log: "Nessuna lettura valida da 4294967s" al boot con sensore già guasto ora riporta i secondi dal boot.
- Test: nuovi scenari host `sensore_bloccato` (sensore che si sblocca solo con VDD e DQ a massa: con il
  codice 6.5.0 resta perso con 3 riavvii, con la 6.5.1 torna al primo power-cycle dopo 144 s senza riavvii)
  e `power_cycle_continui` (12 power-cycle nelle 2 h dopo `riavvio_sospeso`, poi ripresa del controllo).
  30 scenari, tutti superati. Compilato per ESP32 (88 % della flash).

## [v6.5.0] - 2026-10-01
### Regolazione stimata con sensore perso
- **Motivo**: il 2026-10-01 il DS18B20 di F1 ha smesso di rispondere due volte (10:31 e 10:46, letture
  `-127`, recuperato ogni volta dal secondo power-cycle). Fino alla 6.4.2 un guasto permanente spegneva i
  relè: con la stanza a 25 °C la birra sale di ~1 °C/h, e nel weekend servono fino a 50 h per arrivare in
  laboratorio.
- **Nuovo modulo `regolazione_stimata`**. Mentre il controllo è in RUNNING con temperatura utilizzabile, il
  firmware impara il carico termico su blocchi di 15 min (duty reale dei relè, temperatura media) e, con
  almeno 8 blocchi contigui (2 h, finestra fino a 4 h), ricava duty medi, temperatura media e pendenza
  (regressione). Se il sensore è perso (nessuna lettura valida da 20 s) **pilota i relè col carico appreso**
  invece di spegnerli: potenza netta `p = u_f·R_f − u_c·R_c + pendenza + (T_media − setpoint)/τ`, duty su
  finestre di 40 min (cicli della pompa simili al controllo normale), massimo 60 %. Il duty di base è quello
  misurato; `R_f` = 10 °C/h, `R_c` = 0,4 °C/h e `τ` = 12,5 h (misurati su F1 il 24/09) pesano solo sulle
  correzioni. Il setpoint segue il profilo, che **continua ad avanzare** durante la regolazione stimata:
  cambiare il setpoint da remoto sposta la temperatura di regime di altrettanto (leva manuale se l'iSpindel
  mostra uno scostamento).
- Durata massima 72 h cumulative (`STIMA_DURATA_MAX_H`), poi relè spenti. La stima e le ore sopravvivono ai
  riavvii automatici per allarme sensore (memoria RTC) e la stima anche a un blackout (NVS, salvata al più
  ogni ora: +24 scritture/giorno). Senza stima (meno di 2 h di controllo dall'avvio) il comportamento è
  quello di prima: relè spenti.
- Invariati: il ciclo power-cycle/allarme/riavvii del sensore (3 riavvii, poi `riavvio_sospeso`), i divieti
  di sicurezza dello stadio uscite (in regolazione stimata la temperatura passata ai divieti è il setpoint),
  `stato_fsm`.
- MQTT: nuovo oggetto `stima` in `esp/state` (buffer da 1024 a 1280 byte) e nuovi `tipo` su `esp/alarm`:
  `regolazione_stimata`, `fine_regolazione_stimata`, `regolazione_stimata_non_disponibile`,
  `regolazione_stimata_scaduta` (`docs/04_CONTRATTO_HASS.md §2.1, §2.3`). OLED: "Sensore off: STIMA".
- Test: nuovi scenari host `stima_calcolo`, `stima_weekend` (6 h di apprendimento, 50 h senza sensore con 3
  riavvii: temperatura 18,93-19,26 °C con setpoint 19, duty appreso 8,0 % contro 8,3 % reale; poi scadenza a
  72 h e ripresa al ritorno del sensore), `stima_blackout_a/b` (stima da NVS dopo un'accensione col sensore
  già guasto); `sensore_guasto` verifica `regolazione_stimata_non_disponibile`. 28 scenari, tutti superati.
  Compilato per ESP32 (88 % della flash).
- Limite: è un controllo ad anello aperto. Se la stanza o la fermentazione cambiano, la temperatura si
  sposta di circa 1,25 °C per ogni punto di duty sbagliato (τ·R_f), con costante di tempo di ore. Per questo
  HA sorveglia con l'iSpindel (sotto).
- Le funzioni di collaudo previste per la "6.5.0" (test del gruppo B nel motore di collaudo) slittano a una
  versione successiva: i test B restano "non applicabili".

### Home Assistant
- `fermentatore_v6_f1_entita.yaml` (e F2 generato): `binary_sensor.f1_v6_regolazione_stimata`,
  `binary_sensor.f1_v6_stima_pronta`, `sensor.f1_v6_stima_duty_freddo/_caldo/_ore/_carico_appreso`;
  automazione `v6_f1_pkg_regolazione_stimata` (email sugli eventi `esp/alarm` della stima); l'email "sensore
  non utilizzabile" dice se i relè sono spenti o in regolazione stimata e riporta la temperatura stimata
  dall'iSpindel.
- Nuovo pacchetto `fermentatore_v6_f1_ispindel.yaml` (solo F1, escluso da `genera_f2.py`): scarto
  iSpindel − DS18B20 appreso con media esponenziale di 3 h mentre il DS18B20 regola, temperatura stimata
  dall'iSpindel, ultimo setpoint noto; con DS18B20 perso o ESP32 offline email se la temperatura stimata si
  scosta oltre 1,5 °C per 60 min (ripetuta ogni 6 h, poi email di rientro con la correzione di setpoint
  suggerita) ed email se l'iSpindel tace per 3 intervalli. Installato nel Lab il 2026-10-01 (backup
  `6a42fea2`), con ricarica e senza riavvio di HA.

## [v6.4.2] - 2026-09-20
### Diagnostica dei reset
- **Nuovo modulo `diag_reset`** e nuovo topic `esp/diag` (retained). I reset `wdt` osservati su F1 dal 19/09
  hanno una causa non chiara: il watchdog del firmware (60 s) riporterebbe `task_wdt`, e tre reset su sei sono
  avvenuti 0,4-3 s dopo lo spegnimento della pompa. Senza USB collegato non c'era modo di distinguere un
  blocco del software da un disturbo elettrico.
- Il firmware lascia in memoria RTC (`RTC_NOINIT`, sopravvive a watchdog, brownout e riavvii software) l'ultimo
  giro del loop (aggiornato ogni 100 ms), l'ultima commutazione di un relè e la sezione del loop in corso, e al
  boot successivo li confronta con il contatore RTC. `esp/diag` pubblica: codice hardware del reset (`hw`,
  `hw_nome`, quello della ROM, es. `RTCWDT_RTC_RESET`), `giro_ms`, `comm_ms` con relè e stato, sezione,
  `rtc_azzerato`, `valido`; e per il boot corrente il massimo intervallo tra due giri del loop, la sezione
  responsabile e i contatori dei giri oltre 1 s e 10 s (risponde anche a «ci sono operazioni oltre 60 s?»).
- Nessun cambiamento nel controllo, nei relè, nel watchdog (resta 60 s) né in `esp/state`: `esp/state` ha già
  un limite di 900 byte nei test, quindi i dati stanno in un topic a parte. Il loop misura ogni sezione con
  `millis()` e scrive due byte in RTC per sezione; `rtc_time_get()` si legge al massimo ogni 100 ms.
- API: `diag_reset_init()` è la prima riga di `setup()` (prima di `uscite_init()`, che altrimenti sovrascriverebbe
  i dati); `uscite_rele.cpp` chiama `diag_reset_commutazione()` prima di scrivere il pin.
- Test: nuovi scenari host `diagnostica_reset` (payload, giro lento, reset a 400 ms dalla commutazione, contatore
  RTC azzerato) e `diagnostica_reset_rele` (integrazione con `uscite_rele`): 24 scenari, tutti superati.
  Compilato per ESP32.
- Entità HA `sensor.f1_v6_reset_*` e `sensor.f1_v6_loop_*` nel pacchetto `fermentatore_v6_f1_entita.yaml`;
  come leggerle: `docs/06_COLLAUDO_F1.md §7`.

## [Solo Home Assistant] - 2026-09-20
### Metriche dell'algoritmo per la didattica (nessuna modifica al firmware)
- Nuovo pacchetto `hass_spec/packages/fermentatore_v6_f1_metriche.yaml` (e copia F2 generata): periodo e
  ampiezza dell'oscillazione della temperatura, accensioni all'ora, tempo medio acceso e duty misurato per
  relè, inversioni caldo↔freddo, scarto medio con segno, su finestra scorrevole di 2 h. In P/PI/PID le
  accensioni sono al massimo 6/h per relè (finestra di 600 s): la dashboard lo spiega e lo mostra.
- Esperimento a gradino esteso (errore a regime con segno, accensioni e duty per relè, oscillazione) e nuova
  **pagella degli algoritmi** che confronta le prove colonna per colonna. Dashboard `f1_v6_laboratorio_dashboard.yaml`
  con le nuove card nelle viste Laboratorio ed Esperimenti.
- Provato offline (template Jinja su una simulazione a 20 L) e installato nel Lab il 2026-09-20 durante il
  collaudo, **senza riavviare HA** (`input_number`, `history_stats`, `statistics` e `template` si ricaricano
  da soli). Dettagli, limiti e precauzioni con un collaudo in corso: `docs/05_HOME_ASSISTANT_F1.md §5 e §7`.

## [v6.4.1] - 2026-09-20
### Diagnostica del sensore
- **Contatori delle letture del DS18B20**, nuovo oggetto `sensore_diag` in `esp/state`. Il firmware scarta
  in silenzio le letture non valide (NaN, -127, 85 °C, fuori −10…40 °C, picchi alternati) e dichiara il
  guasto solo dopo il timeout dell'allarme: una sonda che perde qualche lettura, ma non abbastanza da
  scattare, restava invisibile a HA (verificato il 2026-09-20 sull'analisi del collaudo). Campi, tutti
  `uint32` dal boot (si azzerano a ogni riavvio): `letture` (conversioni completate), `non_valide`
  (= `scollegato` + `poweron_85` + `fuori_scala` + `picco`), `vicino_rele` (sottoinsieme delle scartate
  entro `SENSOR_DIAG_FINESTRA_RELE_MS` = 5 s da una commutazione di un relè), `power_cycle` (cicli di
  alimentazione del sensore eseguiti).
- `-127` non distingue CRC, bus vuoto e sensore assente: la libreria DallasTemperature restituisce lo
  stesso valore. Il contatore `scollegato` li raggruppa.
- API: `sensor_aggiorna()` prende un terzo parametro opzionale (`ts_ultima_commutazione_ms`, default 0)
  e `uscite_ultima_commutazione_ms()` espone l'ultima commutazione. Nessun cambiamento nel controllo.
- `esp/state` non viene più pubblicato se il JSON supera il buffer (come già `esp/controllo`), invece
  di uscire troncato.
- Test: nuovo scenario host `sensore_diagnostica` (cause, "vicino ai relè" contro "lontano", power-cycle,
  totale = somma delle cause) e campi controllati in `stato_json`: 22 scenari, tutti superati. Compilato
  per ESP32 (1.146.937 byte, 87 % della flash, nessun avviso nel codice dello sketch).
- La 6.4.0 non è mai stata flashata su F1: la 6.4.1 è la 6.4.0 (timing del profilo) più questa diagnostica.
  Entità HA `sensor.f1_v6_sensore_*` nel pacchetto `fermentatore_v6_f1_entita.yaml`.
- Il test di collaudo `salute_sensore` resta un segnaposto: la sua logica nel motore va ancora scritta.
  Il motore ora richiede la 6.5.0 per i test del gruppo B (era 6.4.0, incoerente con la documentazione).

## [v6.4.0] - 2026-09-19
### Temporizzazione del profilo
- **Il profilo avanza con un contatore locale, non più con l'orologio di rete.** Fino alla 6.3.0 la
  posizione nella curva era "ora reale − avvio_epoch": senza NTP dopo un riavvio la curva si metteva
  in pausa e poi **saltava avanti tutta insieme** al primo risincronismo — un blackout (che di solito
  spegne anche il router) durante una rampa produceva così un balzo di temperatura invece di
  riprendere gradualmente. Ora `g_profilo.secondi_trascorsi` cresce solo mentre il controllo è
  davvero in corso (RUNNING e sensore utilizzabile): un blackout, un riavvio o un guasto del sensore
  mettono la curva in pausa e la fanno riprendere esattamente da dove era, senza alcun salto.
  Analisi completa in `docs/11_TIMING_PROFILO.md`.
- **Nessuna dipendenza da NTP per eseguire un profilo**: dall'avvio al completamento, comprese le
  rampe, non serve più l'orologio di rete — solo l'uptime dell'ESP32. `avvio_epoch` resta, ma è ora
  solo informativo (mostra "avviato il").
- Persistenza: nuova chiave NVS `progresso_s` (sostituisce `epoch_noto`), scritta ogni 10 minuti
  (`NVS_SAVE_INTERVAL_MS`, invariato) più un checkpoint immediato prima di un riavvio volontario
  (`reboot`), per non ripartire fino a 10 minuti indietro rispetto a dove il profilo era arrivato.
- **Attenzione**: il layout del blob NVS del profilo è cambiato (rimosso `ultimo_epoch_noto`,
  aggiunto `secondi_trascorsi`) — un profilo attivo salvato con firmware ≤ 6.3.0 viene perso al primo
  boot in 6.4.0, come già accade a ogni cambio di questo layout (vedi `profilo.h`).
- Valutato e scartato un limitatore di velocità generale sul setpoint (avrebbe rallentato anche i
  cambi di temperatura intenzionali: il gradino della dashboard didattica, i cambi rapidi di
  setpoint del collaudo automatico) — con il contatore locale il salto che avrebbe dovuto attenuare
  non può più verificarsi.
- Test: 3 nuovi scenari host (`profilo_pausa_blackout_a`/`_b`, due processi come
  `scenario_persistenza_*`, e `profilo_pausa_sensore`), riscritto `profilo_avvio_zero` per verificare
  l'esecuzione completa di un profilo senza mai sincronizzare l'orologio. Tutti i 21 scenari della
  suite host superati, incluse le simulazioni di 48 h (nessuna regressione sul funzionamento
  normale). Aggiornato allo stesso modello anche il simulatore offline del collaudo
  (`tests/collaudo/simula_collaudo.py`).

## [v6.3.0] - 2026-09-18
### Didattica
- **Nuovo topic `esp/controllo`** (retained, pubblicato insieme a `esp/state`): errore (SP−T), termini P,
  I, D, uscita, integrale, duty applicati, stato dei relè, isteresi e guadagni attivi, zona morta,
  uscita minima, finestra PWM. Permette di mostrare dal vivo perché il controllore agisce.
- **Isteresi regolabile da HA**: `control_set` accetta nei `params` `isteresi` (simmetrica, imposta anche
  caldo e freddo), `isteresi_caldo`, `isteresi_freddo`, da 0,1 a 3,0 °C (`invalid_isteresi` fuori limite).
  Salvata in NVS insieme al modo; con lo stesso modo attivo si applica al volo. Prima i cursori di
  isteresi in HA non avevano alcun effetto.

### Controllo PID
- **Calcolo a ogni nuova lettura** del sensore (5 s) invece che ogni secondo sulla stessa misura: dt
  coerente per integrale e derivata.
- **Derivata sulla misura, filtrata** (costante 300 s): nessun "calcio derivativo" ai cambi di setpoint e
  nessun picco dalla quantizzazione del DS18B20. In simulazione con Kd 20000 il termine D resta tra −6 e
  +1 % (prima picchi di circa il 250 % a ogni scalino di 0,0625 °C).
- **Anti-windup**: nessuna integrazione con uscita satura nella direzione dell'errore, termine I limitato a
  ±50 %. Prima l'integrale era limitato a ±300 °C·s indipendentemente da Ki: con i guadagni configurati
  nel Lab (Kp 1, Ki 0,001 in raffreddamento) la simulazione di 48 h teneva l'acqua a 21,5–22,5 °C con
  setpoint 19. Ora arriva al setpoint, oscillando di circa ±1 °C per effetto della taratura.
  Una banda di separazione dell'integrale è stata provata e scartata per lo stesso motivo.
- Guadagni di default invariati: sono i migliori tra quelli provati sul modello da 20 L.

| Simulazione 48 h, setpoint 19 °C | Raffreddamento (ambiente 24 °C) | Riscaldamento (ambiente 14 °C) |
|---|---|---|
| On/off con isteresi 0,5 | 19,03–19,47 °C, 1 commutazione/h | — |
| PI/PID guadagni di default | 18,84–19,09 °C | 18,83–19,26 °C |
| P, Kp 30 | 19,28–19,32 °C (errore a regime) | 18,06–18,41 °C |
| PI guadagni del Lab | 17,97–19,86 °C | 18,33–19,04 °C, 30 commutazioni/h |

### Test
- 18 scenari host. Nuovi: `controllo_didattico` (coerenza di errore, P, I, D e uscita, derivata senza
  picchi, aggiornamento al volo, limiti), `isteresi_regolabile` (soglia effettiva), `sim_pi_lab_48h`,
  `sim_custom_48h` per tarare i preset (`SIM_MODO`, `SIM_PARAMS`, `SIM_AMBIENTE`, `SIM_BANDA`).
- La persistenza verifica anche isteresi e guadagni dopo il riavvio.
- Gli spegnimenti di sicurezza non sono più contati come violazioni del tempo minimo ON.

### Home Assistant — dashboard "F1 Laboratorio"
- **Pacchetto `fermentatore_v6_f1_laboratorio.yaml`** e dashboard `hass_spec/dashboards/f1_v6_laboratorio_dashboard.yaml`
  (viste Panoramica, Laboratorio, Esperimenti, Fermentazione, Diagnostica).
- **Modalità Produzione / Didattica** (`input_select.f1_v6_modalita`): in Produzione gli script che cambiano il
  controllo o avviano esperimenti rifiutano il comando. "Torna in Produzione" applica on/off con isteresi 0,5 °C.
- **Preset** tarati con le simulazioni: on/off 0,5 (produzione), on/off puro, on/off 1,0, P Kp 30, PI consigliato
  (Kp 30, Ki 0,002), PID (Kd 5000), PI mal tarato (Kp 1, Ki 0,001). "Applica": stesso modo al volo; modo diverso
  in marcia stop → control_set → start, con attesa degli ack.
- **Spiegazione dal vivo** di On/Off, P, PI, PID con i valori correnti; grafici di temperatura, soglie SP±h, relè
  e termini P, I, D, uscita; indicatore dei parametri impostati in HA ma non ancora applicati.
- **Esperimento a gradino** con metriche: sovraelongazione, tempo di assestamento (±0,2 °C per 30 min), errore
  medio assoluto, commutazioni; risultato nel registro attività.
- In Didattica l'allarme di scostamento non manda email (resta la notifica in HA); gli allarmi tecnici restano.
- Nel Lab: isteresi aggiunta ai `control_set` degli script di avvio, rimosso il watchdog del modo e il reinvio
  del `control_set` alla riconnessione (dalla 6.2.0 il firmware ripristina modo e parametri da NVS), tolti gli
  `initial` dei parametri di controllo, che al riavvio di HA li riportavano ai valori di fabbrica.

### Home Assistant — collaudo automatico di F1
- **Modalità Collaudo** e pacchetto `fermentatore_v6_f1_collaudo.yaml`: test reali sul fermentatore senza
  birra, con blocco degli script didattici e allarmi via email come in produzione.
- **App AppDaemon `collaudo_f1`** (motore + adattatore HA): macchine a stati riprendibili, un test alla
  volta, guidate da tick di 30 s, ack del firmware e chiamate a `notify`; metriche calcolate dallo storico
  di Home Assistant. Il motore gira anche in un simulatore offline (`tests/collaudo/simula_collaudo.py`).
- **Dashboard "F1 Collaudo"**, generata da `hass_spec/dashboards/genera_collaudo.py`.
- **Test della Fase A** (firmware 6.3.0): protezione dei comandi, tenuta lager 10 °C, riavvio durante la
  regolazione, commutazioni della pompa, sosta del diacetile 17 °C, rampa 17→10 °C, on/off senza isteresi,
  P/PI/PID in freddo con i guadagni di default, cold crash 2 °C, allarme se il freddo si ferma. Segnaposto
  per la Fase B (firmware 6.4.0: sensore, relè, WiFi, watchdog) e un test manuale di mancanza di corrente.
- **Riavvii spontanei dell'ESP32** tracciati per l'intera sessione di collaudo, con l'ultima commutazione
  di relè o cambio di setpoint che ha preceduto ciascuno (dallo storico HA); nuovo sensore HA
  `sensor.f1_v6_riavvii_spontanei` con allarme via email anche fuori dal collaudo.
- **Guasto trovato**: F1 si resetta da solo (causa `poweron`, offline <15 s) 0,7–4,7 s dopo lo spegnimento
  del relè della pompa (220 V, 10-15 W). Vedi `docs/06_COLLAUDO_F1.md` §7 e `docs/10_PUNTI_APERTI.md`.
- Documentazione: `docs/05_HOME_ASSISTANT_F1.md` (configurazione, allarmi, dashboard, procedure),
  `docs/06_COLLAUDO_F1.md` (architettura e catalogo dei test), `docs/10_PUNTI_APERTI.md` (lavoro futuro).
  (Rinumerati il 2026-09-19, vedi sotto.)

### Riordino della documentazione e del repository (2026-09-19)
- **`obsolete/`**: spostate qui le versioni congelate `fermentatore_v2` … `fermentatore_v5_autotuning`
  (root del repository). Nessuna modifica al contenuto, solo spostamento. `fermentatore_v6` resta
  l'unica linea di sviluppo attiva.
- **Rimosso** `f1_mqtt_log_2026-05-19.json` dalla root: un dump di debug MQTT senza alcun riferimento
  nel resto del repository.
- **`AGENTS.md` → `CLAUDE.md`**, riscritto per riflettere lo stato attuale (F1/F2, `fermentatore_v6`
  attivo, `obsolete/` per le versioni precedenti, moduli reali del firmware, collaudo automatico,
  nota di sicurezza sulle due istanze Home Assistant).
- **Documentazione V6 rinumerata e consolidata** in `fermentatore_v6/docs/`, da 00 a 10 (indice in
  `00_INDICE.md`). Uniti in nuovi documenti i file che si sovrapponevano:
  - `01_PROGETTO_INIZIALE_E_BACKLOG.md` = `V6_MASTER_PLAN.md` + `V6_EXECUTION_BACKLOG.md` +
    `V6_CHECKLIST_COMPATIBILITA_V4_V5.md` + le parti ancora rilevanti di `hass_spec/F1_V6_FIRMWARE_BRIEF.md`
    (storico, con una sezione nuova sugli scostamenti tra il piano originale e l'implementazione reale:
    pin GPIO, struttura del codice, algoritmo PID, autotuning).
  - `02_HARDWARE_E_TARGET.md` = `PIN_MAP_DEFINITIVA_V6.md` + `TARGET_CONFIGURATOR_V6.md`.
  - `04_CONTRATTO_HASS.md` = `hass_spec/HASS_V6_SPEC.md`, spostato (solo i due `.md`, non i pacchetti/
    dashboard operativi, restano in `hass_spec/`).
  - Rinominati senza fondere: `03_PROTOCOLLO_MQTT.md`, `05_HOME_ASSISTANT_F1.md`,
    `06_COLLAUDO_F1.md`, `07_TEST_PLAN.md`, `08_RUNBOOK_OPERATIVO.md`, `09_TOOLING.md`,
    `10_PUNTI_APERTI.md`.
- **`10_PUNTI_APERTI.md`**: aggiunta la guida di onboarding di F2 da scrivere (F1 e F2 saranno due
  fermentatori a controllo separato gestiti entrambi da Home Assistant; per F2 la scheda elettronica
  va progettata e costruita da zero, non solo flashata), più alcune voci di igiene del repository
  emerse nel riordino (manifest di build disallineato dal firmware reale, riferimenti a file mai
  esistiti come `HASS_SPEC_v5_v5A.txt`, `HASS_CONFIGURATORE_V6.md`, `HASS_MONITORAGGIO_V6.md`).
- Corretti nei documenti aggiornati i riferimenti incrociati e i link interni.

## [v6.2.0] - 2026-09-17
### Sicurezza temperatura
- **Relè pilotati con sensore guasto** (`sensor_watchdog.cpp`, `fermentatore_v6.ino`): la lettura
  restituiva "valida" anche nel ciclo che avviava soltanto la conversione, con l'ultima temperatura
  buona, e il controllo usava quel flag. Con DS18B20 scollegato il riscaldatore restava pilotato su
  un valore congelato (misurato su 6.1.19 in simulazione host: acceso 62 s su 300 s) e il ciclo
  ripartiva a ogni riavvio. Ora `sensor_aggiorna()` distingue nessuna novità, lettura valida e
  lettura non valida; il controllo usa la temperatura solo con almeno 3 letture valide consecutive
  e ultima lettura più recente di 20 s. Oltre, relè forzati OFF. Un singolo glitch non interrompe
  il controllo. Al boot nessun controllo sulla temperatura fittizia di 20 °C.
- **Allarme sensore senza STOP**: l'allarme non chiama più `stop_run()`; i relè sono già spenti dal
  gating e il controllo riprende da solo quando il sensore torna valido. Prima, un recupero entro
  i 30 s precedenti al riavvio lasciava il fermentatore fermo fino a un comando manuale.
- **Power-cycle sensore**: non azzera più il timer di errore (l'allarme scattava dopo ~11 min invece
  di 5). Riavvii per allarme sensore limitati a 3 consecutivi (contatore in RTC memory), poi resta in
  allarme pubblicando `riavvio_sospeso`.
- **Task watchdog sul loop** (60 s, panic + reboot, relè OFF al boot). Prima nessun watchdog: un
  blocco del loop lasciava i relè nell'ultimo stato a tempo indefinito.
- **Limiti di sicurezza indipendenti dal modo** (nuovo `uscite_rele.cpp`): riscaldatore vietato sopra
  36 °C o oltre setpoint + 1 °C, refrigeratore sotto -1 °C o oltre setpoint - 1 °C. Setpoint limitato a
  0..35 °C per tutti i modi (prima solo on/off). Duty non numerico → uscite OFF.
- **Stadio uscite unico**: tempo minimo ON 30 s, OFF 60 s, attesa 60 s tra caldo e freddo, impulso
  minimo 30 s, finestra duty PID 10 min (era 10 s senza alcun tempo minimo). Interlock caldo/freddo.
- **Isteresi con memoria** (`controller_onoff.cpp`): l'uscita si accende a SP ∓ isteresi e si spegne al
  setpoint. Prima si spegneva appena rientrata nella banda: in simulazione 48 h 54 commutazioni/ora
  sulla pompa, ora 1/ora. Nei modi PI/PID 12/ora (erano 60/ora).

### Profilo e comandi
- **`avvio_epoch = 0`**: ora significa "parti adesso" (assegnato alla prima ora valida se NTP non è
  sincronizzato). Prima il profilo restava per sempre al primo step (es. cold crash mai eseguito con
  gli script HA `invia_profilo` e `avvia_costante`).
- **Senza NTP il profilo avanza**: ultimo epoch salvato + uptime, invece di restare congelato.
- **Comandi retained/duplicati/vecchi**: ignorati i comandi arrivati entro 10 s dalla SUBSCRIBE senza
  `ts` recente (`possible_retained_ignored`), quelli con `ts` più vecchio di 10 min (`stale_command`)
  e i duplicati con stesso `request_id`+`ts` (`duplicate_ignored`). STOP sempre eseguito. Un
  `profile_set` retained riavviava a ogni riconnessione un fermentatore messo in STOP.
- **`profile_set` validato**: rifiutati step fuori 0..35 °C, senza temperatura o durata, durate non
  valide, più di 20 step (prima troncati in silenzio), `avvio_epoch` incoerente. Nomi sanificati
  (virgolette rendevano invalido il JSON di stato). Nuovo campo opzionale `avvia` (default true).
- **Profili lunghi**: `parse_command` usa un filtro ArduinoJson; il documento da 1 KB rifiutava come
  `invalid_json` i profili oltre ~10 step.
- **Modo di controllo persistente in NVS** (`ctrl_cfg`): dopo un reboot l'ESP torna con lo stesso
  algoritmo e parametri, non più al default. `control_set` con lo stesso modo durante RUNNING
  aggiorna i parametri e risponde ok (`mode_already_active`).
- Guadagni PID validati (finiti, >= 0). Errori di parsing con `request_id` e nome comando.
- **Autotune disabilitato** (`AUTOTUNE_ABILITATO 0`): l'implementazione non pilota i relè e ricava i
  guadagni da una formula fissa con quality score sempre alto. Risposta `autotune_disabled`.

### Monitoraggio remoto (breaking per la configurazione HA)
- **Disponibilità**: LWT spostato da `esp/alarm` (senza retain) a `esp/availability` retained
  `online`/`offline`, con `online` pubblicato a ogni connessione e `offline` prima dei riavvii
  volontari. HA deve usare questo topic (pacchetti `hass_spec/packages/`).
- **Stato ogni 30 s** (era 60 s) e subito al cambio relè o stato FSM; `riscaldatore`/`refrigeratore`
  riportano lo stato fisico dei relè. Nuovi campi: `sensore_ok`, `temp_eta_s`, `ntp_sync`, `uptime_s`,
  `reset_reason`, `rssi`, `heap_libero`, `commutazioni`. `temp_reale` è `null` prima della prima lettura.
- **Ack compatibili con le automazioni HA esistenti**: aggiunti `cmd`, `profilo` ed `event` (per gli
  eventi). Le automazioni "ACK avvio profilo" e "Profilo completato" cercavano questi campi e non
  scattavano mai.

### Connettività
- Timeout MQTT: handshake TLS 10 s (default core 120 s), socket 5 s, keepalive 30 s.
- Riconnessione WiFi non bloccante (prima attesa di 8 s nel loop e pausa di 20 min dopo 3 tentativi).
- Ripiego automatico su `IoT` se l'hotspot OTA non è più disponibile (prima restava offline fino al
  riavvio). OTA avviato una sola volta, watchdog alimentato durante l'upload.
- Scritture NVS solo su cambio stato/step o ogni 10 min; epoch in chiave dedicata invece di riscrivere
  il blob profilo da 1.3 KB ogni 60 s.
- Display: temperatura "--.-" con sensore non utilizzabile; lo stato di controllo ha priorità su WiFi/MQTT.

### Test
- Nuovi test host (`tests/host/run_tests.ps1`): compilano i sorgenti reali con mock Arduino/ESP32 (MSVC)
  e verificano 15 scenari, tra cui sensore guasto, recupero, comandi retained, profili, persistenza e
  simulazioni termiche di 48 h per on/off, PI e PID.
- `tools/v6/test_e2e.py` allineato al contratto 6.2.0 (topic `esp/state`, campi, `ts`) e protetto da
  `--banco-prova` perché invia comandi reali.

## [v6.1.19] - 2026-06-08
### Fixed
- **Fix critico relè ACTIVE LOW con duty PID** (`fermentatore_v6.ino`): la gestione PWM hardware
  introdotta in v6.1.17/v6.1.18 manteneva il pin in livello LOW quando duty=0, che su scheda
  relè ACTIVE LOW equivale a relè chiuso (raffreddamento continuo) anche con FSM `STOPPED` e
  `refrigeratore:false` nel payload MQTT. Sostituita la pilotazione LEDC con logica
  **time-proportional** ACTIVE LOW: ON/OFF usa boolean relè, PID usa finestra `PID_DUTY_WINDOW_MS`
  e interlock caldo/freddo mutuamente esclusivo. `stop_run()` ora forza immediatamente lo stato
  fisico dei pin OFF.
- **Allineamento stato MQTT post-comando**: dopo `start`, `stop` e `control_set` viene pubblicato
  subito `esp/state` per evitare percezione di stato stale lato HA tra due heartbeat da 60 s.

## [v6.1.18] - 2026-06-08
### Fixed
- **Compatibilità core ESP32 3.x in PWM** (`pwm.cpp`): il modulo duty-cycle PID usava
  API LEDC legacy `ledcSetup()` + `ledcAttachPin()` non disponibili nel core 3.3.7,
  causando errore di compilazione. Aggiornato a API pin-based `ledcAttach(pin, freq, res)`
  e `ledcWrite(pin, duty)`. Corretto anche il mapping duty usando la risoluzione configurata.

## [v6.1.17] - 2026-06-08
### Fixed
- **PID PWM duty cycle not applied** (`fermentatore_v6.ino`, `apply_outputs()`): Controller PID
  was correctly calculating `heat_duty_pct` and `cool_duty_pct` in `set_output_from_pid()`, but
  `apply_outputs()` was ignoring them — using only `heat_on`/`cool_on` boolean values. Result:
  P/PI/PD/PID modes were truncated to ON/OFF instead of smooth PWM modulation. Created new PWM
  module (`pwm.h`/`pwm.cpp`) using ESP32 LEDC hardware (1 kHz, 8-bit resolution) on GPIO16
  (heat) and GPIO17 (cool). Modified `apply_outputs()` to apply duty cycle via PWM, enabling
  proper closed-loop control for PID family modes.

## [v6.1.16] - 2026-05-29
### Fixed
- **Stack overflow nel callback MQTT** (`connettivita.cpp`, `_on_messaggio`): `char buf[MQTT_BUFFER_SIZE]`
  (4096 B) era allocato sullo stack del loopTask (8192 B totali). Combinato con
  `ProfiloFermentazione p` (1344 B) in `apply_profile_request` — ancora in scope durante il
  callback — il picco di utilizzo stack superava la soglia critica. Risultato: crash loop
  deterministico ogni volta che il broker consegnava il messaggio retain `profile_set` dopo la
  riconnessione MQTT; i relè non si attivavano mai nonostante la temperatura lontana dal setpoint.
  Corretti entrambi i siti con `static`: la variabile sopravvive tra chiamate ma non occupa stack.
  Il callback PubSubClient non è rientrante, quindi `static` è sicuro in entrambi i casi.

## [v6.1.15] - 2026-05-27
### Fixed
- **§8 Boot autonomy**: NVS recovery spostato PRIMA della scansione WiFi;
  il controller si avvia in RUNNING/HOLDING senza attendere la rete. `caricaStatoFsm`
  distingue resume attivo (RUNNING/HOLDING) da stato STOPPED salvato deliberatamente.
- **§8 NVS FSM persist**: aggiunto salvataggio periodico (60 s, insieme all’heartbeat)
  di `last_fsm`, `last_step_idx`, `ore_save` via `salvaStatoFsm()`.
  Aggiunte funzioni `salvaStatoFsm`, `caricaStatoFsm`, `cancellaStatoFsm` in `profilo.h/.cpp`.
- **Regola 6 — payload esp/state**: sostituita firma `mqtt_pubblica_stato(controller, config, temp, sp)`
  con `mqtt_pubblica_stato(const StatoHeartbeat&)`; il payload ora include tutti i campi spec:
  `stato_fsm`, `control_mode`, `step_nome`, `step_index`, `ore_step`, `ore_totali`,
  `riscaldatore`, `refrigeratore`, `allarme`, `profilo`, `pid_output`,
  `duty_riscaldatore`, `duty_refrigeratore`, `firmware`, `hardware_target`, `fermenter_id`.
  Aggiunto `struct StatoHeartbeat` in `connettivita.h`; aggiunto helper `build_heartbeat()` in `.ino`.
- **Regola 6 — payload esp/alarm**: `mqtt_pubblica_allarme` ora accetta `(tipo, durata_minuti)`;
  payload `{"tipo":...,"durata_minuti":...,"uptime_s":...}` conforme a HASS_V6_SPEC.
- **Regola 6 — LWT**: payload LWT aggiornato a `{"tipo":"esp32_offline","durata_minuti":0,"uptime_s":0}`.
- **Regola 6 — payload esp/ack**: `reason_code` ora `null` quando ok/vuoto (era sempre stringa quotata);
  aggiunto campo `uptime_s`.
- **Regola 6 — payload esp/autotune_state**: campi rinominati conforme spec:
  `state`, `phase`, `phase_index`, `phase_total`, `cycles_done`, `progress`, `temp`, `ts`.
- **Regola 6 — control_mode case**: `mqtt_pubblica_autotune_state` e `parse_control_mode` ora
  usano maiuscolo (`P`, `PI`, `PD`, `PID`) come da select HA; parse accetta anche minuscolo.
- **START gate**: il comando START restituisce `{"status":"error","reason":"no_profile"}` se
  nessun profilo è stato caricato, invece di chiamare `start_run()` incondizionatamente.
- **profilo_completato**: evento spostato da `mqtt_pubblica_allarme` a `mqtt_pubblica_ack`
  (`"event"`/`"profilo_completato"`); non genera più un allarme.
- **bad_serial_command**: rimosso `mqtt_pubblica_allarme`; sostituito con `LOG_W`.
- **Estado pubblicato ogni 1 s rimosso**: le due chiamate `mqtt_pubblica_stato` alla fine del loop
  per ciclo di controllo sono state rimosse; rimane solo l’heartbeat ogni 60 s.

## [v6.1.14] - 2026-05-26
### Fixed
- Display OLED completamente implementato (era uno stub che stampava solo su
  Serial). Portata la logica completa da v5: inizializzazione U8g2 SSD1306 I2C,
  schermata BOOT (WiFi/MQTT), schermata OPERATIVA (RUNNING/STANDBY/HOLDING con
  temperatura, setpoint, barra avanzamento, step corrente, icone relè), schermata
  ALLARME con countdown. Aggiunto stato "AUTOTUNE" per la schermata autotuning.
- Aggiunto `handle_display()` nel loop con aggiornamento a `DISPLAY_UPDATE_MS` (500 ms).
- Aggiunte costanti mancanti `OLED_I2C_ADDR` e `DISPLAY_UPDATE_MS` in `config.h`.

## [v6.1.13] - 2026-05-26
### Changed
- Boot WiFi: implementata logica scan-first (portata da v4). Al boot l'ESP
  scansiona le reti disponibili; se `HOTSPOT_SSID` ("HotIoT") è visibile si
  connette direttamente a quella rete (OTA prioritario), altrimenti si connette
  a `WIFI_SSID` ("IoT"). Eliminato il precedente meccanismo fallback (prova IoT
  → se fallisce prova HotIoT).
- Reconnect loop: usa ora `g_via_hotspot` per riconnettersi alla stessa rete
  scelta al boot (hotspot o rete standard).
- Aggiunto flag globale `g_via_hotspot` in `fermentatore_v6.ino`.

## [v6.1.12] - 2026-05-26
### Fixed
- `configure_target.ps1`: percorso `configPath` corretto da
  `fermentatore_v6/config.h` a `fermentatore_v6/fermentatore_v6/config.h`
  (il file risiede nella sottocartella sketch, non nella root del progetto).

## [v6.1.11] - 2026-05-26
### Fixed
- `OTA_HOSTNAME` cambiato da stringa hardcoded `"fermentatore-v6"` a
  `"fermentatore-v6-" DEVICE_FERMENTER_ID` in `config.h`: F1 e F2 avranno
  ora hostname mDNS distinti (`fermentatore-v6-f1` / `fermentatore-v6-f2`)
  ed evitano collisioni quando entrambi sono sulla stessa rete.
- `configure_target.ps1`: aggiunta riga `ota_hostname` nell'output di
  riepilogo post-configurazione.

## [v6.1.10] - 2026-05-26
### Changed
- Creata cartella `fermentatore_v6/hass_spec/` come home unica per la
  documentazione HASS; spostati `HASS_V6_SPEC.md` (da docs/) e
  `HASS_SPEC_v5_v5A.txt` (da root).
- Rimossi file obsoleti integrati in `HASS_V6_SPEC.md`:
  `HASS_CONFIGURATORE_V6.md`, `HASS_MONITORAGGIO_V6.md`, `HASS_BLUEPRINTS_V6.md`.
- Aggiornati riferimenti in `HASS_V6_SPEC.md`, `README.md`, `V6_MASTER_PLAN.md`.

## [v6.1.9] - 2026-05-26
### Added
- Aggiunto `fermentatore_v6/docs/HASS_V6_SPEC.md`: direttive AI complete per
  generare la configurazione Home Assistant V6 (entità MQTT, dashboard Lovelace,
  automazioni, script) per F1 e F2, con delta rispetto a V5 e topic contract
  da config.h.

## [v6.1.8] - 2026-05-25
### Changed
- Aggiunta stampa del MAC address durante la connessione WiFi: tentativo iniziale, fallback hotspot e tentativi di reconnessione (`LOG_I/W` in `fermentatore_v6.ino`).

## [v6.1.7] - 2026-05-25
### Changed
- Abilitato log runtime VERBOSE per i tag WiFi (`wifi`, `net80211`, `wpa`) in `setup()` per aumentare la visibilita' dei messaggi nel Serial Monitor.
- Aggiunto promemoria a boot: per vedere tutti i log WiFi serve `Tools > Core Debug Level = Verbose` in Arduino IDE.

## [v6.1.6] - 2026-05-25
### Fixed
- Ridotto warning seriale `E (...) wifi:sta is connecting, cannot set config` evitando chiamate ripetute a `WiFi.begin()` durante lo stato di connessione (`WL_IDLE_STATUS`) nel loop di reconnessione in `fermentatore_v6.ino`.

## [v6.1.5] - 2026-05-25
### Fixed
- Risolto conflitto di compilazione con `Arduino.h` (`#define PI ...`) rinominando i valori enum `ControlMode` da `PI/PD/PID` a `PI_CTRL/PD_CTRL/PID_CTRL` in `controller.h` e aggiornando i riferimenti in `controller.cpp` e `command_processor.cpp` senza modificare i payload MQTT (`"pi"`, `"pd"`, `"pid"`).

## [v6.1.4] - 2026-05-25
### Added (v6.1.4) — M5: autotune safety bounds + E2E test stub
- **`autotune.cpp`** `autotune_tick()`: abort immediato se
  `temp_c < AUTOTUNE_TEMP_MIN_C || temp_c > AUTOTUNE_TEMP_MAX_C`
  (guarda dagli scenari in cui il sensore legge valori fisicamente assurdi
  durante la procedura di identificazione).
- **`config.h`**: aggiunti `AUTOTUNE_TEMP_MIN_C -5.0f` e
  `AUTOTUNE_TEMP_MAX_C 45.0f`. Versione → `6.1.4`.
- **`fermentatore_v6.ino`**: `LOG_I/W` per autotune start/stop/apply e
  per le transizioni COMPLETED/ABORTED nei due path del loop.
- **`tools/v6/test_e2e.py`** (nuovo): script Python paho-mqtt che valida
  il flusso MQTT E2E. 5 test case: stop baseline, stato heartbeat,
  start, profile_set, stop post-profilo. Credenziali da env/`.env`.
  Uso: `python test_e2e.py --broker <host>` (TLS abilitato di default).

## [v6.1.3] - 2026-05-25
### Added (v6.1.3) — M4: modulo log dual-channel + diagnostica heartbeat
- **`log.h` / `log.cpp`** (nuovi): logging compile-time dual-channel Serial+MQTT.
  Macro `LOG_E/W/I/D(modulo, fmt, ...)`. Overhead zero quando
  `LOG_SERIAL_LEVEL == LOG_NONE && LOG_MQTT_LEVEL == LOG_NONE`.
  JSON escape minimale per il campo `msg`. `log_init(mqtt_pubblica_log)` chiamata
  in `setup()` dopo `connettivita_mqtt_init()`.
- **`config.h`**: aggiunte costanti `LOG_NONE/ERROR/WARN/INFO/DEBUG`,
  `LOG_SERIAL_LEVEL LOG_DEBUG`, `LOG_MQTT_LEVEL LOG_INFO`. Versione → `6.1.3`.
- **`fermentatore_v6.ino`**: `LOG_I` su boot, WiFi connect/riconnessione, MQTT
  connesso, profilo applicato, NVS resume; `LOG_W` su WiFi fallito e sensore
  instabile; `LOG_E` su allarme DS18B20; `LOG_D` heartbeat con T/SP/heap/stato/profilo.

## [v6.1.2] - 2026-05-25
### Added (v6.1.2) — M3: filtro oscillazione incoerente DS18B20
- **`sensor_watchdog.cpp`**: aggiunto storico 3 campioni (`g_raw_prev1/2`),
  `push_raw_temp()` e `oscillazione_incoerente()` (spike alternati ≥ `SENSOR_SPIKE_DELTA_C`).
  Il branch `oscillazione_incoerente` in `sensor_leggi()` conta oscillazioni
  consecutive; attiva l'allarme dopo `SENSOR_OSC_CONSECUTIVE_LIMIT` hit e
  scaduto il timeout. `sensor_init()` azzera lo storico (incluso nel power-cycle).
- **`config.h`**: aggiunti `SENSOR_SPIKE_DELTA_C 7.0f` e
  `SENSOR_OSC_CONSECUTIVE_LIMIT 3`. Versione → `6.1.2`.

## [v6.1.1] - 2026-05-25

### Added (v6.1.1) — M2: profile_set reale + NVS persistence

- **`profilo.h` / `profilo.cpp`** (nuovi): `ProfiloStep`, `ProfiloFermentazione`,
  `calcolaSetpoint()`, `calcolaDurataTotaleOre()`, `parsaProfiloJSON()`,
  `salvaProfilo()` / `caricaProfilo()` / `cancellaProfilo()`, `salvaEpochNoto()`.
  Adattato da v5, rimossi parametri PID/PWM legacy (gestiti dal modulo `controller`).
- **`fermentatore_v6.ino`**:
  - `ProfiloFsmState` enum (`STANDBY / RUNNING / HOLDING`) e `g_profilo` /
    `g_profilo_fsm` globals.
  - `secondi_trascorsi_profilo()`: calcolo elapsed con fallback `ultimo_epoch_noto`
    quando NTP non ancora sincronizzato.
  - `apply_profile_request()`: parse JSON → salva NVS → aggiorna `g_setpoint` →
    imposta FSM → `start_run()`.
  - Loop: aggiornamento `g_setpoint` da `calcolaSetpoint()` ogni control tick;
    transizione automatica `RUNNING → HOLDING` con publish `profilo_completato`.
  - `setup()`: `caricaProfilo()` da NVS → resume automatico (RUNNING o HOLDING)
    dopo reset/reboot.
  - Heartbeat: `salvaEpochNoto()` ogni `STATUS_INTERVAL_MS` per aggiornare il
    fallback offline.
  - Comandi `START` / `STOP` aggiornati: `START` riprende profilo esistente con
    ricalcolo setpoint; `STOP` preserva `g_profilo` per resume futuro.
- **`config.h`**: aggiunti `MAX_STEPS 20` e `DEFAULT_STANDBY_TEMP_C 20.0`.
  Versione aggiornata a `6.1.1`.



### Added (v6.1.0) — M1: trasporto WiFi/MQTT reale

- **WiFi + MQTT reali** in `connettivita.cpp`: sostituito il serial stub con
  `WiFiClientSecure` + `PubSubClient` (HiveMQ Cloud TLS porta 8883).
  - `connettivita_mqtt_init(MqttCommandHandlerFn)`: registra la callback comandi.
  - `connettivita_mqtt_connetti()`: connect con LWT su `TOPIC_ALARM`, subscribe
    wildcard `TOPIC_PREFIX_HA/#` (QoS 1).
  - `connettivita_mqtt_loop()` / `connettivita_mqtt_connesso()`.
  - `mqtt_pubblica_log()`.
  - Tutte le `mqtt_pubblica_*` ora pubblicano su MQTT reale + log seriale fallback.
- **`connettivita.h`**: aggiunta `typedef MqttCommandHandlerFn` e le quattro funzioni
  lifecycle; aggiunta `mqtt_pubblica_log`.
- **`wifi_connection.ino`** (nuovo): selezione password WiFi per MAC address via
  `WIFI_MAC_PASSWORD_MAP` da `secrets.h`.
- **`secrets.h.template`** (nuovo): template credenziali per v6.
- **`config.h`**: aggiunte sezioni WiFi, MQTT, NTP, NVS, timing, topic V6 completi
  (`TOPIC_CMD_*`, `TOPIC_STATE/ACK/ALARM/AUTOTUNE_STATE/AUTOTUNE_RESULT/LOG`).
  Include `secrets.h`. Versione aggiornata a `6.1.0`.
- **`fermentatore_v6.ino`**: WiFi lifecycle integrato:
  - `setup_network_services()`: configura NTP, OTA hostname/password, avvia
    `ArduinoOTA.begin()` con stop-output safe nel callback `onStart`.
  - `handle_network(now)`: gestione non-bloccante di WiFi reconnect (burst 3 ×
    `HOTSPOT_RECONNECT_INTERVAL_MS`, poi pausa `WIFI_RECONNECT_CYCLE_MS`), MQTT
    reconnect ogni `MQTT_CONNECT_TIMEOUT_MS`, heartbeat `STATUS_INTERVAL_MS`,
    `ArduinoOTA.handle()`.
  - `setup()`: connect WiFi bloccante (max `WIFI_TIMEOUT_MS`), fallback hotspot,
    `setup_network_services()`, `connettivita_mqtt_connetti()`.
  - `loop()`: chiama `handle_network(now)` prima del sensore/controllo.



### Added (v6.0.4)

- Added DS18B20 watchdog module with GPIO-powered recovery:
  - new files `sensor_watchdog.h/.cpp`
  - asynchronous read flow with validity checks
  - `sensor_power_cycle_init()` and `sensor_power_cycle()` via `PIN_SENSOR_PWR`
- Added `PIN_SENSOR_PWR` auto-config support in `config.h` and target configurator.

### Changed (v6.0.4)

- Updated `fermentatore_v6.ino` runtime to apply V5-like sensor recovery strategy:
  - sensor stability streak and fallback transition handling
  - proactive sensor power-cycle after configurable instability timeout
  - alarm path with controlled reboot delay when sensor remains offline
  - fail-safe output stop while sensor is not reliable
- Updated tooling/docs connected to target selection and wiring:
  - `tools/v6/configure_target.ps1` now configures `PIN_SENSOR_PWR` and supports `-PinSensorPwr`
  - `docs/TARGET_CONFIGURATOR_V6.md` updated with new parameter/defaults
  - `docs/PIN_MAP_DEFINITIVA_V6.md` updated with DS18B20 VDD switch pin and wiring notes
  - `v6_manifest.json` firmware version aligned to `6.0.4`

## [v6.0.3] - 2026-05-25

### Added (v6.0.3)

- Added automatic target configurator script `tools/v6/configure_target.ps1`:
  - supports `wroom-f1`, `wroom-f2`, `s3-f1`, `s3-f2`
  - updates `DEVICE_FERMENTER_ID`, `DEVICE_HARDWARE_TARGET`, and board pin map in `config.h`
  - supports explicit pin override parameters for board revisions
- Added operational guide `docs/TARGET_CONFIGURATOR_V6.md` with usage examples and verification workflow.

### Changed (v6.0.3)

- Extended `config.h` with complete board pin macros (`PIN_DS18B20`, relay pins, OLED pins) managed by auto-config.

## [v6.0.2] - 2026-05-25

### Added (v6.0.2)

- Implemented full M2 controller stack in `fermentatore_v6`:
  - added `on_off_hyst_asym`
  - added P, PI, PD, PID runtime control modes
  - added runtime state model (`stopped`, `running`, `autotuning`)
  - enforced mode change only in STOPPED state via `controller_set_mode()`
- Added M3 command processing layer:
  - `command_processor.h/.cpp` with JSON parsing and strict schema/request validation
  - command support for `start`, `stop`, `control_set`, `profile_set`, `reboot`, `autotune_start/stop/apply`
- Added M6 integrated autotuning module in `autotune.h/.cpp` with:
  - start/tick/stop lifecycle
  - quality score and generated gains
  - guarded apply flow with minimum quality threshold
- Extended telemetry publishing in `connettivita.cpp`:
  - state payload with runtime/mode/duty metadata
  - ack payload with `request_id`, `status`, `reason_code`
  - autotune state/result payloads

### Changed (v6.0.2)

- Updated `fermentatore_v6.ino` to orchestrate:
  - stop -> reset/apply -> start flow
  - serial command ingestion for contract testing (`topic|json`)
  - autotuning lifecycle and telemetry publication
- Updated `config.h` with V6 contract defaults, PID defaults, device identity, and autotune guardrails.

## [v6.0.1] - 2026-05-25

### Changed (v6.0.1)

- Reorganized all V6 operational artifacts under `fermentatore_v6`:
  - moved manifest to `fermentatore_v6/v6_manifest.json`
  - moved build scripts to `fermentatore_v6/tools/v6/`
  - moved generated metadata to `fermentatore_v6/generated/`
  - moved planning/HASS docs to `fermentatore_v6/docs/`
- Updated hooks/tooling paths to match the new V6 layout:
  - `.githooks/pre-commit` now verifies via `fermentatore_v6/tools/v6/verify_build_info.ps1`
  - `install_hooks.ps1` now resolves repository root via git.

### Added (Bootstrap)

- Completely rebuilt V6 planning package:
  - `docs/V6_MASTER_PLAN.md`
  - `docs/V6_EXECUTION_BACKLOG.md`
- Added dedicated V6 quality documents:
  - `docs/V6_TEST_PLAN.md`
  - `docs/V6_TOOLING_PLAN.md`

## [v6.0.0] - 2026-05-25

### Added (v6.0.0)

- Bootstrap project structure for `fermentatore_v6` with `config.h`, `fermentatore_v6.ino`, `display`, `connettivita`, and controller modules.
- Introduced common controller interface in `controller.h` and dispatcher in `controller.cpp`.
- Added initial M2 control modes implementation:
  - `on_off`
  - `on_off_hyst_sym`
- Added firmware version exposure in:
  - `config.h` via `FIRMWARE_VERSION`
  - `display.cpp` via boot output and `display_firmware_version()`
  - `connettivita.cpp` via `firmware` field in emitted JSON payload.
