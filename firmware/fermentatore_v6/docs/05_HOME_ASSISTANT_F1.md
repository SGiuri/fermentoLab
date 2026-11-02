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
- **F2**: nessun hardware. I pacchetti F2 esistono (generati da quelli F1) ma nel Lab sono installati solo
  `_allarmi` e, dal 2026-10-17, `_cotta` e `_cotta_ispindel`: F2 è la lager di ottobre, tenuta da un Inkbird
  che HA non vede, con l'iSpindel dentro, e ha la sua dashboard **F2 Cotta** (§5).
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
| `fermentatore_v6_f1_cotta.yaml` | Dati della vista *Cotta*: inizio della fermentazione, profilo del piano, setpoint del regolatore esterno (Inkbird); il piano letto dal file del profilo con `shell_command` (`find … -exec cat`: un profilo vuoto o inesistente dà un piano vuoto senza errori nel log); temperatura del piano adesso; ore dall'inizio; dal 2026-10-17 l'automazione *Segui l'avvio del profilo* (vedi §5); dal 2026-10-26 le letture manuali della densità (script `f1_cotta_registra_lettura`, registro `sensor.f1_cotta_letture`, valori corretti in `sensor.f1_cotta_densita`, vedi §5) | sì (2026-10-03) |
| `fermentatore_v6_f1_ispindel.yaml` | Sorveglianza della regolazione stimata con l'iSpindel (scarto iSpindel − DS18B20, temperatura stimata, email fuori banda e iSpindel muto). Dal 2026-10-17 tutto dipende da `input_boolean.f1_ispindel_presente` (spento: l'iSpindel è nella lager in F2) | sì (2026-10-01) |
| `fermentatore_v6_f2_*.yaml` | Copie F2 generate da `genera_f2.py` (collaudo e iSpindel sono esclusi; `_cotta` dal 2026-10-17 è generato anche per F2) | `_allarmi`, `_cotta` |
| `fermentatore_v6_f2_cotta_ispindel.yaml` | Solo F2, scritto a mano: `sensor.f2_ispindel_ultima_lettura`, l'ora dell'ultimo messaggio dell'iSpindel (trigger MQTT su `ispindel/ispindel01/tilt`), che resta giusta anche dopo un riavvio di HA | sì (2026-10-17) |
| `storico_birre.yaml` | Dati della dashboard *Storico birre*: menu delle birre (`input_select.storico_birra`, opzioni messe da un'automazione), scheda della birra scelta (`sensor.storico_birra`, `sensor.storico_birra_note`), un'entità per posto dei grafici (`sensor.storico_birra_curva_t1`…`_t6`, `_sg1`, `_sg2`), script per aggiungere ed eliminare note, ripristinare il profilo e chiudere la birra. Legge e scrive le schede in `/config/storico_birre/` con `homeassistant/scripts_py/storico_birre.py` (formato in [`storico_birre/README.md`](../storico_birre/README.md)) | sì (2026-10-30) |

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
| **F1 Laboratorio** (`/f1-laboratorio`) | `hass_spec/dashboards/f1_v6_laboratorio_dashboard.yaml` | Panoramica · Cotta · Laboratorio · Esperimenti · Fermentazione · Diagnostica |
| **F1 Collaudo** (`/f1-collaudo`) | `f1_v6_collaudo_dashboard.yaml`, **generato** da `genera_collaudo.py` | Collaudo · Guida |
| **F1 Cotta** (`/f1-cotta`, dal 2026-10-23) | `hass_spec/dashboards/f1_v6_cotta_dashboard.yaml` | Cotta: la vista Cotta di F1 Laboratorio copiata identica, con in cima una riga di stato dell'ESP32 (sonda, setpoint, step, relè, allarmi, regolazione stimata); dal 2026-10-26, sotto le temperature, la densità dalle letture manuali (grafico, tabella, pulsanti *Registra una lettura* ed *Elimina l'ultima registrata*) |
| **Chiller condiviso** (`/chiller-condiviso`, dal 2026-10-23) | `hass_spec/dashboards/chiller_condiviso_dashboard.yaml` | Chiller: glicole, le due birre e i rispettivi setpoint, con la pompa di F1, 48 ore e 7 giorni. Didattica: il glicole è unico e una birra calda in F1 scalda la lager in F2. Dal 2026-10-28 glicole e sonda F2 vengono dal nodo sensori V7 (`sensor.v7_temperatura_chiller`, `sensor.v7_temperatura_f2`) e tutto sta su un solo asse in °C; l'iSpindel in F2 resta come seconda temperatura |
| **F2 Cotta** (`/f2-cotta`, dal 2026-10-17) | `hass_spec/dashboards/f2_v6_cotta_dashboard.yaml` | Cotta. Dal 2026-10-28 la temperatura principale è la sonda F2 del nodo sensori V7, con il glicole nella riga di stato; l'iSpindel resta per la densità e come seconda temperatura |
| **Storico birre** (`/storico-birre`, dal 2026-10-30) | `hass_spec/dashboards/storico_birre_dashboard.yaml` | Storico: una scheda per birra, scelta dal menu. Dati, date chiave, temperature e densità dall'inizio alla fine (misure e piano teorico), letture manuali, piano con le sue date, note (pulsante *Aggiungi una nota*), profilo con *Ripristina il profilo*. Le curve vengono dalle statistiche orarie, ognuna solo nel suo periodo `dal`/`al`; a fine birra *Chiudi la birra* copia curve e letture nella scheda (curve in `/config/www/storico_birre/<id>_curve.json`, servite da `/local`) e da lì la pagina non legge più dati vivi |
| F1 V6 (`/f1-v6`) | solo nel Lab | vecchia dashboard, da rimuovere dopo la validazione di F1 Laboratorio |

