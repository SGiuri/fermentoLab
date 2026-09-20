# 12. Scheda dual F1 + F2 con sonda del bagno di glicole — hardware, impatto e migrazione

**Versione documento**: 1.1 (2026-09-19) · **Stato**: alternativa valutata, **non adottata** — progettata su carta,
non costruita né provata

> **Decisione del 2026-09-19:** si procede con una **scheda indipendente per F2** con due sonde (fermentatore e
> bagno di glicole): vedi [13_F2_SCHEDA_INDIPENDENTE.md](13_F2_SCHEDA_INDIPENDENTE.md). Questo documento resta
> come riferimento (sonde §6, disturbi §7, prove §10) e nel caso si riprenda in futuro l'ipotesi della scheda
> unica: i pin della sonda del glicole (27/23) sono gli stessi in entrambe le soluzioni.

Riferimenti: [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) (pin map attuale),
[06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630)
(guasto reset alla pompa), [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md),
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md), [10_PUNTI_APERTI.md §2](10_PUNTI_APERTI.md) (onboarding F2).

**Ambito.** Progetto hardware completo di una scheda con **1 ESP32, modulo a 4 relè e 3 sonde DS18B20**
che governa F1 e F2 e misura la temperatura del bagno di glicole del chiller (solo monitoraggio,
didattica). Il resto è *valutazione*, non implementazione: impatto sul firmware (§11), su Home Assistant
(§12) e piano di passaggio dalla situazione attuale (§13). **Nessun file del firmware 6.x né dei pacchetti
HA esistenti è stato modificato.**

## 1. Sintesi

- **Fattibile: sì.** I carichi sono minuscoli (≈ 100 W a 230 V in tutto, ≈ 0,3 A di bobine) e servono 12 GPIO:
  sul devkit WROOM i pin d'uscita senza vincoli di boot sono 14, quindi il margine c'è ma non è largo (§5).
  Non c'è ostacolo elettrico.
