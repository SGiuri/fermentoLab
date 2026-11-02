# 02. Hardware e configurazione del target

**Versione documento**: 1.0 (2026-09-19, contenuti dal 2026-05-25) · **Stato**: corrente

**Fonde**: `PIN_MAP_DEFINITIVA_V6.md` + `TARGET_CONFIGURATOR_V6.md` (stesso argomento: pin map elettrica
e lo script che la scrive in `config.h`). Riferimenti: [00_INDICE.md](00_INDICE.md),
[10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) §2 per l'onboarding hardware di F2 (da costruire da zero).

Scopo: pin map unica per il cablaggio delle due board supportate (ESP32 WROOM e ESP32-S3 Super Mini)
e lo strumento che configura `config.h` per uno dei quattro target (WROOM/S3 × F1/F2).

## 1. Regole generali

1. Relè attivi LOW: `RELAY_ON = LOW`, `RELAY_OFF = HIGH`.
2. Massa comune obbligatoria tra ESP32, alimentatore relè, DS18B20 e OLED.
3. Non usare i pad USB data (D+/D-, B+/B-) per I/O applicativi.
4. F1 e F2 usano la stessa pin map elettrica; cambia solo `DEVICE_FERMENTER_ID`.
5. **Solo F1 è hardware reale oggi.** F2 non è ancora costruito: la pin map sotto è il riferimento da
   seguire quando verrà assemblato — vedi [10_PUNTI_APERTI.md §2](10_PUNTI_APERTI.md).
6. **F2 (decisione 2026-09-19)**: scheda indipendente, stessa pin map di F1 più una seconda sonda DS18B20 per il
   bagno di glicole su GPIO27 (DQ) / GPIO23 (VDD commutato) — vedi
   [13_F2_SCHEDA_INDIPENDENTE.md](13_F2_SCHEDA_INDIPENDENTE.md). L'alternativa della scheda unica a 4 relè è in
   [12_HARDWARE_DUAL_F1_F2.md](12_HARDWARE_DUAL_F1_F2.md) (non adottata).

## 2. Pin map ESP32 WROOM (in uso su F1)

| Funzione | GPIO WROOM | Note cablaggio |
| --- | ---: | --- |
| DS18B20 DQ | 33 | Pull-up 4.7k a 3.3V |
| DS18B20 VDD SW | 25 | Alimentazione sensore via GPIO per power-cycle software |
| Relay Heat IN | 16 | Ingresso relay canale riscaldamento (active LOW) |
| Relay Cool IN | 17 | Ingresso relay canale raffreddamento (active LOW) |
| OLED SDA | 21 | I2C data |
| OLED SCL | 22 | I2C clock |

Alimentazioni consigliate:

1. DS18B20 VDD -> GPIO25 (SENSOR_PWR)
2. DS18B20 GND -> GND
3. OLED VCC -> 3.3V
4. OLED GND -> GND
5. Relay board VCC -> 5V (se modulo 5V)
6. Relay board GND -> GND comune
7. Pull-up 4.7k tra DS18B20 DQ e **GPIO25 (SENSOR_PWR)**, non 3.3V fisso

> Con il pull-up su 3.3V fisso, quando GPIO25 va LOW il DS18B20 ha VDD a massa: è il cablaggio
> della modalità parassita, quindi il sensore continua a funzionare alimentandosi dalla linea dati
> e il power-cycle software non lo resetta. Collegando il pull-up alla stessa alimentazione
> commutata il power-cycle è effettivo.
>
> **Guasto correlato trovato il 2026-09-19** (F1): l'ESP32 si resetta da solo (causa `poweron`) a
> 0,7-4,7 s dallo spegnimento del relè della pompa (220 V, 10-15 W) — verosimilmente un disturbo di
> commutazione sull'alimentazione o sul pin EN, non un problema del cablaggio del sensore descritto
> sopra. Vedi [06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630) per i
> dettagli e i rimedi in prova (condensatore su EN, snubber sulla pompa, alimentazione separata del
> modulo relè). **Da tenere presente fin dalla progettazione elettrica di F2.**
>
> **Sonda persa sulla scheda dei relè (ottobre 2026).** Il DS18B20 di F1 si è perso per ore l'1-2/10 e il
> 25-27/10. Il sospetto è il VDD della sonda sui 3,3 V fissi invece che su GPIO25, che rende inutile il
> power-cycle. Dal 2026-10-28 le sonde stanno su un nodo a parte, un ESP32-S3 Super Mini con 3 DS18B20 (F1,
> chiller, F2): cablaggio sulla millefori e pin in [`../../fermentatore_v7/README.md`](../../fermentatore_v7/README.md).
> I GPIO33/25 del WROOM di F1 non sono più letti (`SORGENTE_TEMPERATURA_REMOTA 1`).

