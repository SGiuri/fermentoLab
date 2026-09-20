# 00. Indice della documentazione V6

**Versione documento**: 1.0 (2026-09-19) · **Stato**: corrente

Elenco di tutti i documenti V6, in ordine di lettura consigliato. Ogni file ha nell'intestazione una
riga "Versione documento" (numero + data) e uno stato: **corrente** (riflette il sistema oggi),
**storico** (piano/decisione passata, tenuta per contesto), o una via di mezzo indicata esplicitamente.

Per il resto del repository (build, flash, convenzioni di codice, versioni precedenti) vedi
[CLAUDE.md](../../../CLAUDE.md) nella radice del repository.

> **Percorsi nei comandi.** Dal 2026-09-20 la radice del repository è `FermentaLab/` e il firmware sta in
> `firmware/`. I comandi di questi documenti (`fermentatore_v6/tools/v6/...`, `fermentatore_v6/tests/...`) vanno
> lanciati dalla cartella `firmware/`, oppure prefissati con `firmware/` dalla radice.

| # | Documento | Stato | Argomento |
|---|---|---|---|
| 01 | [01_PROGETTO_INIZIALE_E_BACKLOG.md](01_PROGETTO_INIZIALE_E_BACKLOG.md) | storico | Piano architetturale, backlog a milestone, checklist di parità V4/V5, autotuning (storia), scostamenti tra piano e realtà |
| 02 | [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) | corrente | Pin map WROOM/S3, configuratore del target (`configure_target.ps1`), checklist pre-collaudo elettrica |
| 03 | [03_PROTOCOLLO_MQTT.md](03_PROTOCOLLO_MQTT.md) | corrente | Diagrammi di sequenza MQTT, pipeline di validazione comandi, come diagnosticare un comando rifiutato |
| 04 | [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md) | corrente | Contratto esaustivo topic/payload MQTT↔HA, entità ed elementi Lovelace attesi |
| 05 | [05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md) | corrente | Modalità (Produzione/Didattica/Collaudo), pacchetti HA, allarmi, dashboard, procedure di deploy nel Lab |
| 06 | [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md) | corrente | Architettura del collaudo automatico (AppDaemon), catalogo dei test, risultati, simulatore offline |
| 07 | [07_TEST_PLAN.md](07_TEST_PLAN.md) | corrente | Piano di test complessivo: test host, collaudo su banco, matrice target, KPI, criteri di uscita |
| 08 | [08_RUNBOOK_OPERATIVO.md](08_RUNBOOK_OPERATIVO.md) | corrente | Checklist di pre-deploy, deploy, smoke test, rollback, incident response |
| 09 | [09_TOOLING.md](09_TOOLING.md) | parziale | Toolchain di build info (manifest/generate/verify) — nota: disallineata dal firmware reale |
| 10 | [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) | vivo | Lavoro da fare, decisioni in sospeso, idee — include l'onboarding di F2 da zero |
| 11 | [11_TIMING_PROFILO.md](11_TIMING_PROFILO.md) | analisi/proposta | Come il firmware avanza lungo la curva di fermentazione, rischi (assenza rete, blackout e guasto del sensore durante una rampa) e alternative |
| 12 | [12_HARDWARE_DUAL_F1_F2.md](12_HARDWARE_DUAL_F1_F2.md) | alternativa non adottata | Scheda unica F1+F2 con 4 relè e 3 sonde: valutazione hardware, impatto su firmware e HA, migrazione (sonde e prove di banco ancora utili) |
| 13 | [13_F2_SCHEDA_INDIPENDENTE.md](13_F2_SCHEDA_INDIPENDENTE.md) | corrente | F2 su scheda indipendente con sonda del bagno di glicole: pin map, firmware, Home Assistant, migrazione |

## Percorso di lettura consigliato

- **Per usare Home Assistant su F1** (avviare una fermentazione, leggere gli allarmi): parti da
  [05_HOME_ASSISTANT_F1.md](05_HOME_ASSISTANT_F1.md).
- **Per collaudare F1 (o un domani F2) prima di mettere birra**: [06_COLLAUDO_F1.md](06_COLLAUDO_F1.md).
- **Per costruire il secondo fermentatore F2**: [10_PUNTI_APERTI.md §2](10_PUNTI_APERTI.md), poi
  [02_HARDWARE_E_TARGET.md](02_HARDWARE_E_TARGET.md) per la pin map.
- **Per la scheda di F2 con la sonda del bagno di glicole**: [13_F2_SCHEDA_INDIPENDENTE.md](13_F2_SCHEDA_INDIPENDENTE.md)
  (l'alternativa della scheda unica F1+F2 è valutata in [12](12_HARDWARE_DUAL_F1_F2.md), non adottata).
- **Per modificare il firmware o capire il contratto MQTT**: [04_CONTRATTO_HASS.md](04_CONTRATTO_HASS.md)
  e [03_PROTOCOLLO_MQTT.md](03_PROTOCOLLO_MQTT.md); [01_PROGETTO_INIZIALE_E_BACKLOG.md §7](01_PROGETTO_INIZIALE_E_BACKLOG.md#7-scostamenti-tra-il-piano-e-la-realtà)
  avverte di cosa è cambiato rispetto al piano iniziale.
- **Per fare un deploy o gestire un incidente**: [08_RUNBOOK_OPERATIVO.md](08_RUNBOOK_OPERATIVO.md).
- **Per sapere cosa manca o cosa decidere**: [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md).

## File generati (non modificare a mano)

Alcuni file fuori da `docs/` sono generati da script e vanno rigenerati dopo aver modificato la loro
sorgente (vedi [10_PUNTI_APERTI.md §7](10_PUNTI_APERTI.md)):

| File generato | Script | Sorgente |
|---|---|---|
| `hass_spec/packages/fermentatore_v6_f2_*.yaml` | `hass_spec/packages/genera_f2.py` | gli equivalenti `fermentatore_v6_f1_*.yaml` (tranne `_collaudo`, escluso di proposito) |
| `hass_spec/dashboards/f1_v6_collaudo_dashboard.yaml` | `hass_spec/dashboards/genera_collaudo.py` | catalogo dei test in `hass_spec/appdaemon/collaudo_f1/collaudo_motore.py` |
