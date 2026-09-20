# 11. Cattura delle curve di fermentazione: come funziona, rischi, cosa è stato corretto

**Versione documento**: 2.0 (2026-09-19) · **Stato**: analisi (§1-3) corrente; l'opzione A (§4.1) è
**implementata nel firmware 6.4.0**, validata con i test host e il simulatore offline — **non ancora
flashata su F1** (nessun accesso al dispositivo prima di lunedì). §2-3 restano la descrizione
accurata del comportamento fino alla 6.3.0 (installata su F1 al momento in cui questo documento è
stato scritto). Riferimenti: [00_INDICE.md](00_INDICE.md), [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md),
[10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

## 1. Perché questo documento

Il fermentatore avanza lungo una curva multi-step (temperatura costante e rampe) definita in un
unico JSON inviato una volta con `profile_set` — lo stesso principio con cui erano gestiti i JSON
"fino alla chiusura del processo" nelle versioni precedenti. Un batch reale dura settimane (§2.1):
in quella finestra, un'assenza di rete, un blackout o un guasto del sensore durante una rampa non
sono eventi esotici, sono probabili. Questo documento descrive esattamente come il firmware si
comporta in questi casi (verificato leggendo il codice, non per ipotesi), valuta il rischio reale
per la birra, e propone alternative.

## 2. Come funziona oggi

### 2.1 Il formato della curva — un JSON per l'intero processo

I profili reali usati oggi vivono come file JSON **sul filesystem del Lab**
(`/config/profili_fermentazione/*.json`, non nel repository — vedi §5.1), letti da
`scripts_py/avvia_fermentazione_v6.py` e convertiti nel formato che il firmware accetta
(`duration_days`/`ramp_days` → `duration_hours`/`ramp_hours`). Esempio reale, `lager_generica.json`:

| Step | Temp | Durata | Tipo |
|---|---:|---:|---|
| Fermentazione Primaria | 11 °C | 10 gg | costante |
| Rampa Diacetile | → 16 °C | 2 gg | **rampa** |
| Diacetyl Rest | 16 °C | 3 gg | costante |
| Discesa Lagerizzazione | → 2 °C | 5 gg | **rampa** |
| Lagerizzazione | 2 °C | 28 gg | costante |

Totale: **48 giorni**. `ale_generica.json` (45 giorni), `czech_pils_W34.json`, `ipa_dryhop.json` e
`stout_US04.json` seguono lo stesso schema: 5 step tipici, 2-3 rampe di 1-5 giorni ciascuna, il
resto tenute costanti. Le rampe sono una minoranza del tempo totale ma sono i punti in cui un
problema di temporizzazione ha un effetto reale (una tenuta costante non ha un bersaglio che si
sposta nel tempo: un salto del "clock" del profilo durante una tenuta non cambia il setpoint).

### 2.2 Esecuzione: locale e autonoma, con NVS

Una volta ricevuto, il profilo è validato e salvato in NVS (`parsaProfiloJSON`, `salvaProfilo` —
`profilo.cpp`). Da quel momento **l'esecuzione non dipende da Home Assistant**: il firmware calcola
da solo, ogni `CONTROL_INTERVAL_MS`, la posizione nella curva e il setpoint
(`aggiorna_setpoint_da_profilo`, `fermentatore_v6.ino:167`), indipendentemente da MQTT/WiFi. Un
riavvio dell'ESP32 ripristina profilo e stato da NVS **prima ancora di connettersi al WiFi**
(`setup()`, `fermentatore_v6.ino:1005-1027`) e riprende a regolare. Questa parte è già solida: vedi
§3.1.

### 2.3 Il punto debole: il "clock" del profilo è un epoch assoluto

La posizione nella curva è **tempo trascorso dall'avvio**, calcolato come differenza fra due epoch
Unix (`secondi_trascorsi_profilo`, `fermentatore_v6.ino:130`):

```
secondi_trascorsi = epoch_corrente() − avvio_epoch
```

- `avvio_epoch` è fissato una volta sola all'avvio del profilo e salvato in NVS. Non si sposta mai
  da solo.
- `epoch_corrente()` (`fermentatore_v6.ino:122`) normalmente è l'ora reale (`time()`, sincronizzata
  NTP). **Se l'orologio non è sincronizzato** (subito dopo un riavvio, prima che il WiFi/NTP si
  connettano, oppure se non c'è affatto rete), usa un ripiego: l'ultimo epoch noto salvato in NVS
  prima dello spegnimento, più i millisecondi trascorsi dal boot (`ultimo_epoch_noto`, aggiornato
  ogni 10 minuti mentre il profilo è in marcia — `NVS_SAVE_INTERVAL_MS`, `handle_persistenza`,
  `fermentatore_v6.ino:336-356`).

