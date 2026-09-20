# 13. F2 su scheda indipendente, con sonda del bagno di glicole

**Versione documento**: 1.0 (2026-09-19) · **Stato**: decisione presa, da costruire

Riferimenti: [12_HARDWARE_DUAL_F1_F2.md](12_HARDWARE_DUAL_F1_F2.md) (alternativa valutata e non adottata; ne
restano validi §6 sonde, §7 disturbi, §10 prove), [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md),
[06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630),
[10_PUNTI_APERTI.md §2](10_PUNTI_APERTI.md).

## 1. Decisione

- **F2 è una scheda indipendente**, con il proprio ESP32: clone hardware di F1 (2 relè, OLED, alimentatore).
  F1 non cambia e resta com'è.
- **F2 ha due sonde DS18B20**: una nel fermentatore (T-F2) e una nel **bagno di glicole del chiller** (T-glicole),
  solo per monitoraggio e didattica. La sonda del glicole sta sulla scheda di F2 anche se il bagno è condiviso
  con F1.
- **Rimedio al guasto hardware di reset**: condensatore tra EN e GND, come su F1 (valore risultato efficace su F1).
  Nessuna alimentazione separata delle bobine per ora.

> **Interpretazione da confermare.** Ho letto "correzione bug hardware della seconda che resta così con
> condensatore tra GND e EN" come: la scheda F2 adotta *solo* il condensatore EN–GND. Se intendevi che è F1 a
> restare così, o che F2 debba avere altro, va corretto §3.

Effetto sul resto: sparisce il dominio di guasto comune (F1 e F2 si resettano indipendentemente), sparisce
la ristrutturazione del firmware a due istanze (era il costo grosso del documento 12) e F1 non viene mai
toccata. Resta l'accoppiamento **idraulico**: le due pompe pescano dallo stesso bagno (§7).

## 2. Hardware

F2 = distinta di F1 ([02 §2](02_HARDWARE_E_TARGET.md), README) più una sonda:

| Componente | Nota |
|---|---|
| ESP32 DevKit WROOM-32, modulo 2 relè attivo LOW, OLED SSD1306, alimentatore 5 V | come F1 |
| Sonda DS18B20 inox nel fermentatore (T-F2) | come F1 |
| **Sonda DS18B20 inox 316 per il bagno (T-glicole), cavo in silicone ≥ 2 m** | nuova; compatibile con glicole propilenico |
| 1 × 4,7 kΩ + 1 × 100 Ω + 1 × 100 nF per la sonda del glicole | come T-F2, vedi [12 §6](12_HARDWARE_DUAL_F1_F2.md) |
| Condensatore EN–GND | valore da F1 |

### Pin map F2 (WROOM)

I pin di F1 sono **invariati**: il firmware attuale con `-TargetId wroom-f2` gira su F2 senza override.

| Funzione | GPIO | Rispetto a F1 |
|---|---:|---|
| Relè caldo (fascia) | 16 | uguale |
| Relè freddo (pompa) | 17 | uguale |
| T-F2 DQ | 33 | uguale |
| T-F2 VDD commutato | 25 | uguale |
| **T-glicole DQ** | **27** | nuovo |
| **T-glicole VDD commutato** | **23** | nuovo |
| OLED SDA / SCL | 21 / 22 | uguale |

Perché 27/23: sono gli stessi pin della sonda glicole nella scheda dual del documento 12, così l'alternativa
non viene preclusa. Liberi: 4, 13, 18, 19, 26, 32 (uscite); 34, 35, 36, 39 (solo ingresso).

### Sonde

**Due bus 1-Wire separati**, ciascuno con il proprio VDD commutato da GPIO e il pull-up 4,7 kΩ collegato a *quel*
VDD (stessa regola di F1). Non un bus unico: un guasto della sonda del bagno non deve mai togliere la misura
del fermentatore, né il suo power-cycle di recupero deve interromperla. Schema di cablaggio, cavo schermato,
posa della sonda nel bagno (centro del bagno, ≥ 5 cm da evaporatore e bocchette delle pompe, cavo lontano dal
cavo di rete del chiller): [12 §6](12_HARDWARE_DUAL_F1_F2.md). Morsettiera: aggiungere `GLY_VDD_SW`, `GLY_DQ`,
`GLY_GND`, `GLY_SCHERMO` alle etichette di [02 §4](02_HARDWARE_E_TARGET.md).