Card personalizzate richieste: mushroom, apexcharts-card, card-mod, state-switch, config-template-card
(vista *Cotta*).

**F1 Laboratorio**
- *Panoramica*: stato, modalità con i pulsanti di cambio (con conferma), misure, relè, grafico di 12 ore.
- *Cotta* (pannello a tutta larghezza, dal 2026-10-03; **dal 2026-10-17 è in F2 Cotta**, vedi sotto: in F1
  la vista segue la sonda e il setpoint dell'ESP32, senza iSpindel). Com'era fino al 17/10: la fermentazione dall'inizio a oggi. Temperature
  (mosto dall'iSpindel, parete inox dalla sonda F1, piano, Inkbird tratteggiato), sotto la densità con la
  stessa finestra (grafico a parte: °C e SG non condividono un asse; dal 07/10 con le linee tratteggiate di OG,
  da `input_number.ispindel_01_og`, e FG obiettivo, dalla chiave `fg_obiettivo` del profilo esposta come attributo
  di `sensor.f1_cotta_piano`, e l'attenuazione apparente dal vivo nell'intestazione), poi la batteria dell'iSpindel (asse fisso
  3,2-4,3 V, linea a 3,3 V: lì ha smesso di trasmettere il 30/09), tabella del piano con lo step in corso
  e il prossimo cambio, piano completo e il segno di *Ora*, dati della cotta. Il piano completo (dal 07/10)
  mostra solo l'iSpindel: temperatura del mosto, piano e Inkbird a sinistra, densità sull'asse di destra;
  la griglia è una linea a mezzanotte (ogni 2 giorni o solo il lunedì se troppo fitte), più spessa il
  lunedì, con le date gg/mm sotto: tutti i giorni se ci stanno, se no solo i lunedì. Il piano si
  calcola dal file del profilo e dall'ora di inizio, con la stessa regola di `calcolaSetpoint()` del
  firmware, quindi vale anche con l'ESP32 fermo e un regolatore esterno. Le misure vengono dalle
  statistiche a lungo termine (5 minuti nei primi 9 giorni, poi orarie) perché lo storico grezzo dura 10
  giorni. `config-template-card` calcola la finestra e ridisegna i grafici ogni ora (`sensor.f1_cotta_ore`):
  nel codice JS dei grafici non vanno template literal (`${…}`), li valuterebbe lui. Alla cotta
  successiva si impostano inizio e profilo in *Dati della cotta*; setpoint Inkbird a 0 quando non serve.
  Se il file del profilo ha la chiave `eventi` (date chiave: scarichi del cono, controlli, imbottigliamento,
  formato in `profili/README.md`), il piano completo le mostra come linee grigie tratteggiate e la tabella
  del piano aggiunge "Date chiave" con giorno, ora e cosa fare (✓ fatte, ▶ la prossima). Le annotazioni
  arrivano ad apexcharts-card come array dalla variabile `EVENTI` di `config-template-card`, che restituisce
  il valore di un template intero (`"${EVENTI}"`) senza convertirlo in stringa; apexcharts-card le unisce
  al segno di *Ora*.
  Il profilo della vista è indipendente da quello caricato nell'ESP32: per la cotta di ottobre la vista usa
  `cerveza_especial` dal 03/10 e `cerveza_especial_ott26` dal 07/10 (il calendario reale: diacetyl rest a 16 °C
  dalle 11:47 del 07/10, freddo lunedì 12/10 09:30, scarichi e imbottigliamento spostati di conseguenza),
  mentre l'ESP32 in pausa ha ancora in NVS `czech_pils_W34`, e *Riprendi* ripartirebbe da quello.

  **Dal 2026-10-17 F1 e F2 si dividono la vista** (venerdì 23/10 entra in produzione una seconda cotta). La
  lager di ottobre resta con l'Inkbird, non sorvegliato, e con l'iSpindel, e passa in **F2**; l'ESP32 di F1
  (sonda DS18B20 e relè) passa alla birra nuova.
  - *F2 Cotta* (`/f2-cotta/cotta`): la vista Cotta com'era, con gli helper `f2_cotta_*` (inizio 02/10 17:38,
    profilo `cerveza_especial_ott26`, Inkbird 2 °C), senza la sonda di F1. In cima una card di stato
    dell'iSpindel (temperatura, SG, attenuazione, ultima lettura con i minuti trascorsi, RSSI, batteria, o
    "iSpindel muto da N ore"); dopo la batteria un grafico del segnale (RSSI, linea a −86 dBm, il limite di
    ricezione misurato in sede); in *Dati della cotta* anche l'OG. Nessun allarme: l'utente ha scelto di non
    sorvegliare l'Inkbird. Le statistiche orarie di `sensor.f1_cotta_setpoint_inkbird_storico` sono state
    copiate su `sensor.f2_cotta_setpoint_inkbird_storico` (WebSocket `recorder/import_statistics`), correggendo
    il freddo: 2 °C dal 13/10 ~12:15 CEST, quando la parete è crollata (l'helper era stato aggiornato solo il
    15/10 alle 16:19). Lo storico di F1 è rimasto com'era.
  - *F1 Laboratorio → Cotta*: temperatura della sonda F1, setpoint dell'ESP32 (tratteggiato: coincide col piano
    se il profilo è lo stesso), piano e Inkbird (se usato); niente densità né batteria; il piano completo ha
    la sola sonda F1. Senza profilo dice "Nessuna cotta in F1". L'automazione `f1_cotta_segui_avvio` copia
    ora di inizio e nome del file negli helper `f1_cotta_*` (e azzera l'Inkbird) quando lo stato HA passa da
    idle a in_corso in Produzione con profilo attivo = profilo scelto, cioè all'avvio di un profilo da file:
    non con Riprendi, temperatura costante, esperimento a gradino o collaudo (provato il 17/10 simulando i
    due casi, senza comandi all'ESP32). Al 17/10 gli helper di F1 sono vuoti (inizio 23/10 00:00).
  - Le funzioni JS dei grafici (griglia, piano, date) sono copie identiche nelle due dashboard: se ne correggi
    una, correggi anche l'altra.
  - **Letture manuali della densità** (dal 2026-10-26, solo nella dashboard *F1 Cotta*; il pacchetto generato
    per F2 ha le stesse entità `f2_cotta_*`, ancora senza interfaccia). *Registra una lettura* apre il dialogo
    dello script `f1_cotta_registra_lettura`: SG come letta, strumento, "È l'OG", momento del prelievo (vuoto =
    adesso), temperatura del campione, nota. Lo script manda l'evento `f1_cotta_lettura` al registro
    `sensor.f1_cotta_letture`, un sensore a trigger che tiene le letture grezze nell'ordine di registrazione
    (sopravvive a `template.reload` e ai riavvii). `sensor.f1_cotta_densita` le corregge e le rifà a ogni
    cambiamento del registro o dell'inizio della cotta. Usa le letture da 24 h prima dell'inizio in poi.
    - Rifrattometro: l'OG come mosto, col Brix diviso per il fattore del mosto 1,04. Le altre letture anche per
      l'alcol, con la formula cubica di Sean Terrill e l'ultima OG registrata.
    - Densimetro: riportato a 20 °C, con la stessa formula dell'app di taratura dell'iSpindel.
    - Valore già corretto: invariato.

    Senza OG, le letture del rifrattometro restano senza SG corretta e la tabella lo segnala. Niente helper
    nuovi: `input_number.reload` e `input_select.reload` riporterebbero al valore `initial` gli helper che ce
    l'hanno, anche a cotta in corso. Gli script e i template invece si ricaricano senza effetti collaterali.
  - Le card markdown renderizzano i template in modo *strict*: un attributo mancante, o un `x if cond` senza
    `else`, fa fallire la card senza messaggio (l'errore è solo nel log). `/api/template` non è strict.
    Prova i template con `render_template` via WebSocket con `strict: true`.
  - Il 17/10 F1 ha ricevuto lo **stop completo** (`script.f1_v6_stop_fermentazione`, ack `f1-stop`): il
    `czech_pils_W34` in pausa non c'è più, lo stato HA è idle e la vista Fermentazione mostra *Avvia*, non
    *Riprendi*. Relè già spenti, sonda ancora sulla parete della lager fino al 23/10.
- *Laboratorio*: spiegazione dal vivo dell'algoritmo attivo con i numeri correnti (On/Off, isteresi, P,
  PI, PID); grafico di temperatura, setpoint, soglie SP±h e relè; grafico di P, I, D e uscita; metriche
  dell'algoritmo (oscillazione, accensioni, duty) con grafico di 12 ore; in Didattica preset e parametri con
  *Applica al fermentatore*; parametri attivi sempre in sola lettura.
- *Esperimenti*: gradino di setpoint con metriche (sovraelongazione, assestamento ±0,2 °C per 30 min,
  errore medio e a regime, commutazioni, accensioni e duty per relè, oscillazione), percorso didattico,
  pagella degli algoritmi, registro dei risultati.
- *Fermentazione*: avvio (nascosto in Collaudo), pausa, stop con conferma, grafico di temperatura e densità
  su 7 giorni, profilo, allarme di scostamento, email di prova.
- *Diagnostica*: collegamento, sensore, sicurezze del firmware, statistiche delle 24 ore, riavvio.

**iSpindel (dal 2026-09-29 al 2026-10-17 in F1).** Fino al 17/10 i grafici di temperatura (Panoramica 12 h,
Laboratorio 6 h, Esperimenti 24 h, Fermentazione 7 giorni) mostravano anche `sensor.ispindel_01_temperatura`
(tratteggiata) e `sensor.ispindel_01_densita` sull'asse di destra. Dal 17/10 l'iSpindel è nella lager in F2
e le serie sono state tolte (il grafico della Fermentazione si chiama ora "Temperatura — 7 giorni"); la card
della Panoramica con SG, attenuazione e differenza dalla sonda F1 resta, ma compare solo con
`input_boolean.f1_ispindel_presente` acceso (si accende in *Cotta → Dati della cotta*). Se un iSpindel torna
nella birra di F1, accendere quell'interruttore e rimettere le serie nei grafici. Le entità vengono dal pacchetto
`ispindel_01.yaml` del progetto DensityMeter, dove si trova anche la dashboard di taratura. Una densità fuori
da 0,980–1,150 (iSpindel non tarato) non viene disegnata.

Le due sonde non misurano la stessa cosa. Il DS18B20 di F1 scende di circa 0,8 °C pochi minuti dopo ogni
passata della pompa, quindi sente il liquido raffreddato dalla camicia. L'iSpindel galleggia e misura lo
strato di superficie attraverso il suo tubo, con una costante di tempo di circa 12-15 minuti. Nell'acqua
ferma, o nella birra senza fermentazione attiva, il liquido si stratifica: l'acqua fredda scende lungo la
camicia e quella calda resta in superficie. Il 29/09, con l'acqua a setpoint 12 °C in una stanza a circa
25 °C, l'iSpindel segnava 2,2-2,8 °C più della sonda. Quanta parte di questa differenza sia stratificazione, e
quanta sia l'errore della sonda dell'iSpindel o il calore della sua elettronica (con l'intervallo di invio a
60 s), non è ancora stato separato. Durante la fermentazione tumultuosa la CO₂ rimescola la birra, quindi la
differenza dovrebbe scendere a pochi decimi di grado.

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
2. Il file del profilo deve essere in `/config/profili_fermentazione/` e tra le opzioni di
   `input_select.f1_v6_profilo_scelto` (`configuration.yaml`; senza riavvio: `input_select.set_options`, mai
   `input_select.reload`).
3. Vista Fermentazione: scegliere il profilo (o la temperatura costante) e premere **Avvia**.
4. Verificare nella stessa vista l'ack "profile_set: ok" e lo stato RUNNING; nella vista Cotta inizio e
   profilo si compilano da soli (solo profili da file).
5. Se la sonda F1 non è nella birra (per esempio sulla parete), ricordarlo leggendo lo scarto nei grafici.

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

**Lezioni del 2026-10-03 (vista Cotta)**
- `input_select.reload` riporta `input_select.f1_v6_stato_controllo` al suo `initial: idle` in
  `configuration.yaml`: con una fermentazione in pausa o in corso HA perderebbe lo stato. Per questo il
  profilo del piano è un `input_text`.
- `command_line` non è caricato nel Lab: un pacchetto che lo introduce richiede un riavvio. `file.read_file`
  rifiuta `/config/profili_fermentazione` (non è in `allowlist_external_dirs`). `shell_command` invece è
  caricato e ha `shell_command.reload`: il pacchetto `_cotta` lo usa dentro un sensore template a trigger.
- Il `pattern` di un `input_text` diventa una regex JavaScript con il flag `v`: il trattino in una classe
  va scritto `\-`.

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
| `7a638f66` | 2026-10-03 | vista Cotta (pacchetto `_cotta`, dashboard; la copia `.bak_20261003` sul Lab è stata sovrascritta dal deploy successivo: la dashboard di prima della Cotta sta solo in questo backup) |
| `4e28c78e` | 2026-10-03 | batteria dell'iSpindel nella vista Cotta (copia `f1_v6_laboratorio_dashboard.yaml.bak_20261003_batteria`) |
| `57cf69f4` | 2026-10-03 | profilo `cerveza_especial` nelle opzioni di `f1_v6_profilo_scelto`/`f2_v6_profilo_scelto` (copia `configuration.yaml.bak_20261003_cerveza`; opzioni applicate con `input_select.set_options`, senza reload) |
| `63ea2e2f` | 2026-10-03 | date chiave della cotta nella vista Cotta (attributo `eventi` del piano; copie `fermentatore_v6_f1_cotta.yaml.bak_20261003_eventi` e `f1_v6_laboratorio_dashboard.yaml.bak_20261003_eventi` in `/config`) |
| `00a9d42f` | 2026-10-05 | griglia dell'asse x nei grafici della vista Cotta (linee ogni 3-24 ore e ore sull'asse, in base alla larghezza; copia `f1_v6_laboratorio_dashboard.yaml.bak_20261005_griglia`). Poi la scelta del passo è passata a un evento `mounted`/`updated` di ApexCharts, che la rifà a ogni ridimensionamento (copia della prima versione: `.bak_20261005_griglia_v1`) |
| — | 2026-10-07 | piano completo della vista Cotta: densità sul secondo asse, via la sonda F1, griglia e date giornaliere/settimanali (copia `f1_v6_laboratorio_dashboard.yaml.bak_20261007_piano`; backup completo più recente `bd30647a`, stesso giorno) |
| — | 2026-10-07 | OG/FG obiettivo e attenuazione nel grafico della densità; attributo `fg_obiettivo` nel pacchetto `_cotta` (ricaricati solo i template). Copie `fermentatore_v6_f1_cotta.yaml.bak_20261007_fg`, `f1_v6_laboratorio_dashboard.yaml.bak_20261007_fg`, `cerveza_especial_ott26.json.bak_20261007_fg` in `/config` |
| `bd30647a` | 2026-10-07 | prima della taratura quadratica dell'iSpindel e della riscrittura dello storico; poi profilo `cerveza_especial_ott26` in `/config/profili_fermentazione` e `input_text.f1_cotta_profilo` puntato su di esso |
| — | 2026-10-08 | piano completo della vista Cotta: tolta l'etichetta "Ora" della linea di adesso, che copriva le curve (copia `f1_v6_laboratorio_dashboard.yaml.bak_20261008_ora`) |
| `31170f1d` | 2026-10-23 | profilo `witbier_WB06` in `/config/profili_fermentazione` e fra le opzioni di `f1_v6_profilo_scelto`/`f2_v6_profilo_scelto` (`input_select.reload`, stati invariati) |
| `93664144` | 2026-10-23 | dashboard **F1 Cotta** (`f1_v6_cotta_dashboard.yaml`, registrata in configuration.yaml), copia `configuration.yaml.bak_20261023`. Riavvio di HA |
| `829755f1` | 2026-10-23 | dashboard **Chiller condiviso** (`chiller_condiviso_dashboard.yaml`, registrata in configuration.yaml); prima, nel piano completo delle due dashboard F1 la sonda passa alle statistiche a 5 minuti (copie `.bak_20261023`). Riavvio di HA |
| `f6bd7905` | 2026-10-26 | letture manuali della densità (pacchetti `_f1_cotta`/`_f2_cotta`, dashboard F1 Cotta; copie `.bak_20261026_letture` in `/config`). Solo `script.reload` e `template.reload`, nessun riavvio |
| `f608e0ba` | 2026-10-28 | sonde del nodo sensori V7 nelle dashboard Chiller condiviso, F2 Cotta, F1 Cotta e F1 Laboratorio (copie `.bak_20261028_sonde_v7` in `/config`). Dashboard YAML, nessun riavvio. Poi storico e statistiche delle tre `sensor.v7_temperatura_*` cancellati fino alle 12:35 (`recorder.purge_entities`, `recorder/clear_statistics`): erano letture dal banco, prima del montaggio |
| `5c64ec88` | 2026-10-17 | la lager passa in F2 (dashboard F2 Cotta, pacchetti `_f2_cotta`, `_f2_cotta_ispindel`), F1 senza iSpindel, `f1_ispindel_presente`, automazione `f1_cotta_segui_avvio`, stop completo di F1. Copie `configuration.yaml.bak_20261017`, `f1_v6_laboratorio_dashboard.yaml.bak_20261017`, `fermentatore_v6_f1_cotta.yaml.bak_20261017`, `fermentatore_v6_f1_ispindel.yaml.bak_20261017` in `/config`. Riavvio di HA (nuova dashboard e `shell_command`) |
| `df9353e4` | 2026-10-30 | dashboard **Storico birre** (`storico_birre_dashboard.yaml`, registrata in configuration.yaml, copia `configuration.yaml.bak_20261030_storico`), pacchetto `storico_birre.yaml`, `scripts_py/storico_birre.py`, schede della lager e della witbier in `/config/storico_birre/`. Riavvio di HA (nuova dashboard e `shell_command`) |
| `7bff867a` | 2026-10-30 | pulizia della barra laterale. Disinstallati Studio Code Server, Node-RED (solo i flussi del vecchio F1 v3, su entità che non esistono più; ultimo uso 27/04; `flows.json` copiato anche in `FermentaLab_backup_2026-10-30/node-red/` sul PC) e wmbusmeters (in errore continuo, nessuna entità). Pannelli tolti per tutti (`ingress_panel: false`, add-on attivi): InfluxDB, Tailscale, Terminal. Per l'utente Simone Lab nascosti Overview, Mappa, Energia, Attività, Storico, HACS, Media, Liste (`frontend/set_user_data`, chiave `sidebar`). Pulsanti di F1 Cotta e Storico birre da `button` a `tile` in riga (copie `.bak_20261030_pulsanti`) |

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
