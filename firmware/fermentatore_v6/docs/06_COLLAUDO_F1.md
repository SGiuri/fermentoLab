# 06. Collaudo automatico di F1

**Versione documento**: 1.0 (2026-09-19) · **Stato**: corrente · **Motore collaudo**: 1.2.0 · **Obiettivo**:
verificare F1 senza birra prima del 2026-10-03

Il collaudo è una suite di test reali sul fermentatore F1 (non simulati): invia comandi MQTT veri, legge
lo stato e lo storico da Home Assistant e giudica il comportamento con criteri numerici. Gira da solo in
un'app AppDaemon nel Lab e riprende da dove era interrotto se HA o l'app si riavviano. Vedi anche
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md) per modalità e pacchetti, e
[10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) per quello che manca.

## 1. Perché

Prima del 2026-10-03 bisogna essere sicuri che F1 tenga la temperatura in modo affidabile con la birra
dentro, e che gli allarmi avvisino davvero se qualcosa va storto. Il collaudo automatizza i test perché:
durano ore (il fermentatore pieno d'acqua ha inerzia termica), l'utente non è presente 24 ore su 24, e
un test scritto una volta si ripete identico dopo ogni intervento sul firmware o sull'hardware.

## 2. Vincoli

- Il fermentatore è pieno d'acqua, non birra: i test possono spingerlo a 2 °C o a 17 °C senza problemi.
- Riscaldamento lento (resistenza 30-35 W): la priorità è verificare il **freddo**.
- Durata massima per l'insieme dei test: **3 giorni**.
- L'utente non è sempre disponibile: i test manuali aspettano fino a 72 ore che lui intervenga.
- L'ESP32 è sigillato: l'utente può solo togliere e ridare corrente, non scollegare sensore o cavi.
- Il firmware 6.5.0, con le funzioni di collaudo per sensore/relè/WiFi/watchdog, non è ancora pronto: la
  prima sessione (Fase A) usa solo funzioni della 6.3.0.

## 3. Architettura

```
Home Assistant (pacchetto fermentatore_v6_f1_collaudo.yaml)
  ├─ input_boolean f1_collaudo_sel_<test>   selezione dei test
  ├─ script f1_collaudo_*                    avvia/interrompi/salta/azzera/entra/esci → evento
  └─ automazione f1_collaudo_inoltra_ack     esp/ack → evento f1_v6_ack
        │  eventi f1_collaudo_comando, f1_v6_ack
        ▼
AppDaemon (hass_spec/appdaemon/collaudo_f1/)
  ├─ collaudo_app.py      adattatore: HA ↔ motore (stato, storico, mqtt.publish, servizi, entità)
  └─ collaudo_motore.py   motore: catalogo dei test, macchina a stati, metriche, verdetto
        │  publica sensor.f1_collaudo_sessione / _verdetto / _<test_id>
        ▼
Dashboard "F1 Collaudo" (generata da hass_spec/dashboards/genera_collaudo.py)
```

- **Il motore non dipende da AppDaemon**: riceve un `ctx` con `ora/stato/storia/mqtt/servizio/entita/log`.
  Per questo lo stesso motore gira anche nel simulatore offline (§8).
- **Stato persistente**: `collaudo_stato.json` accanto all'app, riscritto a ogni cambio di fase. Un
  riavvio di AppDaemon o di Home Assistant rilegge il file e riprende dal punto esatto.
- **Tick**: ogni 30 secondi (`run_every`). Ogni test è una macchina a stati con una "fase" e un istante di
  inizio fase; la logica di un test sta tutta nel suo metodo `tick()`.
- **Ack**: l'automazione HA inoltra ogni `esp/ack` come evento `f1_v6_ack`; il motore lo indicizza per
  `request_id` e i test aspettano la risposta con `stato_ack()`.
- **Metriche**: `Motore.metriche(t0, t1)` legge lo storico di temperatura, setpoint, relè e "dati in
  arrivo" con `ctx.storia()` e calcola durata, T min/max, errore massimo e medio, duty e avvii dei relè,
  tempo senza dati, tempo con sensore guasto, riavvii dell'ESP32 (dall'uptime).

