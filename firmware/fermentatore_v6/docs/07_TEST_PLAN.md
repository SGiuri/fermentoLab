# 07. Piano di test V6

**Versione documento**: 1.1 (2026-09-19, baseline 2026-05-25) · **Stato**: corrente
**Ambito**: validazione firmware `fermentatore_v6` per WROOM/S3, F1/F2, controllo runtime. L'autotuning
(sezione 3.4) è disabilitato nel firmware (`AUTOTUNE_ABILITATO 0`): vedi
[01_PROGETTO_INIZIALE_E_BACKLOG.md](01_PROGETTO_INIZIALE_E_BACKLOG.md#6-autotuning-storia-e-stato).

## 1. Test pyramid

1. Unit tests logici
   - controller mode logic
   - parser payload
   - validation gates
2. Integration tests firmware
   - FSM + MQTT flow
   - device identity/namespacing
3. System tests su banco
   - relay behavior
   - sensore fault handling
   - autotuning lifecycle
4. Soak/fault injection
   - rete instabile
   - broker restart
   - reboot inattesi

### 1.1 Test host automatici (firmware 6.2.0)

`tests/host/run_tests.ps1` compila i sorgenti reali dello sketch con mock Arduino/ESP32
(Visual Studio Build Tools, MSVC) e simula tempo, relè, DS18B20, NVS e broker MQTT:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tests/host/run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tests/host/run_tests.ps1 -Scenario sensore_guasto -Dettagli
```

Scenari: stadio uscite (tempi minimi, divieti, interlock, finestra duty), sensore guasto,
recupero, glitch singolo, comandi retained/vecchi/duplicati, profilo con `avvio_epoch = 0`,
validazione profilo, JSON di stato, WiFi assente 2 h, ripiego da hotspot, persistenza del modo
attraverso un riavvio, simulazioni termiche di 48 h per on/off, PI e PID.
Non sostituiscono le prove su banco (sezione 3): non coprono temporizzazioni reali di rete,
TLS, OneWire e I2C.

### 1.2 Collaudo automatico su banco reale (F1, da 2026-09-19)

Le prove su banco della sezione 3 sono automatizzate, per F1, dall'app AppDaemon descritta in
[06_COLLAUDO_F1.md](06_COLLAUDO_F1.md): test reali via MQTT su un fermentatore senza birra, con criteri
misurati e verdetto complessivo. Copre parte di 3.1, 3.2 e 3.3 sotto, più test specifici per F1
(commutazioni della pompa, modi di controllo in freddo). Non sostituisce i test host (troppo lenta,
gira su hardware vero) né le prove manuali non ancora coperte. Vedi anche
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md) e [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

## 2. Matrice target

1. WROOM-F1
2. WROOM-F2
3. S3-F1
4. S3-F2

Ogni suite critica deve girare almeno su WROOM-F1 e S3-F1; smoke test su tutte le combinazioni.
Stato al 2026-09-19: solo WROOM-F1 esiste come hardware reale (F1). F2 non è ancora costruito
(vedi [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) — guida di onboarding del secondo fermentatore).

## 3. Suite obbligatorie

## 3.1 Control engines

1. on_off
2. on_off_hyst_sym
3. on_off_hyst_asym
4. P
5. PI
6. PD
7. PID

Casi minimi per ogni engine:

1. temp sotto setpoint
2. temp sopra setpoint
3. temp in banda neutra
4. transizione mode con reset stato

## 3.2 Sicurezza

1. fault sensore -> relay OFF
2. stop command -> relay OFF immediato
3. cambio mode fuori STOPPED -> rifiuto con reason_code
4. boot cold -> relay OFF iniziale

## 3.3 MQTT/HASS contract

1. payload validi -> ack ok
2. payload invalidi -> ack error + reason_code
3. request_id correlato in ack
4. state periodico con campi obbligatori

## 3.4 Autotuning (disabilitato nel firmware, storico)

> `AUTOTUNE_ABILITATO 0` in `config.h`: ogni comando `autotune_*` è rifiutato con `autotune_disabled`.
> La suite sotto resta come riferimento se l'autotuning verrà riattivato; non fa parte dei criteri
> di uscita correnti. Vedi [01_PROGETTO_INIZIALE_E_BACKLOG.md §6](01_PROGETTO_INIZIALE_E_BACKLOG.md#6-autotuning-storia-e-stato).

1. start in stato valido
2. stop manuale
3. timeout sessione
4. result quality score presente
5. apply con quality sotto soglia -> rifiuto
6. apply con quality sopra soglia -> accettato in STOPPED

## 4. Regression suite

1. Profilo fermentazione multi-step
2. reconnect MQTT durante RUNNING
3. reboot in RUNNING con ripristino stato
4. coesistenza F1/F2 senza collisioni topic

## 5. KPI misurati

1. errore medio assoluto
2. overshoot max
3. settling time
4. numero commutazioni relay/h
5. reconnect success rate
6. autotune success rate — n/a, autotuning disabilitato (§3.4)

## 6. Exit criteria

1. zero P0/P1 aperti
2. safety suite 100% green
3. contract tests 100% green
4. soak test >= 24h senza fault critici — coperto in parte dal collaudo automatico (§1.2), che arriva
   fino a 3 giorni per test
5. rollback test completato
