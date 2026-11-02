# Fermentatore V7: sensori e attuatori separati

Avviata il 2026-10-27, **in uso su F1 dal 2026-10-28**: nodo sensori e nodo attuatori 7.0.0, con la witbier
in F1 e la sonda F2 nella lager. La V6
(`firmware/fermentatore_v6/`) resta il riferimento per documentazione, contratto MQTT, Home Assistant, profili
e collaudo, e la 6.5.1 è il ritorno indietro.

## Perché

Con la V6 una sola scheda ESP32 per fermentatore legge la sonda e comanda i relè. Due problemi visti su F1:

- **Reset del 2026-09-19**: l'ESP32 si resettava allo spegnimento del relè della pompa, per i disturbi della
  commutazione sull'alimentazione.
- **Sonda persa**: il DS18B20 si è perso più volte (01/10, 25-26/10, 27/10). Il 26-27/10 la regolazione
  stimata, ripartita a ogni perdita con la pompa accesa, ha portato la witbier da 20,5 a 12,1 °C.

La V7 tiene le misure lontane dai carichi a 230 V e mette tre sonde su una scheda sola.

| Nodo | Scheda | Cartella | Compito |
|---|---|---|---|
| **Sensori** | ESP32-S3 Super Mini | `nodo_sensori_s3/` | Legge 3 DS18B20 (F1, chiller, F2) e le pubblica via MQTT ogni 5 s. Recupera da sé le sonde perse. Nessun relè. Alimentazione USB sua, lontana dai relè |
| **Attuatori F1** | ESP32 WROOM di F1 con la scheda relè | `nodo_attuatori/` | La 6.5.1 con la temperatura ricevuta dal nodo sensori: profilo, regolazione, sicurezze, regolazione stimata e contratto MQTT/HA invariati |

La regolazione resta nel nodo attuatori, che è autonomo: legge `temp/f1` dal broker, senza passare da Home
Assistant. Quando la temperatura manca si comporta come la 6.5.1 con la sonda persa (relè spenti, poi
regolazione stimata per al massimo 72 h), ma **senza power-cycle e senza riavvii**: la sonda è sul nodo sensori.

Più avanti il nodo attuatori pilota anche F2 (pompa F2 e cintura C2 sulla stessa scheda relè): non è in
questa versione.

## Stato

| Fase | Cosa | Stato |
|---|---|---|
| 1 | Nodo sensori: WiFi, MQTT, ping di prova, dashboard `V7 Nodo sensori` | fatto il 27/10 (`7.0.0-ping` sulla S3 n. 3) |
| 2 | Nodo sensori: lettura delle 3 sonde, recupero, OTA | `7.0.0` sulla S3 n. 1 dal 28/10, sonde montate dalle 12:11 |
| 3 | Nodo attuatori F1 con temperatura remota | `7.0.0` sul WROOM di F1 dal 28/10 11:53 (OTA), 60 scenari host superati |
| 3b | Dashboard v6 con le sonde nuove (Chiller condiviso, F2 Cotta, F1 Cotta, F1 Laboratorio) | fatto il 28/10 (vedi `docs/05_HOME_ASSISTANT_F1.md` della v6) |
| 4 | Nodo attuatori a 4 relè (F1 + F2) | da fare |

## Nodo sensori (`nodo_sensori_s3/`, 7.0.0)

- **Sonde**: un bus OneWire per sonda, VDD da un GPIO, una lettura ogni 5 s, sfasate tra loro. Letture scartate
  per le stesse cause della V6: -127/NaN (scollegato), 85 °C (sonda appena riaccesa), fuori scala (-10…40 °C
  per i fermentatori, -25…40 °C per il chiller).
- **Recupero di una sonda persa** (nessuna lettura valida da 20 s), come la 6.5.1: power-cycle con VDD e DQ a
  massa per 3 s, dopo 2 min, poi ogni 2 min per tre volte, poi ogni 10 min senza limite. Non blocca le altre sonde.
