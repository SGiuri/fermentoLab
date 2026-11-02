# Changelog - Fermentatore V7

## [v7.0.0] - 2026-10-27
### Sensori separati dagli attuatori

- **Motivo**: su F1 la sonda DS18B20 si è persa più volte (01/10, 25-26/10, 27/10). Il 27/10 nemmeno staccare la
  corrente l'ha recuperata. Il 26-27/10, con la sonda intermittente, la regolazione stimata ha raffreddato la
  witbier da 20,5 a 12,1 °C. In più, dal 19/09, il WROOM si resettava per i disturbi della pompa. La V7 sposta le
  sonde su una scheda a sé, lontana dai relè.
- **Nodo sensori** (`nodo_sensori_s3/`, ESP32-S3 Super Mini):
  - tre DS18B20 (F1, chiller, F2), ognuno sul suo bus con VDD da un GPIO; letture ogni 5 s pubblicate su
    `fermentazione/v7/sensori/temp/<id>`, con `ts` NTP e la causa quando una lettura è scartata;
  - power-cycle per sonda come la 6.5.1, non bloccante;
  - riavvio del nodo per una sonda persa da 10 min, al massimo 3 di fila;
  - OTA via hotspot `HotIoT` come la V6;
  - ping ogni 30 s con la diagnostica di ogni sonda, eventi su `evento`, comandi `ping`, `riavvia`,
    `power_cycle`.
- **Nodo attuatori F1** (`nodo_attuatori/`, ESP32 WROOM, dalla 6.5.1):
  - con `SORGENTE_TEMPERATURA_REMOTA 1` la temperatura arriva da `temp/f1` e passa per la stessa valutazione del
    DS18B20;
  - letture con `ts` più vecchio di 60 s scartate;
  - relè in sicurezza dopo 30 s senza letture, allarme `sensore_offline` dopo 5 min;
  - niente power-cycle e niente riavvii per la sonda;
  - topic, client MQTT, OTA e NVS come la 6.5.1: il profilo in corso prosegue dopo il flash.
- **Regolazione stimata**: la fase della finestra di 40 min segue `millis()` invece di ripartire dalla parte ON
  a ogni attivazione. Con una sonda intermittente (simulata come il 26-27/10) la pompa passa dal 26 % al 6 % del
  tempo.
- `esp/state`: nuovo campo `sorgente_temp`.
- **Test**: `test_attuatori/` compila lo sketch con la sorgente locale e con quella remota.
  - Nuovi scenari: `stima_intermittente`, `remoto_normale`, `remoto_muto`, `remoto_null`, `remoto_stantio`,
    `remoto_senza_rete`, `remoto_weekend`.
  - `profilo_pausa_sensore` ha la tolleranza ricavata da `SENSOR_MAX_ETA_MS`; `controllo_didattico` ammette
    D fino a 1,5 con la sorgente remota.
  - 60 scenari, tutti superati.
  - Compilati per ESP32-S3 (81 % della flash) e per ESP32 WROOM (87 %).
- **Home Assistant**: pacchetto `fermentatore_v7_nodo_sensori.yaml` (temperature, eventi nel registro, script) e
  dashboard *V7 Nodo sensori* con le viste *Sonde* e *Collegamento*.
