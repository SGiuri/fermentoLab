# 08. Runbook operativo V6

**Versione documento**: 1.1 (2026-09-19, baseline 2026-05-25) · **Stato**: corrente

Procedure sintetiche per deploy, smoke test, rollback e incident response. Per il collaudo esteso
(giorni, non minuti) vedi [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md); per la configurazione HA vedi
[05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md); per i comandi di build vedi [../../CLAUDE.md](../../../CLAUDE.md).

## 1. Pre-deploy checklist

1. `CHANGELOG.md` di `fermentatore_v6` aggiornato con la nuova versione.
2. `FIRMWARE_VERSION` in `config.h` allineato al CHANGELOG (policy in
   `.github/instructions/versioning.instructions.md`), e coerente in `display.cpp` e
   `connettivita.cpp` (solo questi tre file vanno toccati per un bump di versione).
3. Se cambia il target (F1/F2, WROOM/S3): `configure_target.ps1` eseguito, vedi
   [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md).
4. Test host verdi: `tests/host/run_tests.ps1` (18 scenari, vedi
   [07_TEST_PLAN.md §1.1](07_TEST_PLAN.md)).

> `tools/v6/generate_build_info.ps1` / `verify_build_info.ps1` verificano solo la coerenza interna tra
> `v6_manifest.json` e `generated/v6_build_info.*`: non leggono e non scrivono `FIRMWARE_VERSION` in
> `config.h`. Sono uno strumento separato, non collegato al firmware reale — vedi
> [09_TOOLING.md](09_TOOLING.md) e [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

## 2. Deploy F1/F2

1. Build del target (Arduino IDE, board ESP32 Dev Module per WROOM).
2. Flash: via USB, oppure via OTA solo connettendo il PC all'hotspot definito in `secrets.h`
   (la rete IoT aziendale/domestica blocca l'OTA) — l'ESP32 sceglie l'hotspot solo al boot.
   **Mai il file `merged.bin`** su un ESP32 con un profilo in corso: sovrascrive la NVS. Dal 2026-10-28 F1 ha
   il nodo attuatori V7: ordine del flash (prima il nodo sensori, poi gli attuatori), controlli e ritorno alla
   6.5.1 in [`../../fermentatore_v7/README.md`](../../fermentatore_v7/README.md#flash-del-2026-10-28).
3. Verifica: `esp/availability` = `online`, primo `esp/state` retained ricevuto, heartbeat ogni 30 s.
4. Ripeti per l'altro fermentatore, se presente (oggi solo F1 ha hardware — vedi
   [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) per l'onboarding di F2).

## 3. Smoke test post deploy

1. `ha/cmd/stop` / `ha/cmd/start` con ack `ok`.
2. `ha/cmd/control_set` in `on_off_hyst_sym` con ack `ok`.
3. Relè coerenti con `esp/state` (`riscaldatore`/`refrigeratore`) e con `esp/controllo` (6.3.0+).
4. Allarme simulato (isteresi alzata, vedi [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md), test
   *Allarme se il freddo si ferma*) e verifica del percorso di errore/ack.

Per una verifica più a fondo (ore/giorni, non minuti), usare il collaudo automatico invece di ripetere
questi passi a mano: vedi [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md).

## 4. Autotuning operativo — disabilitato, storico

`AUTOTUNE_ABILITATO 0` in `config.h`: ogni comando `autotune_*` viene rifiutato con
`autotune_disabled`. La procedura sotto è storica (com'era pensata prima della disattivazione) e non
va seguita finché l'autotuning non torna attivo — vedi
[01_PROGETTO_INIZIALE_E_BACKLOG.md §6](01_PROGETTO_INIZIALE_E_BACKLOG.md#6-autotuning-storia-e-stato).

1. porta device in STOPPED
2. avvia autotune_start
3. monitora esp/autotune_state
4. se quality valida -> autotune_apply
5. start e verifica trend

## 5. Rollback

1. `ha/cmd/stop`.
2. Flash della versione stabile precedente (binario o sketch salvato).
3. Ripristino parametri di controllo da backup Home Assistant (Supervisor → Backup, vedi
   [05_HOME_ASSISTANT_F1.md §7](05_HOME_ASSISTANT_F1.md) per l'elenco dei backup noti del Lab).
4. Verifica heartbeat e comandi base (§3).

## 6. Incident response

1. **Assenza heartbeat**: verificare WiFi/broker; controllare `binary_sensor.f1_v6_online` (LWT) e
   `binary_sensor.f1_v6_dati_in_arrivo`.
2. **Allarme sensore**: fail-safe relè OFF (automatico nel firmware) e reboot controllato se persiste;
   vedi anche `sensor.f1_v6_riavvii_spontanei` per riavvii non richiesti.
3. **Commutazioni anomale / riavvii spontanei**: vedi
   [06_COLLAUDO_F1.md §7](06_COLLAUDO_F1.md#7-risultati-della-prima-sessione-fase-a-avviata-2026-09-18-2350-fw-630) — un guasto di questo tipo
   è stato trovato e tracciato il 2026-09-19 (reset dell'ESP32 alla commutazione della pompa).