### Rischio residuo sul guasto di reset

Il condensatore su EN è ancora **in prova su F1**: l'esito di `stress_pompa` dopo l'intervento non è
registrato. Se non bastasse, F2 eredita il difetto. Costo trascurabile, a scelta tua: predisporre già in F2
lo snubber RC (100 Ω + 100 nF X2) e un varistore 275 V in parallelo alla pompa. Non li ho messi nella
distinta per rispettare la tua decisione.

## 3. Prove al banco prima di mettere birra in F2

| Prova | Criterio |
|---|---|
| Relè OFF ad accensione e reset (50 + 50 cicli) | mai eccitati |
| Commutazioni della pompa con carico reale, **≥ 300 cicli** (`stress_pompa` ripetuto o sketch di bring-up) | 0 riavvii spontanei. Su F1 il tasso era ≈ 1 su 8-12; con un tasso vero dell'1 % la probabilità di non vederne nessuno in 300 cicli è ≈ 5 % |
| Le due sonde indipendenti: scollegare o mettere in corto la sonda del glicole | la lettura del fermentatore non si interrompe e il controllo continua |
| Sonda del glicole in acqua-ghiaccio (0 °C) contro termometro di riferimento | errore ≤ 0,5 °C; annotare l'offset e correggerlo in HA |

Poi il collaudo automatico di F2 ([06](06_COLLAUDO_F1.md)), con lo stesso criterio di F1.

## 4. Impatto sul firmware (solo valutazione)

Molto più piccolo del documento 12: un fermentatore per dispositivo, come oggi. Cambia solo l'aggiunta di un
**secondo sensore, di sola telemetria**.

| Modulo | Cambia | Sforzo |
|---|---|---|
| `sensor_watchdog` | oggi un solo `OneWire`/stato `static`: passare a un'istanza per bus (2); validità per canale (glicole ≈ −15…+40 °C invece di −10…+40 °C); power-cycle del bus glicole separato (oggi bloccante ≈ 1,5 s) | M |
| `fermentatore_v6.ino` | lettura e stato della sonda glicole. **Non deve toccare** `g_fallback_attivo`, il gating dei relè, l'allarme sensore né `riavvia_controllato` (oggi 30 s di allarme sensore riavviano l'ESP32: per il glicole sarebbe inaccettabile). Solo telemetria | S-M |
| `connettivita` | nuovo `fermentazione/v6/chiller/esp/state` retained (`temp_glicole`, `sensore_ok`, `temp_eta_s`), pubblicato dal dispositivo F2; nessun comando | S |
| `config.h` | pin e soglie del glicole; flag a compile-time (`SENSORE_GLICOLE_PRESENTE`: 0 su F1, 1 su F2) da gestire in `configure_target.ps1` | S |
| `display` | temperatura del glicole in una riga o come icona (facoltativo) | S |
| `tests/host` | nuovi scenari: sonda glicole guasta o fuori scala **non altera** controllo, relè e allarmi del fermentatore; range di validità; assenza della sonda | M |

- **Versione**: nuova funzione = MINOR (`6.x.0`), resta in `fermentatore_v6`, nessuna nuova cartella. Attenzione:
  la 6.5.0 è già riservata alle funzioni di collaudo ([10 §3](10_PUNTI_APERTI.md)).
- **Disponibilità**: essendo lo stesso dispositivo, le entità del chiller usano
  `fermentazione/v6/f2/esp/availability`. Nessun problema di LWT.
- Controller, profilo e `command_processor` non cambiano.

## 5. Impatto su Home Assistant

Struttura del Lab invariata; F1 non cambia.