## 3. Pin map ESP32-S3 Super Mini (non in uso oggi)

| Funzione | GPIO S3 Super Mini | Note cablaggio |
| --- | ---: | --- |
| DS18B20 DQ | 4 | Pull-up 4.7k a 3.3V |
| DS18B20 VDD SW | 5 | Alimentazione sensore via GPIO per power-cycle software |
| Relay Heat IN | 6 | Ingresso relay canale riscaldamento (active LOW) |
| Relay Cool IN | 7 | Ingresso relay canale raffreddamento (active LOW) |
| OLED SDA | 8 | I2C data |
| OLED SCL | 9 | I2C clock |

Note:

1. Questa mappa è coerente col configuratore automatico (§5).
2. Se la revisione S3 ha un pinout diverso, usare gli override pin del configuratore prima del
   cablaggio finale.
3. Il firmware usa `PIN_SENSOR_PWR` per il recovery automatico del DS18B20 con power-cycle.

## 4. Connettore pratico suggerito

Etichette morsettiera lato quadro:

1. SENSOR_DQ
2. SENSOR_3V3
3. SENSOR_GND
4. RELAY_HEAT_IN
5. RELAY_COOL_IN
6. RELAY_GND
7. OLED_SDA
8. OLED_SCL
9. OLED_3V3
10. OLED_GND

## 5. Configuratore automatico del target

Lo script `fermentatore_v6/tools/v6/configure_target.ps1` scrive in `fermentatore_v6/config.h`:

1. `DEVICE_FERMENTER_ID` (`f1` / `f2`)
2. `DEVICE_HARDWARE_TARGET` (`esp32-wroom` / `esp32-s3-supermini`)
3. la pin map (`PIN_DS18B20`, `PIN_SENSOR_PWR`, relè, OLED)

> Questo è il meccanismo **reale** di identità del fermentatore: `DEVICE_FERMENTER_ID` è una macro
> impostata a compile-time, non un valore letto da NVS a runtime (un'idea di provisioning via NVS era
> nel brief originale ma non è come funziona oggi — vedi
> [01_PROGETTO_INIZIALE_E_BACKLOG.md §7](01_PROGETTO_INIZIALE_E_BACKLOG.md#7-scostamenti-tra-il-piano-e-la-realtà)).
> Cambiare fermentatore target richiede quindi rigenerare `config.h` col configuratore e riflashare,
> non un semplice comando.

Uso base:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId wroom-f1
```

Esempi:

```powershell
# WROOM su F2
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId wroom-f2

# S3 Super Mini su F1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId s3-f1

# S3 Super Mini su F2 con override pin
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId s3-f2 -PinDs18b20 3 -PinSensorPwr 5 -PinHeatRelay 6 -PinCoolRelay 7 -PinOledSda 8 -PinOledScl 9
```

Comandi di allineamento firmware prima del flash (equivalenti, un target ciascuno):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId wroom-f1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId wroom-f2
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId s3-f1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/configure_target.ps1 -TargetId s3-f2
```

## 6. Checklist pre-collaudo

1. Verifica continuità GND comune.
2. Verifica assenza 5V diretti su pin GPIO ESP.
3. Verifica pull-up DS18B20 presente e collegato a SENSOR_PWR (non a 3.3V fisso).
4. Verifica che a boot i relè restino OFF.
5. Esegui i check V6 prima del flash:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/run_checks.ps1
```

6. Dopo il flash: build/flash in Arduino IDE con la board corretta, poi smoke test relè/sensore/MQTT
   (vedi [08_RUNBOOK_OPERATIVO.md §3](08_RUNBOOK_OPERATIVO.md)), e — per un fermentatore nuovo (F2) —
   l'intero collaudo automatico prima di mettere birra ([06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)).
