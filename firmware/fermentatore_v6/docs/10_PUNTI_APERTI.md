# 10. Punti aperti e idee — V6 (F1 e F2)

**Versione documento**: 1.4 (2026-09-19) · **Stato**: elenco vivo — aggiungere qui, non in chat, ogni
volta che emerge qualcosa da fare o da valutare, con la data.

Riferimenti: [00_INDICE.md](00_INDICE.md) (indice di tutti i documenti V6),
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md), [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md),
[11_TIMING_PROFILO.md](11_TIMING_PROFILO.md).

## 0. Timing del profilo — implementato in firmware 6.4.0 (2026-09-19)

Analisi in [11_TIMING_PROFILO.md](11_TIMING_PROFILO.md): fino alla 6.3.0 il "clock" del profilo era
un epoch assoluto che, dopo un blackout senza rete durante una rampa, si metteva in pausa e poi
**saltava in avanti** non appena l'orologio si risincronizzava — invece di riprendere gradualmente.
Un guasto del sensore durante una rampa aveva un effetto simile (il setpoint pianificato avanzava
comunque, i relè restavano spenti). Su un batch di 45-48 giorni con rampe di 1-5 giorni, non sono
casi rari — non ancora sul dispositivo reale (nessun accesso a F1 prima di lunedì), ma scritto,
validato con i test host e col simulatore offline, pronto per il flash.