## 4. Modalità Collaudo e sicurezza

- I test partono solo con `input_select.f1_v6_modalita = Collaudo` (script `f1_collaudo_entra`, chiede
  conferma che non ci sia birra).
- **Limiti di sicurezza del collaudo** (oltre a quelli sempre attivi del firmware, §HOME_ASSISTANT_F1 §1):
  se la temperatura letta esce da 0,5-26 °C il collaudo si interrompe subito.
- **A fine collaudo**, o se interrotto: F1 torna al controllo di produzione (on/off, isteresi 0,5 °C), va
  alla temperatura di parcheggio (`input_number.f1_collaudo_temp_parcheggio`), la modalità torna a
  Produzione, e arriva un'email con l'esito completo.
- **Autoverifica**: prima di avviare, il motore controlla di poter leggere lo storico di HA (`autoverifica()`
  in `collaudo_app.py`); se fallisce, il collaudo non parte, per non scoprire il problema solo ore dopo
  quando un test deve calcolare le sue metriche.
- **App ferma**: se durante un collaudo l'entità della sessione non si aggiorna da 10 minuti, un'automazione
  del pacchetto HA avvisa (F1 continua comunque a regolare con l'ultimo comando ricevuto).

## 5. Catalogo dei test

Tre gruppi (`Test.gruppo`): **A** firmware attuale, automatico; **B** richiede il firmware 6.5.0; **C**
manuale.

| id | Nome | Gruppo | Durata stimata | Presenza |
|---|---|---|---|---|
| `comandi` | Protezione dei comandi | A | 10 min | nessuna |
| `lager` | Tenuta lager 10 °C | A | 6 h | nessuna |
| `riavvio` | Riavvio durante la regolazione | A | 5 min | nessuna |
| `stress_pompa` | Commutazioni della pompa | A | 1 h | nessuna |
| `diacetile` | Sosta del diacetile 17 °C | A | 12 h | nessuna |
| `rampa` | Rampa di discesa 17 → 10 °C | A | 8 h | nessuna |
| `modo_on_off` | On/Off senza isteresi | A | 2,5 h | nessuna |
| `modo_p` | Proporzionale (P) in freddo | A | 4 h | nessuna |
| `modo_pi` | PI in freddo (default) | A | 5 h | nessuna |
| `modo_pid` | PID in freddo | A | 5 h | nessuna |
| `cold_crash` | Cold crash 2 °C | A | 16 h | nessuna |
| `allarme_freddo` | Allarme se il freddo si ferma | A | 8 h | nessuna |
| `salute_sensore` | Salute del sensore | B | tutto il collaudo | nessuna |
| `sensore_breve` | Sensore senza alimentazione (1 min) | B | 10 min | nessuna |
| `sensore_lungo` | Guasto prolungato del sensore | B | 20 min | nessuna |
| `wifi` | WiFi assente 20 min | B | 40 min | nessuna |
| `watchdog` | Blocco del programma | B | 10 min | nessuna |
| `corrente` | Mancanza di corrente | C | fino a 72 h di attesa + 5 min | **2 interventi di 1 minuto** |

I test B sono segnaposto (`TestFirmware64`): risultano "non applicabile" finché il firmware installato è
sotto `fw_min` (6.5.0). Descrizione e criteri sono già scritti; l'implementazione arriva con la 6.5.0.
Dalla 6.4.1 il firmware pubblica i contatori delle letture del sensore (`esp/state.sensore_diag`, entità
`sensor.f1_v6_sensore_*`): sono la base del test `salute_sensore`, la cui logica nel motore resta da scrivere.

### Dettaglio dei test A

**Protezione dei comandi**: invia setpoint fuori range, isteresi e guadagni non validi, uno STOP con
timestamp di un'ora fa (come un messaggio rimasto nel broker), lo stesso `control_set` due volte, un JSON
corrotto. Verifica il `reason_code` di ogni rifiuto e che alla fine profilo, setpoint, modo e isteresi
siano identici a prima.

**Tenuta** (`lager`, `diacetile`, `cold_crash`, riuso di `TestTenuta`): porta la temperatura al target con
un profilo costante, poi la tiene per un tempo fisso. Misura tempo di arrivo, velocità (riscaldamento o
raffreddamento), scostamento massimo e medio in tenuta, avvii dei relè, e — nel cold crash — quanto resta
acceso il freddo (`duty_freddo_max`) e la temperatura minima raggiunta.

**Riavvio durante la regolazione**: invia `reboot` con un profilo attivo, aspetta il ritorno online (uptime
che torna a un valore basso) e verifica causa "software", stesso profilo/step/setpoint/modo/isteresi,
regolazione ripresa, tempo del profilo non tornato indietro, nessuna email di allarme.

**Commutazioni della pompa** (`stress_pompa`): sposta il setpoint appena sotto e appena sopra la
temperatura 25 volte, rispettando i tempi minimi dei relè (accendi, aspetta 40 s, spegni, aspetta 65 s), e
conta i riavvii spontanei dell'ESP32 e i minuti con sensore inutilizzabile. Nato per ricreare il guasto
storico "perdita del sensore alla commutazione dei relè" (§7).

**Rampa 17 → 10 °C**: profilo con rampa lineare di 6 ore (1,2 °C/h, come dalla sosta del diacetile alla
maturazione). Verifica il setpoint a metà rampa, lo scostamento durante la rampa, e la temperatura 1,5 ore
dopo la fine.

**Modi di controllo in freddo** (`modo_on_off`, `modo_p`, `modo_pi`, `modo_pid`, classe `TestModo`):
ciascuno ferma F1 (stop → ack), applica il modo con `control_set`, applica un gradino di setpoint con un
profilo costante, osserva per un tempo fisso, poi ripristina il controllo di produzione con la stessa
sequenza stop → control_set → profilo. Durante l'osservazione campiona a ogni tick i valori di
`esp/controllo` (errore, P, I, D, uscita, duty) per verificare la coerenza interna:
- **On/Off senza isteresi**: dallo storico di temperatura, setpoint e relè calcola a quale scostamento la
  pompa si accende e a quale si spegne; l'isteresi *effettiva* deve essere ≤ 0,25 °C (il campo `isteresi`
  dell'`esp/controllo` non cambia perché il firmware lo ignora in questo modo: bisogna guardare il
  comportamento, non il parametro dichiarato).