- **Il costo vero non è l'hardware.** Sono tre cose: (1) un solo ESP32 diventa **dominio di guasto comune**
  (un reset, un OTA o un blocco fermano F1, F2 e sonda); (2) il firmware è scritto per **un solo
  fermentatore** (§11: è una ristrutturazione, non un'aggiunta); (3) il collaudo e la didattica su F2 non
  sono più innocui per F1 (§12).
- **Prerequisito bloccante.** F1 oggi si resetta 1 volta su ~8-12 spegnimenti della pompa
  ([06 §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630)).
  Replicare quell'architettura su 4 relè raddoppia le occasioni di reset e le porta su due fermentatori. La
  scheda qui sotto include fin dall'inizio tutti i rimedi in prova su F1 (§7), ma **non va adottata in
  produzione finché il guasto non è chiuso e capito**.
- **Percorso consigliato (§13).** Scheda *additiva*: stessi pin di F1 per il canale 1, canali nuovi sugli
  altri. Così il firmware **attuale, senza modifiche**, gira sulla scheda come F1 oppure — ricompilato con i
  pin di F2 via `configure_target.ps1` — come F2. Si può quindi mettere la scheda in servizio *prima* di
  avere il firmware dual, e ogni passo ha il rollback sul firmware già qualificato.
- **La sonda del glicole non entra mai nel controllo**: solo telemetria, grafici e un allarme "il chiller non
  raffredda". Un suo guasto non deve mai fermare né riavviare i fermentatori.

## 2. Punto di partenza e ipotesi

**Da repository:** F1 = ESP32 WROOM, modulo a 2 relè attivi LOW, DS18B20 su GPIO33 con VDD commutato da
GPIO25, OLED su 21/22; fascia riscaldante 30-35 W sul relè del caldo; camicia a glicole con pompa 220 V
10-15 W sul relè del freddo e chiller sempre acceso ([05 §1](05_HOME_ASSISTANT_F1.md)). F2 non ha hardware.

**Ipotesi di questo progetto — da confermare (§14):**

1. Ogni camicia ha la sua pompa (10-15 W) che pesca dal **bagno del chiller** e vi restituisce, come F1 oggi.
   Le due pompe stanno quindi nello stesso bagno. (Variante con una pompa del chiller e due elettrovalvole:
   stessi relè, ma bobine/diodi diversi — vedi §4.3.)
2. Il chiller ha il **proprio termostato**, è sempre acceso e sta su una **presa separata**: la scheda non lo
   comanda e non ne alimenta il compressore (qualche ampere di spunto e il disturbo che genera).
3. Un fermentatore = una fascia da 30-35 W (come F1).
4. Fermentatori, chiller e scheda sono vicini: cavi sonda ≤ 3 m.

## 3. Valutazione: una scheda o tre?

| Criterio | **A. Scheda dual** (1 ESP32, 4 relè, 3 sonde) | **B. Due scatole F1 identiche + nodo sonda** (3 ESP32) |
|---|---|---|
| Firmware | ristrutturazione a più istanze (§11) | nessuna modifica; il nodo sonda è un mini-sketch |
| Dominio di guasto | **unico**: reset/OTA/blocco fermano tutto | tre indipendenti |
| Collaudo o didattica su F2 con birra in F1 | i test invasivi (riavvio, watchdog, WiFi, power-cycle sensore, stress relè) colpiscono **anche F1** | nessuna interferenza |
| Hardware | 1 ESP32, 1 scatola, 1 connessione WiFi/MQTT | 3 ESP32, 3 alimentazioni, 3 connessioni |
| Cablaggio 230 V | tutto presso il chiller: le pompe sono nel bagno, cavi corti | due scatole ai fermentatori, cavi più lunghi verso le pompe |
| Stesso orologio per le tre temperature | sì | no (in HA si usa l'istante di arrivo: sufficiente) |
| Costo | più basso | più alto |

**Giudizio.** A è la scelta giusta se lo scopo è anche didattico (un'unica vista dell'impianto, un solo
dispositivo da mantenere) *e* se si accettano le regole operative del §12. B è l'alternativa prudente se il
guasto di reset non si risolve o se i test invasivi su F2 diventano frequenti mentre F1 ha birra. Il progetto
hardware qui sotto **non chiude la porta a B**: la stessa scheda può funzionare da "solo F2 + sonda", e la
sonda si può spostare su un nodo a parte.

## 4. Architettura

### 4.1 Idraulica (ipotesi §2)

```
        ┌──────────── CHILLER SS Brewtech (presa propria, termostato proprio, sempre acceso) ────────────┐
        │   bagno di glicole  ◄── T3 (DS18B20 inox: temperatura del bagno, solo monitoraggio)           │
        │        ▲ P1 (pompa F1, K2)                     ▲ P2 (pompa F2, K4)                            │
        └────────┼───────────────────────────────────────┼──────────────────────────────────────────────┘
          mandata│ritorno                          mandata│ritorno
        ┌────────┴──────────┐                   ┌────────┴──────────┐
        │ camicia F1        │                   │ camicia F2        │
        │ fascia H1 (K1)    │                   │ fascia H2 (K3)    │
        │ sonda T1          │                   │ sonda T2          │
        └───────────────────┘                   └───────────────────┘
```

Il bagno è **accoppiato**: quando le due pompe girano insieme il glicole si scalda e la potenza frigorifera
disponibile per ciascun fermentatore scende. È il fenomeno che la terza sonda rende visibile (e che può
spostare l'efficacia dei guadagni PID tarati su F1 con una sola pompa).

### 4.2 Alimentazione: due rami isolati

```
230 V ─ F0 T2A ─ MOV ─ X2 100nF ─┬─► PSU-L 5V/1A ─► ferrite ─► 470 µF ‖ 100 nF ─► ESP32 VIN
                                  │       │  GND-L ───────────────────────────────► GND ESP32, sonde, OLED,
                                  │       │                                          GND ingressi modulo relè
                                  │       └─ ESP32 3V3 ─► OLED · pull-up IN1..IN4 · VCC (LED opto) modulo · VDD sonde (via GPIO)
                                  │
                                  └─► PSU-R 5V/1A ─► JD-VCC modulo relè (bobine)
                                          GND-R ───► GND bobine — NESSUN collegamento a GND-L
```

- **Due alimentatori distinti** (es. moduli AC-DC da incasso 5 V ≥ 1 A, tipo Hi-Link HLK-5M05 o Mean Well
  IRM-05-5 — non verificati): logica ≈ 0,25 A medi / 0,5 A di picco WiFi; bobine 4 × ≈ 70-90 mA ≈ 0,3-0,36 A.
- **Modulo relè a 4 canali con optoisolatori e ponticello JD-VCC rimovibile**, attivo LOW come F1
  (`RELAY_ON = LOW`), contatti NO. Il ponticello JD-VCC–VCC va **tolto**: le bobine vanno da PSU-R, il lato
  LED degli opto dal 3V3 dell'ESP32. Così tra ESP32 e bobine non c'è continuità galvanica.
- **Da verificare al banco sul modulo scelto** (varia per produttore): che a ponticello tolto i due GND siano
  davvero separati (prova B1) e che i relè commutino in modo affidabile con VCC opto a 3,3 V (prova B3). Sul
  modulo di F1 VCC è a 5 V e funziona; col 4 canali e GPIO a 3,3 V *non* alimentare VCC a 5 V a scatola
  chiusa senza aver provato: a GPIO alto il LED dell'opto vede 5 − 3,3 = 1,7 V e può restare acceso.
- Il chiller **non passa da questa scatola**.

### 4.3 Uscite 230 V

```
L (da F0) ─┬─ F1 T500mA ─► K1 COM ─NO─► H1 fascia F1  ┐
           ├─ F2 T500mA ─► K2 COM ─NO─► P1 pompa F1   │  ogni carico: RC snubber (100 Ω + 100 nF X2)
           ├─ F3 T500mA ─► K3 COM ─NO─► H2 fascia F2  │  + varistore 275 V, in PARALLELO al carico,
           └─ F4 T500mA ─► K4 COM ─NO─► P2 pompa F2   ┘  montati vicino al morsetto del carico
N comune ────────────────────────────────────────────► ritorno di H1, P1, H2, P2
PE ──────────────────────────────────────────────────► PE di ogni carico e del fermentatore
```

- Si commuta solo la fase; contatti **NO**: relè a riposo (reset, boot, ESP scollegato) = carico spento.
- Fusibili ritardati T500mA per uscita: fascia 35 W ≈ 0,15 A, pompa 15 W ≈ 0,1-0,15 A. **Verificare la
  targhetta** delle pompe reali (corrente di spunto).
- Snubber *in parallelo al carico* e non sui contatti: sui contatti farebbe passare ≈ 7 mA (100 nF a 230 V)
  anche a relè aperto. Sulle fasce (resistive) serve poco ma si monta comunque, per un assemblaggio uguale
  su tutte e quattro le uscite.
- Variante elettrovalvole al posto delle pompe: valvola 230 V AC → stesso schema; valvola in continua →
  alimentatore dedicato e **diodo di ricircolo** in parallelo alla bobina, mai sullo stesso 5 V della logica.
- La presa a monte deve avere **differenziale 30 mA**: pompe immerse in un bagno conduttivo.

## 5. Pin map WROOM dual

Regola: **i pin di F1 non cambiano**; tutto il resto è aggiunto. Nessun pin di strapping (0, 2, 5, 12, 15),
nessun pin flash (6-11), nessun UART0 (1, 3, serve per USB/log), GPIO 14 evitato (segnale PWM al boot).

| Funzione | GPIO | Stato rispetto a F1 |
|---|---:|---|
| K1 — relè caldo F1 (IN1) | 16 | invariato |
| K2 — relè freddo/pompa F1 (IN2) | 17 | invariato |
| K3 — relè caldo F2 (IN3) | 18 | nuovo |
| K4 — relè freddo/pompa F2 (IN4) | 19 | nuovo |
| T1 DQ — sonda F1 | 33 | invariato |
| T1 VDD commutato | 25 | invariato |
| T2 DQ — sonda F2 | 32 | nuovo |
| T2 VDD commutato | 26 | nuovo |
| T3 DQ — sonda glicole | 27 | nuovo |
| T3 VDD commutato | 23 | nuovo |
| OLED SDA / SCL | 21 / 22 | invariato |
| Riserva uscite | 4, 13 | libere |
| Riserva solo ingresso | 34, 35, 36, 39 | libere (pulsante, sensore di perdite, corrente…) |

Devkit WROOM a 30 pin; il pin map S3 Super Mini di [02 §3](02_HARDWARE_E_TARGET.md) **non basta** per questa
scheda (servono 12 linee). Se si vuole l'S3 servirà una board con più pin.

**Pull-up di sicurezza:** una 10 kΩ da 3V3 su ciascuno dei 4 ingressi IN. Con i GPIO in alta impedenza
(boot, reset, e IN3/IN4 mentre gira il firmware attuale che non li conosce) i relè restano OFF.

## 6. Sensori: tre bus separati

Tre DS18B20 impermeabili in inox, **un bus 1-Wire per sonda**, ciascuno con il proprio VDD commutato e il
proprio pull-up 4,7 kΩ collegato a *quel* VDD (come già prescritto per F1 in [02 §2](02_HARDWARE_E_TARGET.md)).

Perché non un bus unico con tre indirizzi ROM: (1) un cavo in corto o tranciato metterebbe fuori uso tutte e
tre le misure, cioè entrambi i fermentatori in fail-safe; (2) il power-cycle software di recupero
(`sensor_power_cycle`) interromperebbe anche le altre sonde; (3) sostituire una sonda guasta sarebbe una
riassegnazione di indirizzi invece di "sfilo e infilo". L'identità è fisica: **T1 = F1, T2 = F2, T3 = glicole**,
guaina termorestringente colorata sui cavi e sulle etichette.

```
GPIOxx (VDD_SW) ──┬───────────────────── VDD sonda (rosso)
                  ├── 100 nF verso GND, vicino al morsetto
                  └─[4,7 kΩ]──┬────────── DQ sonda (giallo)
GPIOyy (DQ) ──[100 Ω]─────────┘
GND ─────────────────────────────────────── GND sonda (nero)      schermo → GND solo lato scheda
```

- Cavo schermato a 3 conduttori + schermo; ≤ 3 m con 4,7 kΩ (fino a ~10 m con 2,2 kΩ).
- **Sonda glicole**: inox 316, cavo in silicone, compatibile con glicole propilenico. Al centro del bagno, ad
  almeno 5 cm da evaporatore e bocchette delle pompe (altrimenti misura un getto, non il bagno), fissata con
  clip, passacavo sul coperchio. Cavo ≥ 10 cm dal cavo di rete del chiller e mai a contatto con compressore
  o tubi caldi. Il DS18B20 è specificato ±0,5 °C da −10 a +85 °C: adeguato per il bagno (≈ −5…+10 °C).
  Registrare un offset con la prova B10; correggerlo in HA (template), non nel firmware.
- Le soglie di validità del firmware (−10…+40 °C, [sensor_watchdog.cpp](../fermentatore_v6/sensor_watchdog.cpp))
  sono tarate sul fermentatore: per il bagno vanno diverse (§11).

## 7. Protezione dai disturbi — lezione del guasto di F1

Causa del reset su F1 **non ancora accertata** (ipotesi in [06 §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630)).
La scheda dual monta *tutti* i rimedi insieme; quando F1 dirà quale è decisivo, annotarlo in 06 §7 e in questo
documento, per non perderlo in una revisione futura.

| Rimedio | Dove | Su F1 oggi |
|---|---|---|
| Alimentazione bobine separata e isolata | PSU-R + JD-VCC (§4.2) | da provare |
| Snubber RC + varistore sui carichi | in parallelo a ogni carico (§4.3) | da provare |
| Condensatore EN–GND | vicino al modulo ESP32; stesso valore che risulta efficace su F1 (i devkit hanno già un circuito di auto-reset su EN: valori grandi possono impedire la programmazione automatica, verificarlo) | in prova |
| Filtro sull'ingresso 5 V dell'ESP32 | ferrite in serie + 470 µF ‖ 100 nF | no |
| Filtro di rete | MOV + X2 100 nF all'ingresso, F0 T2A | no |
| Separazione fisica 230 V / bassa tensione | due zone con setto (§8); pompe con cavo intrecciato L+N | no |
| Chiller su presa separata | il compressore genera transitori di rete | da verificare |
| Cavi GPIO dei relè corti, ritorno di massa affiancato | tra ESP32 e modulo | no |

## 8. Scatola, morsettiera, sicurezza

```
┌──────────────────────────────────────────────────────────────────┐
│ ZONA BASSA TENSIONE              ║  ZONA 230 V                    │
│  ESP32 devkit · OLED             ║  ingresso · F0 · MOV · X2      │
│  morsetti T1  T2  T3 (sonde)     ║  PSU-L · PSU-R                 │
│  J_AUX (4,13,34,35,3V3,GND)      ║  fusibili F1..F4               │
│        ┌─────────────────────────╨───────┐                        │
│  IN1..4│   modulo 4 relè (a cavallo      │COM/NO K1..K4 ─────────►│
│  VCC ─►│   del setto: opto a sinistra,   │                        │
│        │   contatti a destra)            │  morsetti carichi:     │
│        └─────────────────────────╥───────┘  H1 P1 H2 P2 (L,N,PE)  │
│  pannello: USB prolunga, EN/reset║                                │
└──────────────────────────────────────────────────────────────────┘
```

- Scatola plastica IP54 o superiore, passacavi a tenuta, sfogo per condensa/glicole; scatola *sopra* il livello
  del bagno con goccioloni sui cavi. PE su un morsetto comune a tutti i carichi.
- **Morsettiera bassa tensione** (estende [02 §4](02_HARDWARE_E_TARGET.md)): per ogni sonda `VDD_SW`, `DQ`,
  `GND`, `SCHERMO` (T1, T2, T3); `OLED` 4 poli (3V3, GND, SDA, SCL); `J_AUX` di riserva.
- **Pannello**: prolunga USB per riflashare senza aprire (F1 è sigillata, [06 §2](06_COLLAUDO_F1.md)) e pulsante
  EN. Con USB collegata e PSU-L attiva, verificare che il devkit abbia il diodo sul 5 V, altrimenti scollegare
  PSU-L durante la programmazione.
- **Interruttore generale dei carichi** sul ramo PSU-R: taglia tutte e quattro le uscite in un colpo, senza
  spegnere la logica (comodo per lavorare sui fermentatori).
- Ordine di lavoro: prima bassa tensione e prove a vuoto, poi i 230 V; lavori solo a scatola scollegata.

## 9. Distinta base (una scheda dual)

| Qtà | Componente | Nota |
|---:|---|---|
| 1 | ESP32 DevKit WROOM-32, 30 pin | come F1 |
| 1 | Modulo 4 relè, opto, attivo LOW, JD-VCC separabile, 10 A/250 V | verificare isolamento (B1) |
| 2 | Alimentatore AC-DC 5 V ≥ 1 A da incasso | logica e bobine separate |
| 3 | Sonda DS18B20 inox impermeabile, cavo ≥ 1,5 m (glicole: silicone, ≥ 2 m) | una di riserva consigliata |
| 1 | OLED SSD1306 128×64 I2C (0x3C) | come F1; un secondo a 0x3D possibile |
| 4 | Fusibile T500mA 5×20 + portafusibile | uno per uscita |
| 1 | Fusibile T2A + portafusibile | ingresso, F0 |
| 4 | Snubber RC 100 Ω + 100 nF X2 (275 V AC) | uno per carico |
| 5 | Varistore 275 V AC (es. S10K275) | 4 carichi + ingresso |
| 1 | Condensatore X2 100 nF | ingresso |
| 3 | Resistenza 4,7 kΩ (pull-up sonde) + 3 × 100 Ω (serie DQ) | |
| 4 | Resistenza 10 kΩ (pull-up ingressi relè) | |
| 1 | 470 µF 16 V + 100 nF + ferrite | filtro 5 V ESP32 |
| 1 | Condensatore EN–GND | valore da F1 |
| 1 | Scatola IP54+, passacavi, morsetti (fase/neutro/PE, sonde), setto isolante | |
| 1 | Interruttore bipolare per il ramo carichi | |

Stima di ordine di grandezza, **non verificata**: 70-110 € tutto compreso, senza chiller, pompe e fasce.

## 10. Prove al banco (criteri di accettazione)

Prima della messa in servizio, a vuoto e poi con carichi reali (pompe in un secchio d'acqua). Il campione
minimo di **300 commutazioni per canale** è scelto sul dato di F1: se il vero tasso di reset fosse 1 % la
probabilità di non vederne nessuno in 300 cicli è ≈ 5 % (con il ≈ 10 % osservato su F1 ne bastano 25).

| # | Prova | Come | Criterio |
|---|---|---|---|
| B1 | Isolamento logica/bobine | ponticello JD-VCC tolto, multimetro tra GND-L e GND-R | nessuna continuità (> 1 MΩ) |
| B2 | Relè OFF ad accensione e reset | 50 cicli di alimentazione + 50 reset da EN, IN3/IN4 non pilotati | nessun relè si eccita, mai |
| B3 | Logica 3,3 V | ogni canale a LOW / HIGH | chiude a LOW, rilascia a HIGH, su tutti e 4 |
| B4 | Stress singolo canale | 300 cicli ON/OFF (≈ 2 s) con carico reale | 0 reset; 0 letture sonda non valide entro 5 s dalla commutazione |
| B5 | Stress a 4 canali | simultaneo, sfasato e casuale, 300 cicli ciascuno | come B4 |
| B6 | Disturbo del chiller | scheda sulla presa del chiller, 24 h con compressore che cicla e pompe che commutano | 0 reset; ripetere con presa separata e confrontare |
| B7 | Sonde indipendenti | scollegare / cortocircuitare una sonda alla volta; power-cycle di una | le altre due leggono senza interruzioni |
| B8 | Fail-safe | staccare PSU-L, premere EN, staccare USB | i relè si aprono e restano aperti |
| B9 | Termica | 1 h con 4 relè eccitati | superficie PSU e modulo ≤ 60 °C |
| B10 | Sonda glicole | bagno acqua-ghiaccio (0 °C) e ambiente contro termometro di riferimento | errore ≤ 0,5 °C; annotare l'offset |

B4-B6 richiedono uno **sketch di bring-up** (non è firmware V6: commuta i relè, legge le tre sonde, stampa
uptime e causa di reset). Da scrivere prima del passo 1 del §13.

## 11. Impatto sul firmware (solo valutazione)

Il firmware 6.x è tutto a **istanza singola**: `fermentatore_v6.ino` ha ≈ 50 variabili `static` che
rappresentano *un* fermentatore (`g_controller`, `g_config`, `g_sensore`, `g_profilo`, deduplica comandi, flag
NVS, heartbeat…), e i moduli `uscite_rele` e `sensor_watchdog` hanno un solo relè-coppia e un solo bus.

| Modulo | Oggi | Cambia | Sforzo |
|---|---|---|---|
| `config.h` | pin e topic a compile-time, un solo `DEVICE_FERMENTER_ID` | pin per canale; topic costruiti a runtime per `f1`/`f2`/`chiller`; target `wroom-dual` nel configuratore | S |
| `uscite_rele` | due relè `static` | una coppia per fermentatore (interlock caldo/freddo per coppia); regola tra coppie: mai due commutazioni nello stesso istante (sfasamento ≥ 2 s, riduce i disturbi) | S-M |
| `sensor_watchdog` | 1 `OneWire` + stato `static`; validità fissa −10…+40 °C; power-cycle **bloccante** ≈ 1,5 s (500 + 200 + 750 ms) | un'istanza per bus (3); validità per canale (glicole fino a ≈ −15 °C); power-cycle non bloccante | M |
| `controller`, `controller_onoff` | già su struct (`ControllerState/Config`), niente stato di modulo | nulla: due istanze | — |
| `command_processor` | parser senza stato | nulla (il routing `f1`/`f2` sta nel dispatcher) | — |
| `profilo` | namespace NVS e chiavi fissi (`ferm_v6`) | namespace/chiavi per fermentatore; **il blob NVS di F1 non si sposta da una scheda all'altra** (§13) | S |
| `connettivita` | 1 client MQTT, 1 LWT, topic costanti | subscribe a due prefissi comandi, publish parametrico, LWT (sotto), payload glicole | M |
| `display` | 1 OLED, schermata di un fermentatore | 3 temperature + stato relè; allarme per fermentatore | M |
| `fermentatore_v6.ino` | ≈ 1100 righe, stato globale di un fermentatore | struct `Fermentatore` istanziata due volte, funzioni con riferimento all'istanza, loop che scorre i canali | **L** |
| `tests/host` | 22 scenari su istanza singola | ogni scenario su istanza 1 e 2; nuovi: indipendenza dei canali, sfasamento, canale assente, validità glicole | **L** |
| `collaudo_motore.py` | `entity_id` di F1 in `E{}` | parametrizzazione per fermentatore (già in [10 §4](10_PUNTI_APERTI.md)) | M |

**Il rischio principale è la regressione sul codice già qualificato su F1.** Contromisura: lasciare
invariati controller, profilo (matematica) e command_processor, e imporre che tutti gli scenari attuali
passino **identici** sull'istanza 1 con l'istanza 2 ferma, e viceversa.

**Quattro decisioni di progetto da prendere prima di scrivere codice:**

1. **Nessun guasto si propaga tra canali.** Oggi un allarme sensore che dura 30 s fa scattare
   `riavvia_controllato("allarme sensore")` (fino a 3 volte, [.ino:490](../fermentatore_v6/fermentatore_v6.ino)):
   su un dispositivo dual una sonda rotta di F2 riavvierebbe anche F1. Il canale va in fail-safe, non il
   dispositivo. Il **canale glicole non deve mai** causare fail-safe, allarme critico o riavvio. Serve anche
   un canale "assente/disabilitato" (F1 non ancora cablato non è un errore).
2. **LWT unico.** MQTT ammette un solo Last Will per connessione; oggi ogni fermentatore ha
   `…/<id>/esp/availability` e HA la usa su ogni entità
   ([fermentatore_v6_f1_entita.yaml](../hass_spec/packages/fermentatore_v6_f1_entita.yaml)).

   | Opzione | Idea | Giudizio |
   |---|---|---|
   | **A** (consigliata) | LWT su un topic *di dispositivo*; `f1/…/availability` e `f2/…/availability` pubblicati `online`/`offline` dal firmware; in HA le entità usano **due** `availability` con `availability_mode: all` | un cambio meccanico nei pacchetti (§12); nessun costo di RAM |
   | B | due client MQTT sulla stessa scheda, un LWT ciascuno | ≈ 40 KB di heap per sessione TLS (da misurare: l'heartbeat pubblica già `heap_libero`); due connessioni verso HiveMQ |
   | C | LWT solo su F1 | F2 e glicole non risulterebbero mai offline |

3. **Cartella e versione.** La policy ([versioning.instructions.md](../../../.github/instructions/versioning.instructions.md))
   lega `N` della versione alla cartella. Una ristrutturazione di questa portata è coerente con una nuova
   cartella **`fermentatore_v7`**: `fermentatore_v6` resta congelato come firmware "F1-only" qualificato,
   cioè il **rollback** del §13. Da decidere (§14).
4. **Contratto MQTT.** `f1` e `f2` restano identici; nuovo dispositivo logico `fermentazione/v6/chiller/esp/state`
   (retained: `temp_glicole`, `sensore_ok`, `temp_eta_s`, `uptime_s`, `firmware`) e nessun comando. Da
   aggiungere a [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md) quando definito.

## 12. Impatto su Home Assistant

La struttura del Lab **resta**: topic, `unique_id` ed `entity_id` di F1/F2 non cambiano, `genera_f2.py` continua
a generare i pacchetti F2. Si aggiunge, non si sostituisce.

| Elemento | Cambio |
|---|---|
| Pacchetto nuovo `fermentatore_v6_chiller.yaml` | Non generato: `genera_f2.py` tocca solo i file `_f1_`. Entità `sensor.chiller_v6_temperatura_glicole`, disponibilità, età del dato. Template derivati: `ΔT = T fermentatore − T glicole` per F1 e F2 (la forza che spinge lo scambio, ottima per la didattica) |
| Allarmi glicole | **Glicole troppo caldo con una pompa accesa da più di N minuti** = "il chiller non raffredda" (mail); **sonda glicole offline** = solo notifica in HA, niente mail. Il firmware non vede un guasto di chiller ([04 §9 punto 12](04_CONTRATTO_HASS.md)): questo allarme colma il buco |
| Entità F1/F2 | `availability` a due topic con `availability_mode: all` (opzione A del §11): modifica meccanica, si fa nei file F1 e si rigenera F2 |
| Dashboard | Serie **T glicole** nei grafici di 12 h e del Laboratorio di F1. Nuovo riquadro/vista **Impianto**: T F1, T F2, T glicole e relè freddo F1/F2 sovrapposti — si vede la sega del termostato del chiller e il bagno che si scalda quando entrambe le pompe girano. La dashboard F2 oggi **non esiste**: generarla estendendo `genera_f2.py` (oggi ignora `dashboards/`) |
| Modalità | `f1_v6_modalita` e `f2_v6_modalita` restano separate, ma il **dominio di guasto è uno solo**: i test invasivi (`riavvio`, `watchdog`, `wifi`, `sensore_*`, `stress_*`, `corrente`) su un fermentatore sono ammessi solo se l'altro è **vuoto**. Conviene un controllo negli script di collaudo/didattica che lo verifichi |
| Contratto ([04 §9](04_CONTRATTO_HASS.md) regola 8: "nessuna entità condivisa") | Vale ancora per F1/F2; il chiller è un terzo dispositivo logico condiviso: aggiornare il testo |
| Collaudo | Parametrizzare `E{}` per fermentatore (già in [10 §4](10_PUNTI_APERTI.md)): serve comunque per F2 |
| InfluxDB | L'add-on è già attivo e inutilizzato ([10 §8](10_PUNTI_APERTI.md)): archiviare qui la curva del glicole a lungo termine |

Ogni modifica al Lab segue le procedure di [05 §7](05_HOME_ASSISTANT_F1.md): backup prima, solo l'istanza Lab.

## 13. Migrazione dalla situazione attuale

Principi: **prima l'hardware, poi il firmware, poi Home Assistant; mai due cose nuove insieme su F1; ogni
passo ha un rollback sul firmware già qualificato.** Due fatti rendono il percorso a basso rischio:

- I pin di F1 sono invariati (§5): il firmware **6.x attuale gira sulla scheda dual come F1-only**.
- `configure_target.ps1` accetta gli override dei pin ([02 §5](02_HARDWARE_E_TARGET.md)): lo stesso
  firmware, ricompilato con `-TargetId wroom-f2 -PinDs18b20 32 -PinSensorPwr 26 -PinHeatRelay 18 -PinCoolRelay 19`,
  gira sulla scheda come **F2-only**. Il codice non cambia.

| Passo | Cosa | Firmware | Home Assistant | Gate di uscita | Rollback |
|---|---|---|---|---|---|
| **0** — fino al 3/10 | **F1 non si tocca.** Chiudere il guasto reset-pompa, flash 6.4.0, Fase A. In parallelo: confermare le ipotesi (§14), scegliere il modulo relè, ordinare, scrivere lo sketch di bring-up | 6.x attuale su F1 | invariato | F1 pronto per la birra ([06 §6](06_COLLAUDO_F1.md)); guasto reset **chiuso e capito** | — |
| **1** — banco | Assemblare la scheda, prove B1-B10 (§10). Prima con lo sketch di bring-up, poi con il firmware attuale nelle immagini F1 e F2 | attuale (immagine F1, immagine F2) | nessuno | tutte le prove B superate | scheda non in servizio |
| **2** — scheda come **F2** | Primo uso reale: scheda con carichi di F2 (camicia, pompa P2, fascia H2, sonda T2), pompa P1 di F1 ancora sulla vecchia scatola nello stesso bagno. Sonda T3 cablata ma non letta | attuale, immagine F2 | installare `fermentatore_v6_f2_entita` e `_laboratorio` (generati); parametrizzare il collaudo; collaudare F2 | collaudo F2 superato (stesso criterio di F1); nessun reset spontaneo in ≥ 7 giorni | spegnere la scheda; F1 non è mai stata toccata |
| **3** — firmware dual | Nuovo firmware sulla stessa scheda; F2 continua invariato; abilitare il canale glicole; il canale F1 resta *disabilitato* finché non è cablato | `fermentatore_v7` (§11) | pacchetto `chiller`, grafici, allarmi, `availability` a due topic | scenari host attuali identici; F2 stesso comportamento del passo 2; glicole in HA | riflash immagine F2-only |
| **4** — cutover **F1** | **Solo a fine lotto, con F1 vuoto.** Spegnere e scollegare la vecchia scatola; spostare fascia H1, pompa P1 e sonda T1 sui canali 1/2 e T1; abilitare il canale F1 e flashare; ricaricare il profilo | `fermentatore_v7` | invariato per F1 (stessi topic) | collaudo F1 (test rapidi + `stress_pompa` sui 4 canali) | rimettere i carichi sulla vecchia scatola, che resta intatta come scorta |
| **5** — assestamento | ≥ 2 settimane di osservazione; aggiornare 02 (pin map), CLAUDE.md, 10 §2; decidere il destino della vecchia scatola | — | — | nessun reset spontaneo; allarmi coerenti | — |

**Perché il cutover di F1 solo a lotto finito.** Il progresso del profilo (`secondi_trascorsi`), il modo di
controllo e i guadagni stanno nell'NVS del *vecchio* ESP32 e non si trasferiscono: spostare F1 a metà lotto
significherebbe ripartire dal profilo da zero. Se il lotto parte il 3/10 e dura 45-48 giorni ([10 §0](10_PUNTI_APERTI.md)),
finisce attorno al **17-20 novembre**: il passo 4 non prima.

**Da non fare mai:**

- **Due dispositivi con lo stesso `fermenter_id` accesi insieme** (vecchia scatola e scheda dual come F1):
  `MQTT_CLIENT_ID = esp32_v6_f1` uguale, il broker espelle uno dei due a ripetizione, e i comandi e
  l'availability si mescolano. La vecchia scatola va **spenta prima** di abilitare F1 sulla scheda.
- Cambiare hardware *e* firmware su F1 nello stesso passo.
- Flashare (OTA) la scheda dual mentre un fermentatore ha birra e la regolazione è delicata: il riavvio ferma
  entrambi per ≈ 30 s (relè OFF al boot: sicuro, ma è una fermata).
- Condividere `secrets.h` tra dispositivi senza verificare gli identificativi ([10 §2](10_PUNTI_APERTI.md)).

## 14. Decisioni aperte

- [ ] **Confermare le ipotesi del §2** — in particolare pompe nel bagno (una per camicia) e non elettrovalvole.
- [ ] Potenza e corrente di spunto reali di pompe e fasce (targhette) per confermare fusibili e snubber.
- [ ] Un OLED (3 temperature) o due (uno per fermentatore, a 0x3C/0x3D).
- [ ] Posizione della scatola e lunghezza dei cavi sonda; presa separata per il chiller.
- [ ] Modulo relè e alimentatori concreti; esito di B1 e B3 sul modulo scelto.
- [ ] Opzione LWT (A/B/C, §11) e cartella/versione del nuovo firmware (`fermentatore_v7`?).
- [ ] Scrivere lo sketch di bring-up (§10).
- [ ] **Esito del guasto reset-pompa su F1** e quale rimedio lo risolve (bloccante per il passo 0).
- [ ] Decidere se il "verdetto pronto per la birra" di F2 richiede la stessa suite di F1 ([10 §2](10_PUNTI_APERTI.md)).