| Elemento | Cambio |
|---|---|
| Pacchetti F2 | Installare `fermentatore_v6_f2_entita.yaml` e `_laboratorio` (generati da `genera_f2.py`; oggi c'è solo `_allarmi`) |
| Pacchetto nuovo `fermentatore_v6_chiller.yaml` | Non generato (il generatore tocca solo i file `_f1_`). `sensor.chiller_v6_temperatura_glicole`, età del dato, disponibilità = quella di F2. Derivati `ΔT = T fermentatore − T glicole` per F1 e F2 |
| Allarmi | **Glicole troppo caldo con una pompa accesa da più di N minuti** = "il chiller non raffredda" (mail; il firmware non vede questo guasto, [04 §9 punto 12](04_CONTRATTO_HASS.md)). **Sonda glicole offline**: solo notifica, niente mail |
| Dashboard | Serie **T glicole** nei grafici di F1 e F2; riquadro **Impianto** con T F1, T F2, T glicole e relè freddo dei due fermentatori sovrapposti. La dashboard F2 non esiste ancora: generarla estendendo `genera_f2.py` |
| Collaudo | Parametrizzare `E{}` per fermentatore ([10 §4](10_PUNTI_APERTI.md)): serve per collaudare F2 |
| Contratto | [04 §9](04_CONTRATTO_HASS.md) regola 8 ("nessuna entità condivisa") vale ancora per F1/F2; il chiller è un terzo dispositivo logico. Aggiungere il topic al contratto quando definito |

## 6. Migrazione

F1 non si tocca mai. Ogni passo è additivo e ha il rollback sul firmware già qualificato.

| Passo | Cosa | Firmware | Home Assistant | Gate |
|---|---|---|---|---|
| **0** — fino al 3/10 | F1 come da piano ([10 §1](10_PUNTI_APERTI.md)). In parallelo: ordinare i componenti, confermare l'interpretazione del §1, decidere sullo snubber | 6.x attuale su F1 | invariato | F1 pronta per la birra |
| **1** — banco | Costruire F2 e fare le prove del §3 | attuale, `wroom-f2` | nessuno | prove superate |
| **2** — F2 in servizio | Fermentatore, pompa, fascia e T-F2 su F2; T-glicole cablata ma non ancora letta | attuale, `wroom-f2` (nessun override) | installare pacchetti F2, parametrizzare il collaudo, collaudare F2 | collaudo F2 superato; nessun riavvio spontaneo in ≥ 7 giorni |
| **3** — sonda glicole | Nuovo firmware su F2 (OTA), pacchetto `chiller`, grafici e allarmi | `6.x.0` (§4) | pacchetto `chiller` | scenari host attuali identici; glicole in HA; sonda scollegata → F2 regola normalmente |
| **4** — F1 | Facoltativo: portare F1 alla stessa base di codice con il flag a 0 | stessa versione, flag 0 | nessuno | a fine lotto di F1, non prima |

Il rollback del passo 3 è il riflash del firmware del passo 2. Mai due dispositivi con lo stesso `fermenter_id`
accesi insieme (`MQTT_CLIENT_ID` uguale: il broker espelle uno dei due a ripetizione).

## 7. Accoppiamento tramite il bagno condiviso

Le due pompe pescano dallo stesso bagno, quindi F2 può disturbare F1 anche senza dipendenze elettriche: un
`cold_crash` o uno `stress_pompa` su F2 scaldano il glicole e riducono la potenza frigorifera disponibile per F1.
La sonda del glicole rende visibile il fenomeno. Regola operativa finché F1 ha birra: **test pesanti in freddo
su F2 solo dopo aver guardato T glicole e la tenuta di F1**, oppure a F1 vuota. Non c'è invece più alcun
rischio da riavvii o blocchi di F2 su F1.

## 8. Decisioni aperte

- [ ] Confermare l'interpretazione del §1 (rimedio = solo condensatore EN–GND su F2).
- [ ] Snubber RC e varistore sulla pompa di F2: sì o no.
- [ ] Conferma delle ipotesi idrauliche ([12 §2](12_HARDWARE_DUAL_F1_F2.md)): una pompa per camicia nello stesso bagno.
- [ ] Lunghezza del cavo della sonda del glicole (scatola di F2 → bagno) e posizione della scatola.
- [ ] Un OLED con la riga del glicole o nessuna modifica al display.
- [ ] Esito di `stress_pompa` su F1 dopo il condensatore (informa il rischio residuo del §2).