- **P**: verifica `p ≈ Kp × errore`, `i` e `d` nulli, duty dei relè coerente con l'uscita (zona morta 2,5 %,
  uscita minima 5 %, come da `config.h`), e che la temperatura scenda di almeno un terzo del gradino;
  calcola l'errore a regime atteso come duty / Kp e lo confronta con quello osservato.
- **PI** (guadagni di default: Kp 25, Ki 0,03): deve arrivare al setpoint (±0,3 °C) con errore medio ≤
  0,2 °C nell'ultima ora e termine I entro ±50 % (anti-windup).
- **PID** (Kp/Ki di default, **Kd 5000**: il Kd di default è 0, quindi userebbe di fatto il PI): verifica che
  il termine D sia diverso da zero e senza picchi (≤ 30 %), oltre ai criteri del PI.

**Allarme se il freddo si ferma**: porta l'acqua a bassa temperatura (2 °C se già fatto il cold crash,
altrimenti fino a 10 °C), poi alza l'isteresi a 3 °C per simulare un freddo che non parte. Verifica che
l'allarme di scostamento scatti (§HOME_ASSISTANT_F1 §4) entro il tempo impostato + 30 minuti e che arrivi
l'email; poi ripristina l'isteresi a 0,5 °C e verifica il rientro entro 6 ore. Misura anche la velocità di
riscaldamento dell'acqua col freddo fermo.

**Mancanza di corrente** (manuale): chiede all'utente, con una notifica persistente, di togliere
l'alimentazione all'ESP32 e ridarla dopo almeno 5 minuti, in qualunque momento nelle 72 ore successive.
Verifica causa del reset, stato ripristinato, email "ESP32 offline" se il buio è durato più di 3 minuti.

