# Diagrammi di flusso — fermentatore_v5_autotuning

## FSM principale

```mermaid
flowchart TD
    BOOT(["Boot / setup()"])
    BOOT --> WIFI_S

    WIFI_S["WIFI\nWiFi.begin()"]
    WIFI_S -->|"Connesso"| OTA_S["OTA init + NTP sync"]
    WIFI_S -->|"Timeout 20 s"| RISC_INIT

    OTA_S --> MQTT_S["MQTT_CONN\nTLS :8883"]
    MQTT_S -->|"Connesso o Timeout 15 s"| RISC_INIT

    LOOP_NOTE>"ogni loop(): lettura sensore ogni 5 s\nMQTT loop + heartbeat ogni 10 s\nOTA handle"]

    subgraph RISC ["RISCALDAMENTO_TEST  ·  max 24 h"]
        RISC_INIT["autotuner_init\nSP 25 °C · amp 80 %"]
        RISC_ABORT{"T > 32 °C ?"}
        RISC_TIMEO{"Timeout 24 h ?"}
        RISC_SENS{"Sensore OK ?"}
        RISC_AT["autotuner_aggiorna(T)\nrelay_risc = ON / OFF"]
        RISC_OFFN["relay_risc OFF\n(attende prossimo loop)"]
        RISC_SAVE["≥ 3 cicli OK\nsalva kp · ki · Tu · Au\n(riscaldamento)"]

        RISC_INIT --> RISC_ABORT
        RISC_ABORT -->|"No"| RISC_TIMEO
        RISC_TIMEO -->|"No"| RISC_SENS
        RISC_SENS -->|"OK"| RISC_AT
        RISC_SENS -->|"Fail"| RISC_OFFN
        RISC_OFFN --> RISC_ABORT
        RISC_AT -->|"oscillazione in corso"| RISC_ABORT
        RISC_AT -->|"done"| RISC_SAVE
    end

    RISC_ABORT -->|"Sì → ABORT"| ERRORE
    RISC_TIMEO -->|"Sì"| PAUSA
    RISC_SAVE --> PAUSA

    PAUSA["PAUSA  5 min  ·  relay OFF"]
    PAUSA -->|"300 s"| FRIGO_INIT

    subgraph FRIGO ["RAFFREDDAMENTO_TEST  ·  max 2 h"]
        FRIGO_INIT["autotuner_init\nSP 14 °C · amp 80 %"]
        FRIGO_ABORT{"T < 8 °C ?"}
        FRIGO_TIMEO{"Timeout 2 h ?"}
        FRIGO_SENS{"Sensore OK ?"}
        FRIGO_AT["autotuner_aggiorna(T)\nrelay_frigo = ON / OFF"]
        FRIGO_OFFN["relay_frigo OFF\n(attende prossimo loop)"]
        FRIGO_SAVE["≥ 3 cicli OK\nsalva kp · ki · Tu · Au\n(raffreddamento)"]

        FRIGO_INIT --> FRIGO_ABORT
        FRIGO_ABORT -->|"No"| FRIGO_TIMEO
        FRIGO_TIMEO -->|"No"| FRIGO_SENS
        FRIGO_SENS -->|"OK"| FRIGO_AT
        FRIGO_SENS -->|"Fail"| FRIGO_OFFN
        FRIGO_OFFN --> FRIGO_ABORT
        FRIGO_AT -->|"oscillazione in corso"| FRIGO_ABORT
        FRIGO_AT -->|"done"| FRIGO_SAVE
    end

    FRIGO_ABORT -->|"Sì → ABORT"| ERRORE
    FRIGO_TIMEO -->|"Sì"| PUBBL
    FRIGO_SAVE --> PUBBL

    PUBBL["PUBBLICAZIONE\nMQTT retain:\n• autotuning/f1/risultati/riscaldamento\n• autotuning/f1/risultati/raffreddamento\n  { kp · ki · kd · Tu_s · Au_C }"]
    PUBBL --> DONE(["DONE — Idle\n(riavviare per nuovo test)"])

    ERRORE(["ERRORE — Idle\nrelay OFF"])

    RISC_ABORT -. "sensor_allarme_nuovo()\n(da loop, qualsiasi stato)" .-> ERRORE
```

---

## Algoritmo interno — `autotuner_aggiorna(T)`
*(Relay-feedback Åström–Hägglund)*

```mermaid
flowchart TD
    CALL(["autotuner_aggiorna(temp_reale)"])

    CALL --> FIRST{"Primo\ncampione?"}
    FIRST -->|"Sì"| INIT_REL["relay = ON se T < SP\nelse OFF\nreturn false"]
    FIRST -->|"No"| PEAKS["Aggiorna peak_max / peak_min\ndel semi-periodo corrente"]

    PEAKS --> CROSS{"T ha attraversato\nil setpoint?"}
    CROSS -->|"No"| EMIT["relay_on_out = relay\nreturn false"]

    CROSS -->|"Sì"| SAVE_X["Salva timestamp crossing"]
    SAVE_X --> AMP_CALC["Ampiezza semi-periodo:\nse eravamo sopra → peak_max − SP\nse eravamo sotto → SP − peak_min"]
    AMP_CALC --> STORE_AMP["Salva ampiezza in _amplitudes[]"]
    STORE_AMP --> RESET_PK["Reset peak_max / peak_min\nCambia relay: sopra→OFF, sotto→ON"]
    RESET_PK --> CHK_N{"_n_crossings ≥ 7\n(≥ 3 cicli completi)\ne _n_amplitudes ≥ 4 ?"}

    CHK_N -->|"No"| EMIT
    CHK_N -->|"Sì"| CALC_TU["Tu = media periodi\nsu coppie di crossings omologhi\n[0,2] [2,4] [4,6]"]
    CALC_TU --> CALC_AU["Au = media di tutte\nle ampiezze _amplitudes[]"]
    CALC_AU --> CHK_AU{"Au ≥ 0.01 °C ?"}
    CHK_AU -->|"No (segnale troppo debole)"| EMIT
    CHK_AU -->|"Sì"| ZN["Ziegler-Nichols PI:\nKu = 4 · amp / (π · Au)\nkp = 0.45 · Ku\nki = 0.45 · Ku · 1.2 / Tu\nkd = 0"]
    ZN --> DONE_AT(["return true\n→ gains pronti"])
```

---

## Note

- La FSM è **one-shot**: parte da `WIFI` e termina in `DONE` o `ERRORE`. Per rifare il test si riavvia l'ESP32.
- L'allarme sensore (`sensor_allarme_nuovo`) è controllato **fuori** dalla FSM in ogni `loop()`: da qualsiasi stato può portare in `ERRORE`.
- Se il test finisce in **timeout** (nessuna oscillazione convergente), lo stato avanza comunque (risc → PAUSA, frigo → PUBBLICAZIONE) ma il relativo risultato non viene pubblicato su MQTT.
- L'autotuner opera con il **sensore letto ogni 5 s** (`SENSOR_READ_INTERVAL_MS`), quindi ogni chiamata a `autotuner_aggiorna` lavora su una nuova misura ogni 5 secondi.