Il ripiego è pensato bene per continuare a regolare **subito** dopo un riavvio, prima che WiFi/NTP
siano pronti (altrimenti il profilo resterebbe fermo per i secondi/minuti necessari a riconnettersi).
Ma ha una conseguenza che il codice non gestisce: mentre l'orologio non è sincronizzato, il tempo
trascorso **non include il tempo realmente passato durante lo spegnimento** (il ripiego riparte
da "ultimo epoch noto" più zero, non più la durata del blackout). Il profilo si comporta come se
fosse rimasto in pausa. Quando NTP si sincronizza (tipicamente pochi secondi dopo la riconnessione
WiFi, se la rete è disponibile), `epoch_corrente()` torna a essere l'ora reale, e
`secondi_trascorsi_profilo()` **salta istantaneamente in avanti** di quanto è durato il blackout,
perché `avvio_epoch` non si è mai spostato. Se in quel momento il profilo è a metà di una rampa, il
setpoint fa un salto discreto verso il punto in cui "dovrebbe" trovarsi in quel momento secondo
l'orologio reale, invece di continuare la transizione graduale prevista.

Nessun meccanismo oggi corregge questo salto: non c'è un rate-limiter sul setpoint, né un
ribasamento di `avvio_epoch` per assorbire il tempo perso.

### 2.4 Il sensore e il profilo sono disaccoppiati — di proposito, ma con un effetto collaterale

`aggiorna_setpoint_da_profilo(true)` viene chiamata **a ogni ciclo di controllo, incondizionatamente**
(`loop()`, `fermentatore_v6.ino:1085-1091`), mentre l'uscita verso i relè
(`controller_apply_inputs`) gira solo `if (temp_ok)` — con il sensore non utilizzabile i relè
vengono forzati spenti (`controller_stop_outputs`). Questo è corretto per la sicurezza (fail-safe:
mai pilotare i relè su una lettura inventata), ma ha una conseguenza sulla curva: **durante un
guasto del sensore, il setpoint pianificato continua ad avanzare mentre la temperatura reale non è
governata affatto.** Se il guasto capita durante una rampa e dura a lungo, alla ripresa il
controllore si trova con un errore grande da recuperare, e la parte di rampa "persa" non viene mai
davvero eseguita sul liquido reale — il curve-tracking si rompe silenziosamente finché non scatta
l'allarme di scostamento.

### 2.5 Un gap operativo scoperto durante l'indagine: le modifiche a batch in corso

Sul Lab esiste `/config/profili_fermentazione/.runtime/f1_runtime.json`, generato — a giudicare dai
metadati (`"action": "allunga_step"`, `"source_profile": "stout_US04"`) — da un intervento manuale
per allungare uno step durante un batch reale, non da uno script versionato in questo repository:
non c'è un comando `profile_set` "patch" ufficiale, solo la possibilità di rimandare un `profile_set`
completo con gli step ricalcolati a mano. Utile saperlo: la curva non è sempre stata eseguita
esattamente come pianificata all'inizio, e oggi non c'è un modo pulito e ripetibile per farlo. Vedi
§6.3.

## 3. Matrice dei rischi