## 6. Metriche e verdetto

- Ogni test produce una lista di **controlli** (`voce`, `ok`, `valore`, `limite`, `critico`): un controllo
  non critico che fallisce dà un test "avviso", uno critico dà "fallito".
- **Verdetto complessivo** (`Motore.verdetto()`): "pronto per la birra" se tutti i test essenziali sono
  superati o solo con avviso; "non pronto" se almeno uno è fallito, o se ci sono stati riavvii spontanei
  dell'ESP32 durante la sessione; "incompleto" se mancano test da eseguire.
- **Riavvii spontanei**: tracciati per tutta la sessione (`_controlla_riavvii`), non solo dentro il test che
  li ha causati, perché un reset durante un test "innocente" (per esempio la tenuta) è comunque un problema.
  `ultima_commutazione()` cerca nello storico, nei 2 minuti prima del boot, l'ultima commutazione di un
  relè o un cambio di setpoint che comanda lo spegnimento della pompa (il reset a volte avviene prima che
  il firmware pubblichi lo stato "spento").

## 7. Risultati della prima sessione (Fase A, avviata 2026-09-18 23:50, fw 6.3.0)

| Test | Esito | Nota |
|---|---|---|
| Protezione dei comandi | Superato | tutti i rifiuti con il motivo atteso |
| Tenuta lager 10 °C | Superato | scostamento massimo 0,56 °C; con l'inerzia del glicole l'acqua è scesa a 9,50 °C e la resistenza si è accesa una volta |
| Riavvio durante la regolazione | Superato | ritorno online in 30 s |
| **Commutazioni della pompa** | **Fallito** | 2 riavvii spontanei su 25 cicli (4,7 s e 2,5 s dopo lo spegnimento comandato della pompa); vedi sotto |

**Guasto trovato**: F1 si resetta da solo (causa "poweron", offline meno di 15 s) a distanza di 0,7-4,7
secondi da uno spegnimento della pompa. Nello storico delle 37 ore precedenti erano già successi altri 2
reset con lo stesso schema (18/09 11:25 e 23:59), per un totale di 4 su circa 30 commutazioni osservate.
Non è un vero blackout: l'ESP32 riprende da solo con lo stesso profilo in circa 30 secondi, ma per qualche
secondo il sensore risulta inutilizzabile — verosimilmente la causa dei guasti al sensore osservati in
passato.

**Ipotesi**: disturbo elettrico sulla linea di alimentazione o sul pin di reset (EN) dell'ESP32 al momento
in cui il relè apre il carico della pompa (220 V, 10-15 W). L'alimentatore del sistema (un alimentatore da
Raspberry Pi) è adeguatamente dimensionato: il sospetto è la commutazione induttiva della pompa, non la
potenza disponibile.

**Intervento in corso (utente, 2026-09-19)**: condensatore tra EN e GND dell'ESP32. Se non bastasse,
prossimi passi in ordine di costo: snubber RC (100 Ω + 100 nF X2) o varistore sulla pompa; alimentazione
separata per il modulo relè. Vedi [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

**Verifica dopo l'intervento**: rilanciare solo `stress_pompa` dalla dashboard (25 cicli, circa un'ora) e
controllare `sensor.f1_v6_riavvii_spontanei` prima/dopo.