- [x] **Opzione A implementata** (firmware 6.4.0, non ancora flashato): il profilo avanza con un
  contatore locale (`g_profilo.secondi_trascorsi`), incrementato solo mentre il controllo è
  davvero in corso (RUNNING e sensore utilizzabile), persistito ogni 10 minuti più un checkpoint
  immediato prima di un riavvio volontario. Nessuna dipendenza da NTP per l'esecuzione del
  profilo. 3 nuovi scenari nei test host (`profilo_pausa_blackout_a/b`, `profilo_pausa_sensore`),
  tutti i 21 scenari superati; simulatore aggiornato allo stesso modello. Vedi
  [11_TIMING_PROFILO.md §4.1](11_TIMING_PROFILO.md#41-opzione-a-tempo-del-profilo-locale-accumulato-e-pausabile-non-più-un-epoch-assoluto).
- [x] **Opzione C (rate-limiter sul setpoint) valutata e scartata**: durante l'implementazione è
  emerso che frenerebbe anche i cambi di temperatura *intenzionali* e già in uso — il gradino
  dell'esperimento didattico e i cambi rapidi di setpoint del collaudo stesso (`costante()`,
  `TestModo`). Con l'opzione A il salto che doveva attenuare non può più accadere: non serve.
  Vedi [11_TIMING_PROFILO.md §4.3](11_TIMING_PROFILO.md#43-opzione-c-limitare-la-velocità-con-cui-il-setpoint-può-cambiare-rate-limiter).
- [x] **Portare `profili_fermentazione/*.json` dal Lab al repository** — fatto il 2026-09-19, ora in
  [`../profili/`](../profili/) (checksum verificato uguale all'originale sul Lab). Resta da
  automatizzare la sincronizzazione se le ricette cambiano da un lato o dall'altro (oggi manuale).
  Vedi [11_TIMING_PROFILO.md §5.2](11_TIMING_PROFILO.md#52-quello-che-non-è-locale-il-catalogo-delle-curve-stesse).
- [ ] Aggiungere il test `corrente_in_rampa` al collaudo (blackout durante una rampa, con un
  profilo "onda" veloce): non richiede firmware nuovo, si può fare prima di lunedì. Vedi
  [11_TIMING_PROFILO.md §6.2](11_TIMING_PROFILO.md#62-test-proposto-blackout-durante-una-rampa-corrente_in_rampa).
- [ ] Quando arrivano le funzioni di collaudo 6.5.0 per il sensore (§3), estendere
  `sensore_breve`/`sensore_lungo` per esercitare anche il caso rampa, non solo la tenuta. Vedi
  [11_TIMING_PROFILO.md §6.3](11_TIMING_PROFILO.md#63-test-proposto-guasto-del-sensore-durante-una-rampa-estensione-di-sensore_brevesensore_lungo).
- [ ] Flash di 6.4.1 (= 6.4.0 + contatori del sensore) e ripetizione *selettiva* dei test alla luce del nuovo setup, al prossimo accesso a F1 — vedi §1.

## 1. Prima del 3 ottobre (bloccanti, solo F1)

- [ ] **Guasto hardware: reset dell'ESP32 alla commutazione della pompa** (pompa 220 V, 10-15 W, sulla
  scheda relè). Osservato 4 volte in ~30 commutazioni, sempre 0,7-4,7 s dopo lo spegnimento. Utente sta
  provando un condensatore tra EN e GND. Se non basta: snubber RC (100 Ω + 100 nF X2) o varistore sulla
  pompa; alimentazione separata per il modulo relè. **Verifica**: rilanciare il test `stress_pompa` (circa
  1 ora) dopo ogni intervento. Vedi [06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630).
- [ ] **Concludere la Fase A su 6.3.0** (in corso dal 2026-09-19 13:47, fine stimata martedì 22/09
  notte): `diacetile`, `rampa`, i quattro modi di controllo, `cold_crash`, `allarme_freddo`. Si
  interrompe comunque al flash di lunedì: i test non finiti si rilanciano dopo.
- [ ] **Dopo il flash di 6.4.1 e l'intervento hardware, ripetere solo alcuni test, non tutta la Fase
  A**: `riavvio`, `rampa`, `stress_pompa` (subito dopo l'intervento), `corrente` nella variante in
  rampa, `lager` come prova rapida — motivazioni test per test in
  [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md#quali-test-ripetere-dopo-il-flash-della-640-e-lintervento-hardware).
  Il profilo attivo su F1 va perso al primo boot in 6.4.0 (layout NVS cambiato, vedi §0), come previsto
  quando cambia il blob (`profilo.h`).
- [ ] **Scrivere il firmware 6.5.0** con le funzioni di collaudo (vedi §3) — non blocca il 3 ottobre,
  la 6.4.0 (timing del profilo, pronta) può essere flashata prima: il codice dei test B
  (`TestFirmware64` in `collaudo_motore.py`) è già scritto come segnaposto, con descrizione e criteri,
  ma restituisce sempre "non applicabile" finché il firmware installato non è almeno 6.5.0.
- [ ] **Test manuale della mancanza di corrente**: va eseguito durante la stessa visita (serve comunque la
  presenza per il flash). Ricordarsi di avviarlo dalla dashboard *prima* di intervenire, altrimenti l'app
  non registra l'evento.
- [ ] Decidere se il verdetto "pronto per la birra" deve essere dato da tutti i test A+B, o se un
  sottoinsieme è sufficiente il 3 ottobre e il resto si completa dopo (per esempio: i test dei modi di
  controllo non sono indispensabili se in produzione si userà solo on/off).

## 2. Secondo fermentatore F2 — guida di onboarding da scrivere

**F1 e F2 saranno due fermentatori con controllo separato, entrambi gestiti da Home Assistant.** Oggi
F2 esiste solo come identità software (`fermenter_id = f2`, topic MQTT, pacchetto
`fermentatore_v6_f2_allarmi.yaml` già generato e installato nel Lab): **non ha ancora hardware**. A
differenza di un secondo F1 "clonato", per F2 **anche la scheda elettronica del termostato va
progettata e costruita da zero** — non solo flashata. Serve una guida di onboarding dedicata (nuovo
documento, per esempio `11_ONBOARDING_F2.md`) che copra almeno:

- [x] **Decisione 2026-09-19**: F2 su **scheda indipendente** (clone hardware di F1) con una seconda sonda per
  il bagno di glicole, solo telemetria; rimedio al reset = condensatore EN–GND come F1. La scheda unica F1+F2
  ([12](12_HARDWARE_DUAL_F1_F2.md)) è stata valutata e non adottata. Piano, pin map, impatto su firmware e HA:
  [13_F2_SCHEDA_INDIPENDENTE.md](13_F2_SCHEDA_INDIPENDENTE.md). Le voci qui sotto restano valide, con F2 = F1 più la
  sonda del glicole (in particolare: pin di F1 invariati, `wroom-f2` senza override).
- [ ] **Elettronica da zero**: schema elettrico completo (non solo la pin map di
  [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md)), scelta e distinta base dei componenti
  (ESP32, DS18B20, modulo relè 2 canali, OLED, alimentatore, morsettiera), dimensionamento
  dell'alimentazione per relè + pompa + resistenza, e — alla luce del guasto trovato su F1 (§1) —
  prevedere fin da subito la protezione contro i disturbi di commutazione (snubber sul carico
  induttivo, condensatore su EN, alimentazione separata ESP32/relè) invece di scoprirla dopo.
- [ ] **Fermentatore fisico**: camicia a glicole o altro sistema di raffreddamento/riscaldamento,
  equivalente a quello di F1 (pompa + resistenza), da costruire o approvvigionare.
- [ ] **Provisioning firmware**: uso reale di `configure_target.ps1 -TargetId wroom-f2` (o `s3-f2`),
  `secrets.h` proprio per F2 (credenziali WiFi/MQTT/OTA — mai copiare quello di F1 se le password
  variano per MAC), primo flash via USB, verifica del namespace MQTT `fermentazione/v6/f2/...`
  separato da F1. Nota: il firmware brief originale ipotizzava un provisioning `fermenter_id` via NVS
  a runtime — quello che si usa davvero è `DEVICE_FERMENTER_ID` in `config.h`, impostato a
  compile-time dal configuratore (vedi [01_PROGETTO_INIZIALE_E_BACKLOG.md §7](01_PROGETTO_INIZIALE_E_BACKLOG.md#7-scostamenti-tra-il-piano-e-la-realtà)).
- [ ] **Home Assistant**: installare `fermentatore_v6_f2_entita.yaml` (generato, non ancora
  installato nel Lab — oggi c'è solo `_allarmi`) e decidere se F2 avrà anche i pacchetti
  `_laboratorio`/`_collaudo` (oggi generati/scritti solo per F1: `_collaudo` è escluso di proposito da
  `genera_f2.py`, vedi [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md) e §4 sotto).
- [ ] **Dashboard**: F1 ha "F1 Laboratorio" e "F1 Collaudo" dedicate; decidere se F2 avrà le sue
  dashboard analoghe, una dashboard multi-fermentatore condivisa, o entrambe.
- [ ] **Collaudo**: il motore di collaudo (`collaudo_motore.py`) oggi ha gli `entity_id` di F1 scritti
  nel dizionario `E{}`: per collaudare F2 serve parametrizzarlo per fermentatore (non è un lavoro
  grande, ma va fatto prima di poter riusare la suite su F2).
- [ ] **Criterio di accettazione**: la stessa checklist di collaudo di F1 (o una versione aggiornata)
  va superata anche da F2 prima di mettere birra nel secondo fermentatore.

## 3. Firmware 6.5.0 — funzioni di collaudo da implementare

Serve un comando o una modalità che duri un tempo limitato e scada da sola (mai lasciare F1 permanentemente
più fragile). Proposta: comandi `ha/cmd/test_*` accettati solo con `input_select.f1_v6_modalita = Collaudo`
lato firmware (o un flag equivalente), con timeout massimo e log distinguibile dai guasti reali.

- [x] **Contatori del sensore** (firmware 6.4.1, 2026-09-20): letture, scartate per causa, "vicino ai relè"
  (entro 5 s da una commutazione) e power-cycle, in `esp/state.sensore_diag` ed entità HA
  `sensor.f1_v6_sensore_*`. **Restano da fare**: la logica del test `salute_sensore` nel motore (differenza
  dei contatori tra inizio e fine sessione, criterio ≤ 0,1 %, nessuna persa vicino ai relè) e la distinzione
  CRC / bus vuoto / timeout, che la libreria DallasTemperature non permette (restituisce sempre -127).
- [ ] **Lettura forzata del sensore subito dopo ogni commutazione dei relè**: per il test
  `stress_rele`, che deve rilevare non solo i riavvii dell'ESP32 (già visibili dall'uptime) ma anche letture
  del sensore perse o corrotte senza reset.
- [ ] **Comando che taglia davvero l'alimentazione del sensore** (GPIO25) per un tempo dato, bypassando la
  logica di power-cycle automatico: serve a `sensore_breve` (60 s) e `sensore_lungo` (8 min) per verificare
  fail-safe dei relè, allarme, email e ripresa, senza aspettare un guasto vero.
- [ ] **Comando che disconnette il WiFi** per un tempo dato mantenendo il loop di controllo attivo: serve a
  `wifi`, per verificare che la regolazione continui in autonomia e che HA segnali l'assenza.
- [ ] **Comando che blocca il loop** (o innesca deliberatamente il watchdog) per verificare che il task
  watchdog di 60 s riavvii l'ESP32 e che la ripresa sia pulita: serve a `watchdog`.
- [ ] Aggiornare [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md) con i nuovi comandi/campi quando sono
  definiti, come fatto per `esp/controllo` nella 6.3.0 e per `secondi_trascorsi` nella 6.4.0.
- [ ] Nota di sicurezza da rispettare in ogni comando di test: mai disabilitare le sicurezze esistenti
  (relè spenti su sensore stale, limiti assoluti, tempi minimi dei relè) mentre si simula il guasto.

## 4. Collaudo — miglioramenti al motore

- [ ] **Notifica di avanzamento**: oggi si sa a che punto è il collaudo solo aprendo la dashboard. Con
  sessioni di 1-2 giorni potrebbe avere senso una notifica push o un digest email quando cambia test, o a
  metà sessione.
- [ ] **Raggruppare le email dei riavvii spontanei** se diventano frequenti (oggi una email per ogni
  riavvio): un digest orario se superano una soglia, per non intasare la posta durante un guasto ricorrente.
- [ ] **Storico dei collaudi**: oggi `sensor.f1_collaudo_<test>` tiene solo l'ultimo risultato di ogni test.
  Se si ripete il collaudo dopo una modifica hardware/firmware, sarebbe utile un confronto (per esempio nel
  registro attività, già presente, ma non strutturato per confronti).
- [ ] **Parametrizzare le temperature dei test** dalla dashboard invece che nel codice (oggi 10 °C per il
  lager, 17 °C per il diacetile, 2 °C per il cold crash sono fissi in `collaudo_motore.py`): utile quando si
  cambierà tipo di birra o si vorrà provare temperature diverse senza toccare il codice.
- [ ] **Generalizzare il motore per più fermentatori** (F1/F2): oggi gli `entity_id` di F1 sono scritti nel
  motore. Prerequisito per collaudare F2, vedi §2.
- [ ] **Test di lunga durata** (soak test 24-48h, previsto in
  [01_PROGETTO_INIZIALE_E_BACKLOG.md](01_PROGETTO_INIZIALE_E_BACKLOG.md), voce V6-042) non ancora coperto
  dal collaudo automatico oltre ai 3 giorni già usati per la Fase A: da considerare come test aggiuntivo.

## 5. Sicurezza in Home Assistant

- [ ] `select.f1_v6_control_mode` e i pulsanti `button.f1_v6_start/stop/reboot` in `mqtt.yaml` mandano
  comandi diretti a F1 e **aggirano il blocco della modalità Produzione**. Non sono su nessuna dashboard ma
  restano raggiungibili (Impostazioni → Entità, o API). Valutare se rimuoverli o nasconderli
  (`entity_category: diagnostic` non basta a bloccarli).
- [ ] Il blocco delle modalità è interamente lato script HA: un comando MQTT pubblicato a mano (per
  esempio da un test, da un altro automatismo, o da uno strumento esterno collegato allo stesso broker)
  raggiunge comunque F1. Accettabile per un banco di prova, da rivedere se F1 diventasse produzione reale
  con più persone in HA.

## 6. Dashboard e didattica

- [ ] **Rimuovere la vecchia dashboard "F1 V6"** dopo che l'utente ha validato "F1 Laboratorio" per un
  periodo d'uso reale (rimandato più volte, ancora presente nel Lab).
- [ ] Alcuni preset (`PI mal tarato`, `On/Off puro`) non sono mai stati provati dal vivo su F1, solo in
  simulazione: potrebbe valere la pena eseguirli una volta con la dashboard Laboratorio a fermentatore
  vuoto, per la parte didattica.
- [ ] Estendere le viste Esperimenti con un confronto affiancato tra due esperimenti (oggi mostra solo
  l'ultimo).

## 7. Repository e processo

- [ ] Valutare quando unire `v6.3.0-didattica` in `main` (stato al 2026-09-19: `main` fermo a `41599db`,
  `v6.3.0-didattica` avanti di vari commit su didattica, collaudo e riordino della documentazione).
- [ ] I file generati (`fermentatore_v6_f2_*.yaml`, `f1_v6_collaudo_dashboard.yaml`) sono committati:
  ricordarsi di rigenerarli (`genera_f2.py`, `genera_collaudo.py`) a ogni modifica dei sorgenti, altrimenti
  il file nel Lab diverge da quello nel repository.
- [ ] Nessun MCP server (github, slack, ecc.) è attualmente autorizzato in questa sessione: da attivare via
  connettori claude.ai o `/mcp` se si vuole automatizzare PR o notifiche.
- [ ] **`v6_manifest.json` disallineato dal firmware reale** (dichiara `firmware_version: 6.1.4`, il
  firmware reale è 6.3.0): la toolchain `generate_build_info.ps1`/`verify_build_info.ps1` non legge né
  scrive `config.h`, quindi non se ne accorge. Decidere se: (a) aggiornare il manifest a ogni bump di
  versione, (b) far leggere/scrivere davvero `config.h` a questi script, o (c) rimuovere questa toolchain
  se il processo manuale (CHANGELOG → `config.h`/`display.cpp`/`connettivita.cpp`) basta. Vedi
  [09_TOOLING.md](09_TOOLING.md).
- [ ] Il `CHANGELOG.md` nella radice del repository (schema `v0.0.x`) parla di file che non esistono più
  con quei nomi (`HASS_CONFIGURATORE_V6.md`, `HASS_MONITORAGGIO_V6.md`) e non è mai stato aggiornato dopo
  il 2026-05-25: valutare se archiviarlo o riscriverlo per riflettere lo stato attuale (il changelog vivo
  è `fermentatore_v6/CHANGELOG.md`).
- [ ] `README.md` in radice descrive ancora v4/v5 come sistema principale, con topic MQTT dello schema
  vecchio (`fermentazione/f1/...` invece di `fermentazione/v6/f1/...`). È stato aggiornato il minimo
  indispensabile (percorsi dopo lo spostamento in `obsolete/`, link rotti) il 2026-09-19: una revisione
  completa che lo centri su V6 resta da fare.

## 8. Idee non urgenti

- [ ] Grafico Grafana/InfluxDB per l'archivio a lungo termine (l'add-on InfluxDB è già installato e attivo
  nel Lab, non ancora usato per F1).
- [ ] Notifica push (invece che solo email) per gli allarmi critici (sensore guasto, offline): da valutare
  quando F1 ospiterà birra vera.
- [ ] Un secondo livello di allarme "critico" (temperatura molto fuori range, non solo oltre soglia) con
  canale diverso, se in futuro si vorrà distinguere l'urgenza.