| # | Scenario | Cosa succede oggi (verificato) | Probabilità su un batch di 45-48 gg | Impatto |
|---|---|---|---|---|
| R1 | **Assenza WiFi/MQTT, ESP32 mai riavviato** | Nessun effetto: `time()` resta sincronizzato e continua a scorrere sull'orologio interno dell'ESP32 indipendentemente dalla connettività. Il profilo avanza esattamente come previsto. | Alta (succede spesso) | **Nessuno** — già mitigato dal design |
| R2 | **Riavvio dell'ESP32 con WiFi/NTP disponibili** (reset software, watchdog, OTA) | NTP si risincronizza in pochi secondi dopo la riconnessione; lo scarto rispetto al tempo reale è trascurabile (secondi). | Alta (osservato più volte nel collaudo) | Basso |
| R3 | **Blackout (o riavvio) con rete/NTP indisponibili, durante una rampa** | Il profilo "si mette in pausa" per tutta la durata dell'assenza di NTP, poi **salta in avanti** non appena l'orologio si risincronizza — vedi §2.3. In casa, un blackout elettrico spegne quasi sempre anche router e modem: rete e alimentazione tornano insieme, quindi questo è lo scenario di blackout **più probabile**, non un caso raro. | Media-alta su 45-48 gg (un blackout di qualche ora capita) | **Alto se cade durante una rampa** (1-5 gg su 45-48, quindi ~5-15% delle volte "a caso"): richiesta di recupero brusca proprio nel tratto pensato per essere graduale (es. discesa di lagerizzazione, cold crash) |
| R4 | **Guasto prolungato del sensore durante una rampa** | Il setpoint pianificato avanza comunque (§2.4); i relè restano spenti finché il sensore non torna valido; nessun controllo del liquido per tutta la durata del guasto. | Bassa-media (il firmware ha già recovery automatico del sensore; un guasto "prolungato" dovrebbe essere raro se l'hardware è sano) — cresce se il guasto hardware trovato il 2026-09-18 ([06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630)) non viene risolto | **Alto se cade durante una rampa breve** (es. rampa diacetile di 1-2 gg): la fase intera può risultare non eseguita come da programma |
| R5 | **Assenza WiFi combinata con un riavvio** (es. router down per manutenzione, ESP32 si riavvia per un motivo indipendente) | Come R3: nessun modo di sapere quanto tempo reale è passato finché la rete non torna. | Bassa | Come R3, ma la finestra di "pausa silenziosa" può durare più a lungo (fino al ritorno della rete, non del solo blackout) |

**Non sono rischi di sicurezza fisica**: i limiti assoluti (36 °C / −1 °C), i tempi minimi dei relè
e il fail-safe sul sensore restano sempre attivi, indipendentemente da tutto questo. Sono rischi
**di qualità del processo**: la birra non segue la curva pensata, in modo silenzioso (nessun
allarme dedicato oggi distingue "temperatura anomala" da "il programma è saltato avanti/indietro
rispetto al previsto").

## 4. Alternative di mitigazione

Le opzioni sotto sono indipendenti e cumulabili; la raccomandazione è in §4.5.

### 4.1 Opzione A — tempo del profilo locale, accumulato e pausabile (non più un epoch assoluto)

> **Implementata nel firmware 6.4.0 (2026-09-19)**, vedi §4.5 e `profilo.h`/`fermentatore_v6.ino`
> (`avanza_profilo`). Il resto di questa sezione descrive la proposta; è rimasta accurata anche a
> implementazione fatta.

Invece di "tempo trascorso = ora reale − avvio_epoch", accumulare i secondi di avanzamento in un
contatore persistito (checkpoint ogni `NVS_SAVE_INTERVAL_MS`, come oggi), incrementato **solo**
mentre il controllo è realmente in corso: RUNNING **e** (opzionalmente) sensore utilizzabile.

- Elimina del tutto la dipendenza da NTP per l'avanzamento del profilo: nessun "salto" possibile,
  perché non c'è più un orologio assoluto da riallineare. Il profilo semplicemente si ferma durante
  un blackout (l'ESP32 è spento: non può fare altro) o durante un guasto prolungato del sensore (se
  si sceglie di mettere in pausa anche in quel caso), e riprende esattamente da dove era.
- È anche più corretto dal punto di vista della fermentazione: ciò che conta per il lievito è il
  tempo *passato a quella temperatura*, non l'ora del calendario. Una pausa non pianificata non
  dovrebbe "consumare" ore di rampa senza che il liquido le abbia davvero vissute.
- Effetto collaterale voluto: **rende l'esecuzione del profilo completamente locale**, senza alcuna
  dipendenza da NTP/rete per la logica di controllo (risponde direttamente alla domanda sul
  "caricare il programma in locale", vedi §5).
- Costo: cambia il formato persistito in NVS e la logica di `secondi_trascorsi_profilo` — un
  intervento nel firmware, non enorme ma da testare bene (i test host attuali assumono la semantica
  a epoch, vanno aggiornati).

### 4.2 Opzione B — ribasare `avvio_epoch` invece di ricostruire il timer

Variante più chirurgica di A, senza cambiare il modello a epoch: quando l'orologio torna valido dopo
essere stato invalido con il profilo in RUNNING/HOLDING, invece di lasciare `avvio_epoch` fisso,
spostarlo in avanti della durata stimata dell'interruzione (differenza fra l'orologio appena
risincronizzato e l'ultimo `ultimo_epoch_noto` salvato, al netto degli uptime coperti dal ripiego).
Effetto pratico equivalente ad A (nessun salto), diff più piccolo nel codice esistente. Meno pulito
concettualmente (si tengono due nozioni di tempo da riconciliare), ma più veloce da implementare e
verificare con i test esistenti.

### 4.3 Opzione C — limitare la velocità con cui il setpoint può cambiare (rate limiter)

> **Valutata durante l'implementazione e scartata**: vedi la nota in fondo a questa sezione.

Indipendente da A/B: invece di assegnare `g_setpoint = sp` istantaneamente
(`aggiorna_setpoint_da_profilo`, riga `if (sp > -998.0f) g_setpoint = sp;`), far avvicinare il
setpoint *applicato* al setpoint *target* con una velocità massima configurabile (°C/ora), come già
avviene naturalmente durante una rampa pianificata. Un salto improvviso — che sia causato da un
recupero di NTP, da un guasto del sensore, o anche da un errore umano nel comando HA — verrebbe
sempre attenuato a un ritmo di variazione bevanda-compatibile, invece di chiedere ai relè
un'accelerazione massima verso il nuovo bersaglio.

- Vantaggio: protegge *da qualunque causa* di salto del setpoint, non solo da quelle individuate
  qui — difesa in profondità, poco codice (un clamp sull'incremento per ciclo), nessun cambiamento
  al modello di temporizzazione.
- Non risolve da solo il problema di fondo (il profilo può comunque desincronizzarsi dal tempo
  reale, vedi R3/R4): rallenta solo la *reazione* del controllore al salto, rendendola meno brusca
  per il liquido. Va combinato con A o B per correggere anche la causa.

**Perché è stata scartata**: un limitatore *generale* frenerebbe anche i cambi di setpoint
*intenzionali* che il sistema usa attivamente altrove — il gradino della vista Esperimenti
(`f1_v6_lab_gradino`, pensato apposta per un salto immediato, non graduale) e le chiamate
`costante()`/i profili a temperatura fissa che il collaudo stesso usa per passare rapidamente da un
test all'altro (`TestModo`, `TestTenuta`, `TestAllarmeFreddo`) entro il budget di 3 giorni. Un rate
limiter non distingue "questo è un salto voluto" da "questo è un salto anomalo": li tratterebbe
allo stesso modo, rompendo funzioni già in uso. Con l'opzione A implementata, inoltre, il problema
che doveva attenuare non può più verificarsi: `secondi_trascorsi` non fa mai un balzo, quindi la
rampa non genera mai un bersaglio discontinuo da smorzare. Non è stata sostituita da nessun'altra
forma di limitazione: il caso resterebbe utile solo per una fonte di salto non ancora individuata,
e non ce n'è evidenza oggi.

### 4.4 Opzione D — allarme dedicato di "scostamento dal programma"

Un allarme separato da quello esistente di temperatura fuori range: se `|temperatura − setpoint|`
resta sopra soglia più a lungo di quanto una rampa dovrebbe impiegare a colmarlo, avvisare che "il
processo è indietro/avanti rispetto al programma" — utile soprattutto se si sceglie di non
implementare A/B subito: almeno l'operatore verrebbe avvisato che la curva reale sta divergendo da
quella pianificata, invece di scoprirlo solo guardando lo storico a batch finito.

### 4.5 Raccomandazione e cosa è stato fatto

Raccomandazione originale (2026-09-19, prima di implementare): C come intervento immediato, A come
correzione strutturale per la 6.4.0, D come rete di sicurezza nel frattempo, B tenuta in tasca come
ripiego più rapido se A si fosse rivelata troppo rischiosa.

**Deciso e fatto lo stesso giorno**, dopo aver interrotto la sessione di collaudo in corso su F1 per
non continuare a testare una versione che si sapeva da aggiornare:

1. **Opzione A implementata** in `profilo.h`/`profilo.cpp`/`fermentatore_v6.ino` (firmware 6.4.0):
   nuovo campo `secondi_trascorsi` nel profilo, funzione `avanza_profilo()` chiamata a ogni ciclo di
   controllo, persistenza dedicata (`salvaProgresso`/chiave NVS `progresso_s`, sostituisce
   `ultimo_epoch_noto`/`epoch_noto`) più un checkpoint immediato prima di un riavvio volontario.
   `avvio_epoch` resta, solo come informazione di quando è cominciato il batch. **Non ancora
   flashata su F1** — nessun accesso al dispositivo prima di lunedì.
2. **Opzione C valutata e scartata** durante l'implementazione stessa: vedi la nota in §4.3. Un
   limitatore generale avrebbe rotto funzionalità già in uso (gradino didattico, cambi rapidi di
   setpoint del collaudo), e con l'opzione A il salto che doveva attenuare non può più accadere.
3. **Opzione D non implementata**: era pensata come rete di sicurezza *nel periodo di transizione*
   prima che A fosse pronta; con A già scritta e testata non è più stata la priorità di questo
   intervento. Resta un'idea valida se in futuro emergesse un'altra fonte di scostamento dal
   programma da segnalare.
4. **Opzione B non necessaria**: A è stata implementabile e verificabile nei tempi, non è servito il
   ripiego più rapido.

**Verifica**: 3 nuovi scenari nella suite di test host
(`profilo_pausa_blackout_a`/`profilo_pausa_blackout_b`, due processi come `scenario_persistenza_*`,
e `profilo_pausa_sensore`), più la riscrittura dello scenario `profilo_avvio_zero` per dimostrare
l'esecuzione senza alcuna sincronizzazione NTP durante l'intero profilo. Tutti i 21 scenari della
suite superati, incluse le simulazioni di 48 h (nessuna regressione sul comportamento normale,
ininterrotto). Il simulatore offline del collaudo (`tests/collaudo/simula_collaudo.py`) è stato
aggiornato allo stesso modello di temporizzazione, e tutti gli scenari di collaudo simulati restano
coerenti con i risultati precedenti.

## 5. "Caricare il programma in locale sulla scheda" — cosa significa davvero qui

Vale la pena separare due cose diverse che la domanda mette insieme:

### 5.1 L'esecuzione della curva è già locale

Una volta che `profile_set` è stato ricevuto, l'ESP32 non ha più bisogno di Home Assistant per
eseguire tutto il resto del processo, comprese le rampe: la posizione nella curva, il calcolo del
setpoint e la persistenza attraverso i riavvii sono già interamente a bordo (§2.2). Spostare
*ulteriormente* l'esecuzione "sulla scheda" non aggiungerebbe robustezza al netto dei rischi in
§3 — quei rischi vivono nella logica del tempo (§2.3-2.4), non nella dipendenza da Home Assistant.

### 5.2 Quello che NON è locale: il catalogo delle curve stesse

I file `lager_generica.json`, `ale_generica.json`, ecc. (§2.1) esistono **solo** sul filesystem del
Lab, non versionati in questo repository, non copiati altrove. Se quel disco si perde o quella
cartella viene toccata per errore, le ricette numeriche delle birre (giorni e gradi di ogni fase)
vanno perse senza preavviso — un rischio indipendente da blackout e sensori, ma reale e facile da
correggere.

**Fatto il 2026-09-19**: i file sono ora in [`../profili/`](../profili/) (checksum verificato uguale
all'originale sul Lab) — storico, diff e backup ottenuti. Lo script del Lab continua a leggerli da
`/config/profili_fermentazione/`: la sincronizzazione fra le due copie resta manuale, un
miglioramento possibile ma non urgente (vedi il README in `../profili/`).

**Proposta opzionale, più difensiva**: un profilo minimo "di sicurezza" compilato nel firmware
stesso (poche righe, una temperatura costante ragionevole), usabile solo se non c'è alcun profilo
valido in NVS e arriva un comando esplicito per attivarlo — una rete di sicurezza per il caso limite
in cui sia il Lab sia la sua configurazione fossero irraggiungibili proprio mentre serve avviare un
nuovo batch. Non sostituisce il catalogo flessibile via `profile_set` (che resta il modo giusto di
definire una ricetta), è solo un ultimo paracadute. Priorità bassa rispetto a §5.2 e alla §4.

### 5.3 Le modifiche a batch in corso (§2.5)

Se si decide di rendere ufficiale la possibilità di modificare una curva a metà processo (come è già
successo in modo informale), vale la pena definire un comando esplicito — per esempio un
`profile_set` con un parametro "applica dal punto attuale in poi" che non tocchi gli step già
completati — invece di ricalcolare a mano un JSON intero come accaduto finora. Non urgente, ma da
tenere presente se questa esigenza si ripete.

## 6. Test dedicati da aggiungere

Il collaudo automatico ([06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)) oggi **non** esercita né R3 né R4
nella forma in cui contano davvero: il test manuale `corrente` fa il blackout con un profilo a
temperatura costante (nessuna rampa in corso, quindi nessun bersaglio che si sposta nel tempo), e i
segnaposto `sensore_breve`/`sensore_lungo` (Fase B, firmware 6.5.0) sono descritti anch'essi contro
una tenuta, non contro una rampa.

### 6.1 Scoperta utile: esiste già un profilo "onda" veloce

`/config/profili_fermentazione/test_wave_10_20m.json` (già usato in produzione per test rapidi)
alterna tenute di ~20 minuti e rampe di ~10 minuti fra 12-16 °C. È esattamente ciò che serve per
testare "guasto durante una rampa" in minuti invece che in giorni, dentro il budget di 3 giorni del
collaudo.

### 6.2 Test proposto: blackout durante una rampa (`corrente_in_rampa`)

Realizzabile **subito**, senza nessuna modifica al firmware — è solo una diversa orchestrazione del
test manuale esistente: invece di parcheggiare a temperatura costante, avviare un profilo tipo
`test_wave_10_20m` e chiedere il blackout mentre `sensor.f1_v6_step` indica una rampa in corso.
Criteri di superamento proposti: nessun salto di setpoint superiore a quanto la rampa avrebbe
percorso nella durata reale del blackout, nessuna richiesta di uscita al 100% prolungata subito dopo
il ritorno online. Con il firmware fino alla 6.3.0 era più uno strumento di **misura del problema**
che un test di conformità; con l'opzione A (§4.1) implementata in 6.4.0 diventa un vero test di
non-regressione.

### 6.3 Test proposto: guasto del sensore durante una rampa (estensione di `sensore_breve`/`sensore_lungo`)

Richiede le stesse funzioni di collaudo 6.5.0 già pianificate ([10_PUNTI_APERTI.md §3](10_PUNTI_APERTI.md))
per tagliare l'alimentazione del sensore: la sola differenza è farlo scattare mentre un profilo
tipo `test_wave_10_20m` è a metà di una rampa invece che su una tenuta, e misurare a fine test lo
scostamento fra dove il setpoint pianificato è arrivato e dove la temperatura reale si trova.

## 7. Prossimi passi

- [x] Opzione A implementata, opzione C valutata e scartata, opzione D non necessaria per ora —
  vedi §4.5.
- [x] Portato `profili_fermentazione/*.json` dal Lab al repository (§5.2).
- [ ] Flashare 6.4.0 su F1 e ripetere da capo la Fase A del collaudo — al prossimo accesso
  (lunedì, vedi [10_PUNTI_APERTI.md §1](10_PUNTI_APERTI.md)).
- [ ] Aggiungere `corrente_in_rampa` (§6.2) al collaudo — non richiede firmware nuovo, si può fare
  prima di lunedì; dopo il flash di 6.4.0 diventa un vero test di conformità (non solo uno
  strumento di misura del problema, essendo la causa ormai corretta).
- [ ] Estendere `sensore_breve`/`sensore_lungo` per esercitare anche il caso rampa (§6.3), insieme
  al resto delle funzioni di collaudo 6.5.0.

Riportato anche in [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).