- **Riavvio del nodo**: una sonda che funzionava ed è persa da 10 min fa riavviare la S3, che così toglie
  corrente a tutte le sonde. Al massimo 3 riavvii di fila, contati in memoria RTC, poi solo power-cycle. Il
  contatore si azzera all'accensione e dopo 30 min con tutte le sonde a posto. Una sonda mai collegata non fa
  riavviare nulla.
- **OTA come la V6**: all'avvio cerca l'hotspot `HotIoT`. Se lo trova si collega a quello e il PC, sullo stesso
  hotspot, vede la porta di rete `fermentatore-v7-sensori` in Arduino IDE. Se l'hotspot sparisce, dopo 6
  tentativi torna sulla rete `IoT`. Per farle cercare l'hotspot senza toccarla: script HA *Riavvia il nodo*.
- **Comandi** su `cmd/`: `ping`, `riavvia`, `power_cycle` (`{"sonda":"f1"}`).
- Watchdog di 60 s sul loop. LED: blu WiFi, giallo MQTT, verde breve a ogni ping, rosso tenue con una sonda persa.

## Nodo attuatori F1 (`nodo_attuatori/`, 7.0.0)

Copia della 6.5.1 con queste differenze (tutto il resto, compresi topic `fermentazione/v6/f1/...`, client MQTT,
hostname OTA e namespace NVS, è uguale: **il profilo in corso prosegue dopo il flash**):

- `SORGENTE_TEMPERATURA_REMOTA 1` in `config.h`: la temperatura arriva su `fermentazione/v7/sensori/temp/f1`,
  passa per la stessa valutazione del DS18B20 (classificazione, filtro anti-picco, timer di errore) e conta
  come una lettura. Un messaggio con `ts` più vecchio di 60 s (orologi sincronizzati) è scartato. Senza
  letture per 30 s (20 s con la sonda locale) i relè vanno in sicurezza, dopo 5 min parte l'allarme
  `sensore_offline`. Niente power-cycle, niente riavvii.
- **Regolazione stimata**: la fase della finestra di 40 min segue `millis()`, non l'attivazione. Fino alla 6.5.1
  ogni perdita della sonda faceva ripartire la finestra dalla parte "pompa accesa": è il difetto che il 26-27/10
  ha raffreddato la witbier. Lo scenario `stima_intermittente` lo riproduce: con la stima della 6.5.1 la pompa
  sta accesa il 26 % del tempo, con questa il 6 %.
- `esp/state` ha il campo `sorgente_temp` (`remota` / `locale`). Display e log di avvio dicono "V7".
- Con `SORGENTE_TEMPERATURA_REMOTA 0` è la 6.5.1 con la correzione della stima: serve ai test di regressione.

Test host (MSVC): `powershell -File firmware/fermentatore_v7/test_attuatori/run_tests.ps1` compila lo sketch con
la sorgente locale (31 scenari, quelli della 6.5.1 più `stima_intermittente`) e con quella remota (29 scenari, tra
cui `remoto_normale`, `remoto_muto`, `remoto_null`, `remoto_stantio`, `remoto_senza_rete`, `remoto_weekend`).
Il 27/10/2026 sono passati tutti e 60.

## MQTT

Il broker è lo stesso della V6 (HiveMQ Cloud, TLS sulla 8883). Niente MQTT discovery, niente retained tranne lo
stato del nodo.

| Topic | Direzione | Contenuto |
|---|---|---|
| `fermentazione/v7/sensori/stato` | nodo → tutti, retained | `online` / `offline` (LWT) |
| `fermentazione/v7/sensori/temp/<id>` | nodo → attuatori e HA, ogni 5 s | `{"seq","t","ts"}` oppure `{"seq","t":null,"err","ts"}`; `id` = `f1`, `ch`, `f2` |
| `fermentazione/v7/sensori/ping` | nodo → HA, ogni 30 s | rete, firmware, reset, riavvii per sonda e per ogni sonda `t`, `eta_s`, `ok`, `errore`, contatori |
| `fermentazione/v7/sensori/evento` | nodo → HA | `{"tipo":"power_cycle" \| "riavvio" \| "comando_…","sonda","dettaglio"}` |
| `fermentazione/v7/sensori/cmd/ping` · `riavvia` · `power_cycle` | HA → nodo | `{"token": <ms>}` · `{}` · `{"sonda":"f1"}` |
| `fermentazione/v7/sensori/pong` | nodo → HA | `{"token": <stesso>, "uptime_s"}` |