**Sessione interrotta il 2026-09-19** (era arrivata a `diacetile`, con `comandi`/`lager`/`riavvio`
superati) per scrivere la 6.4.0 (timing del profilo, vedi
[10_PUNTI_APERTI.md §0](10_PUNTI_APERTI.md#0-timing-del-profilo-implementato-in-firmware-640-2026-09-19)),
poi **ripartita lo stesso giorno alle 13:47 sulla 6.3.0** — la 6.4.0 non si può flashare senza accesso
a F1 (hotspot, lunedì) e i test rimasti non dipendono dal timing corretto, quindi fermarsi in attesa
avrebbe solo perso giorni. Set rilanciato: `diacetile` (rifatto da capo, era stato interrotto), `rampa`,
`modo_on_off`, `modo_p`, `modo_pi`, `modo_pid`, `cold_crash`, `allarme_freddo` — circa 61 ore, fine
stimata martedì 22/09 di notte. Non rilanciati: `comandi`/`lager`/`riavvio` (già superati sulla stessa
firmware, nessuna informazione nuova), `stress_pompa` (vedi sotto), `corrente` (serve una persona).

### Quali test ripetere dopo il flash della 6.4.0 e l'intervento hardware

> Il firmware che verrà flashato è la **6.4.2** (= 6.4.1 + diagnostica dei reset, vedi §7; nessuna modifica
> al controllo): la valutazione qui sotto vale anche per lei.

Valutazione del 2026-09-19, test per test. La 6.4.0 cambia *solo* come il profilo avanza nel tempo
(contatore locale al posto dell'epoch); l'intervento hardware (condensatore su EN, eventuale snubber)
riguarda i reset alla commutazione della pompa.

| Test | Esito su 6.3.0 | Da ripetere dopo 6.4.0 + hardware? | Motivo |
|---|---|---|---|
| `comandi` | superato | no | il contratto dei comandi non cambia |
| `lager` | superato | solo come prova rapida | tenuta a temperatura costante: il timing del profilo non conta |
| `riavvio` | superato | **sì** | verifica che "il tempo del profilo non torni indietro" dopo un riavvio: è proprio ciò che la 6.4.0 ha riscritto (checkpoint del progresso prima del reboot) |
| `stress_pompa` | **fallito** (guasto hardware) | **sì, subito dopo l'intervento** | ripeterlo prima non dà informazioni nuove: il guasto è già documentato e la correzione non c'è ancora |
| `diacetile` | in corso | no, se passa | tenuta a temperatura costante |
| `rampa` | in corso (baseline 6.3.0) | **sì** | esercita il calcolo della posizione lungo una rampa, che la 6.4.0 ha cambiato: il confronto prima/dopo è il controllo di non-regressione |
| `modo_on_off`, `modo_p`, `modo_pi`, `modo_pid` | in corso | no, salvo fallimenti dovuti ai reset | il controllo P/PI/PID non è toccato dalla 6.4.0 |
| `cold_crash`, `allarme_freddo` | in corso | no | tenuta / catena di allarme, non toccate |
| `corrente` (manuale) | non eseguito | **sì, nella variante in rampa** | è il test che esercita il bug corretto: oggi fa il blackout su una tenuta costante, dove il salto del vecchio comportamento non si vedrebbe (`corrente_in_rampa`, vedi [11_TIMING_PROFILO.md §6.2](11_TIMING_PROFILO.md#62-test-proposto-blackout-durante-una-rampa-corrente_in_rampa)) |

**Attenzione ai reset della pompa nel frattempo**: finché il guasto hardware c'è (circa 1 spegnimento
su 12), qualunque test con molti cicli della pompa — `modo_on_off` in particolare, che li forza di
proposito — può fallire per il solo criterio "riavvii spontanei" senza che ci sia un problema nuovo.
Un fallimento di quel tipo va riletto dopo l'intervento, non trattato come una regressione.

### Diagnosi dei reset con la 6.4.2 (`esp/diag`)

I reset osservati finora hanno causa `poweron` (18/09) e `wdt` (dal 19/09 sera). La 6.4.2 permette di capire
cosa li provoca senza collegare l'USB: il firmware lascia in memoria RTC l'ultimo giro del loop, l'ultimo relè
commutato e la sezione del loop in corso, e al boot successivo li pubblica in `esp/diag` (contratto in
[04_CONTRATTO_HASS.md §2.1c](04_CONTRATTO_HASS.md)). Entità: `sensor.f1_v6_reset_hardware` (con tutti i campi
come attributi), `_reset_ultimo_giro`, `_reset_ultima_commutazione`, `_loop_massimo`, `_loop_sezione_lenta`,
`_loop_giri_lenti`.

Come leggerle (ipotesi da verificare sul campo, non risultati):

| Osservazione | Lettura |
|---|---|
| `hw_nome` `RTCWDT_*` o `TGWDT_*`, `giro_ms` ≈ tempo di avvio, `comm_ms` di pochi secondi, `comm_rele` = pompa spenta | Disturbo elettrico alla commutazione: il loop girava un istante prima. Conferma la correlazione con la pompa |
| `hw_nome` `TG0WDT_SYS_RESET` o `giro_ms` vicino a 60 000 | Il loop era fermo: blocco del software. La sezione dice dove |
| `giro_ms` di circa 9 000-10 000 | Avvio bloccato: watchdog RTC del bootloader (il chip si è riavviato e non è ripartito) |
| `rtc_azzerato` true (tipico di `RTCWDT_RTC_RESET`) | Reset del dominio RTC: i tempi non sono affidabili, conta il solo codice |
| `valido` false, `hw_nome` `POWERON_RESET` | Vera perdita di alimentazione: nessun dato del boot precedente |
| `loop_max_ms` alto senza reset, `loop_max_sez` `rete` | Connessione WiFi/MQTT lenta: risponde a «ci sono operazioni oltre 60 s?» |

Il tempo `giro_ms` include l'avvio del chip (ROM e bootloader), che va misurato: il test `riavvio` provoca un
riavvio comandato (`SW_CPU_RESET`) e dà il valore di riferimento per tutti gli altri reset.

## 8. Simulatore offline (`tests/collaudo/simula_collaudo.py`)

Fa girare il **motore reale** (`collaudo_motore.py`, non una copia) contro un modello del fermentatore:
capacità termica dell'acqua, scambio con l'ambiente, camicia a glicole, regolazione on/off e P/PI/PID
uguale a quella del firmware 6.3.0 (anti-windup, derivata filtrata sulla misura, zona morta, uscita minima,
finestra PWM di 600 s), risposte MQTT con gli stessi `reason_code` del firmware. Serve a validare le
modifiche al motore prima di provarle su F1 vero, in secondi anziché giorni.

```
python fermentatore_v6/tests/collaudo/simula_collaudo.py            # tutti gli scenari
python fermentatore_v6/tests/collaudo/simula_collaudo.py completo   # un solo scenario
```

Scenari: `completo` (Fase A intera + blackout simulato), `modi` (i quattro test dei modi), `pompa_reset`
(reset casuale al 10 % delle commutazioni, per verificare che il collaudo lo rilevi), `riavvio_motore`
(riavvio del motore a metà sessione, verifica la ripresa dallo stato), `freddo_debole` (chiller
sottodimensionato), `bug_firmware` (un firmware che eseguisse per errore uno STOP vecchio).

## 9. Come usarlo

1. Dashboard **F1 Collaudo** → *Entra in modalità Collaudo* (conferma che non c'è birra).
2. Selezionare i test (pulsanti Fase A / Fase B / Manuali / Nessuno, o singolarmente) e *Avvia*.
3. Seguire l'andamento nella vista Collaudo: stato, verdetto, grafico di 3 giorni, risultati, registro.
4. *Salta* passa al test successivo; *Interrompi* chiude la sessione e riporta F1 in produzione.
5. A sessione conclusa, decidere in base al verdetto se procedere con la birra o correggere e ripetere.

Per aggiungere un test a una sessione già in corso (come fatto per i quattro test dei modi durante questa
sessione): evento `f1_collaudo_comando` con `azione: inserisci`, `test: [...]` e opzionalmente
`dopo: <id>` per la posizione in coda.

## 10. Aggiornare il motore

Il codice si carica via SSH in `/addon_configs/a0d7b954_appdaemon/apps/collaudo_f1/`; AppDaemon ricarica
l'app da solo quando i file cambiano. Lo stato persistente (`collaudo_stato.json`) non viene toccato
dall'aggiornamento, quindi una sessione in corso continua con il motore nuovo. Prima di caricare in
produzione, validare con il simulatore (§8).