**Mai pubblicare a mano su `temp/f1`** quando il nodo attuatori ha la 7.x: lo prenderebbe per una misura.

Home Assistant: pacchetto `hass_spec/packages/fermentatore_v7_nodo_sensori.yaml` (temperature, ping, eventi nel
registro, script *Ping*, *Riavvia il nodo*, *Power-cycle di una sonda*) e dashboard
`hass_spec/dashboards/v7_nodo_sensori_dashboard.yaml` (viste *Sonde* e *Collegamento*). La dashboard è in modalità
storage (`/v7-sensori`) ed è caricata via WebSocket con `lovelace/config/save`, senza riavviare HA. Nel Lab dal
27/10 (backup `fe65c343` e `b1476a5b`).

## Rete

La rete `IoT` ha una chiave per dispositivo. I firmware scelgono la password dal MAC con
`WIFI_MAC_PASSWORD_MAP` in `secrets.h`, come la V6. La rete `dbAir` è WPA2-Enterprise (802.1X): non si usa.
Ogni scheda nuova va fatta abilitare col suo MAC Wi-Fi, che il firmware stampa sulla seriale all'avvio.
I MAC non stanno nel repository: la mappa MAC → password è in `secrets.h`.

## Compilazione

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled,USBMode=hwcdc,FlashSize=4M,PartitionScheme=default" nodo_sensori_s3
arduino-cli compile --fqbn esp32:esp32:esp32 nodo_attuatori
```

Librerie (come la V6): PubSubClient 2.8, ArduinoJson 6.x, OneWire, DallasTemperature (U8g2 solo per gli
attuatori). `secrets.h` parte da `secrets.h.template` ed è ignorato da git. I binari compilati contengono le
credenziali: stanno fuori dal repository, in `Coding/FermentaLab_bin_7.0.0/`.

## Flash del 2026-10-28

Ordine: prima il nodo sensori con le sonde, poi il nodo attuatori. Così quando F1 passa alla 7.0.0 la sua
temperatura arriva già.

1. **Sonde sulla millefori**, con la S3 spenta (schema `schema_millefori_s3.svg` e accortezze qui sotto).
2. **Nodo sensori**, via USB: Arduino IDE con `nodo_sensori_s3.ino`, scheda *ESP32S3 Dev Module*, USB CDC On Boot
   *Enabled*, PSRAM *QSPI*, Flash 4MB, partizioni *Default 4MB with spiffs*, porta COM della S3 → Carica. In
   alternativa `arduino-cli upload` con l'FQBN qui sopra. Sulla seriale: `[BOOT] Nodo sensori V7 7.0.0`, rete,
   MQTT. Dalla volta dopo si può aggiornare via OTA (hotspot `HotIoT`).
3. **Controllo in HA**: dashboard *V7 Nodo sensori* → *Sonde*. Tre temperature, tutte "✅ ok", F1 coerente con la
   temperatura della birra.
4. **Nodo attuatori F1**, come per la V6: OTA con l'hotspot `HotIoT` acceso e il PC collegato all'hotspot,
   l'ESP32 riavviato perché lo trovi all'avvio, porta `fermentatore-v6-f1` in Arduino IDE, sketch
   `nodo_attuatori.ino`, scheda *ESP32 Dev Module*. Oppure via USB. **Mai il file `merged.bin`** sul WROOM:
   sovrascrive la NVS col profilo. Via OTA o con l'upload normale la NVS resta.
5. **Controllo in HA** (F1 Cotta o F1 Laboratorio): firmware `7.0.0`, `sorgente_temp` = `remota`, sonda ok entro
   ~15 s, stato RUNNING, stesso step del profilo. La vecchia sonda sui GPIO33/25 del WROOM non è più letta.
6. **Ritorno alla 6.5.1** se serve: `Coding/FermentaLab_bin_6.5.1/fermentatore_v6.ino.bin` via OTA o USB (non il
   merged). La NVS è compatibile.

## Collegamento delle sonde al nodo sensori

Ogni sonda ha il suo bus. Un cavo guasto non blocca le altre due, e il firmware riconosce la sonda dal GPIO,
senza leggere l'indirizzo ROM.

Cablaggio reale della millefori (27/10/2026). I numeri delle sonde sono quelli scritti sulle sonde:

| Sonda | Vasca | DQ (dati) | VDD (alimentazione commutata) | GND | Pull-up 4,7 kΩ |
|---|---|---:|---:|---|---|
| sensore 7 | F1 | GPIO6 | GPIO7 | GND | tra GPIO6 e GPIO7 |
| sensore 6 | Chiller (glicole) | GPIO5 | GPIO1 | GND | tra GPIO5 e GPIO1 |
| sensore 5 | F2 | GPIO3 | GPIO4 | GND | tra GPIO3 e GPIO4 |

GPIO3 è un pin di strapping (sorgente del JTAG), ma conta solo con l'eFuse `STRAP_JTAG_SEL` bruciato, che di
fabbrica non lo è: come linea dati va bene. GPIO8 e GPIO9 restano liberi per un eventuale OLED I2C. GPIO48 è il
LED RGB della scheda.

All'avvio il firmware controlla i collegamenti e scrive sulla seriale una riga `[CABLAGGIO]` per sonda. Per la
VDD dice se è in corto verso massa. Per la linea dati dice se il pull-up c'è, se la linea è aperta (manca la
resistenza o il filo) o se è a massa. Ogni 10 s scrive anche il riepilogo delle letture (`[SONDE]`).

`schema_millefori_s3.svg` (generato da `genera_millefori.py`) è il progetto iniziale, con altri pin e i connettori
con la VDD al centro. La millefori costruita segue la tabella qui sopra.

Accortezze:

1. **Solo 3,3 V.** I GPIO della S3 non tollerano 5 V: VDD e pull-up non vanno mai sul pin 5V.
2. **Pull-up sull'alimentazione commutata della sonda, non sul 3V3 fisso.** Con il pull-up sul 3V3 la sonda
   spenta resta alimentata in parassita dalla linea dati e il power-cycle software non la resetta (V6, F1,
   2026-10-02).
3. **Colori dei fili**: di solito rosso = VDD, nero = GND, giallo o bianco = DQ. Verificali sulla scheda del
   venditore prima di saldare: alcune sonde usano altri colori.
4. **Giunzioni** saldate e coperte da guaina termorestringente, con un fermacavo vicino alla scheda. Niente
   morsetti a vite in zona umida.
5. **Cavi lontani dai cavi a 230 V** di pompe e cinture, mai nella stessa guaina. Oltre ~3 m usa un cavo con DQ
   e GND intrecciati, e se le letture sono instabili scendi a un pull-up da 2,2-3,3 kΩ.
6. **Pozzetti asciutti.** Metti un po' di pasta termica sul fondo. La condensa nel pozzetto, con la birra più
   fredda della stanza, è un sospetto per le perdite della sonda di F1.
7. **Sonda del chiller**: immergi tutta la parte in acciaio nel glicole, ma tieni fuori dal liquido la giunzione
   acciaio-cavo, che è il punto da cui entra l'acqua. Fissala in modo che non tocchi le serpentine.
8. **Etichette sui due capi di ogni cavo** (F1, CH, F2). Scambiando i cavi si scambiano le misure.
9. **Alimentazione della S3** da un suo alimentatore USB, non da quello della scheda relè o delle pompe.
10. Facoltativo: un condensatore ceramico da 100 nF tra VDD e GND di ogni sonda, vicino alla scheda.
11. Collega le sonde con la scheda **spenta**.
