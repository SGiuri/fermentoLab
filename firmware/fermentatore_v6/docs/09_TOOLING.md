# 09. Tooling V6

**Versione documento**: 1.1 (2026-09-19, baseline 2026-05-25) · **Stato**: parzialmente storico —
vedi nota sotto
**Scopo**: standardizzare toolchain, automazioni e check gate per `fermentatore_v6`.

> **Attenzione — il manifest è disallineato dal firmware reale.** `v6_manifest.json` dichiara
> `firmware_version: 6.1.4`, ma il firmware reale (`fermentatore_v6/config.h`) è alla 6.3.0. Il
> generator/verifier (§1-2) controllano solo che `generated/v6_build_info.*` sia coerente col
> manifest: **non leggono né scrivono `FIRMWARE_VERSION` in `config.h`**, quindi non si accorgono
> del disallineamento. La versione reale del firmware segue una policy separata e più semplice,
> descritta in `.github/instructions/versioning.instructions.md` e in
> [08_RUNBOOK_OPERATIVO.md §1](08_RUNBOOK_OPERATIVO.md). Vedi
> [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md) per la decisione da prendere (aggiornare il manifest,
> collegarlo davvero al firmware, o rimuovere questa toolchain se non serve più).

## 1. Artefatti e percorsi

1. Manifest: `fermentatore_v6/v6_manifest.json`
2. Generator: `fermentatore_v6/tools/v6/generate_build_info.ps1`
3. Verifier: `fermentatore_v6/tools/v6/verify_build_info.ps1`
4. Hook installer: `fermentatore_v6/tools/v6/install_hooks.ps1`
5. Generated metadata: `fermentatore_v6/generated/v6_build_info.h`

## 2. Workflow locale standard

1. Aggiorna `v6_manifest.json`
2. Esegui generator
3. Esegui verifier
4. Build firmware target
5. Commit (hook ripete verify)

Comandi:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/generate_build_info.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/verify_build_info.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tools/v6/install_hooks.ps1
```

## 3. CI minima consigliata

1. Stage lint/docs
2. Stage manifest verify
3. Stage build matrix
4. Stage test matrix
5. Stage package artifacts

## 4. Build matrix

1. WROOM-F1 — **reale**, hardware esistente
2. WROOM-F2 — pianificato, hardware non ancora costruito (vedi [10_PUNTI_APERTI.md](10_PUNTI_APERTI.md))
3. S3-F1 — non utilizzato al momento
4. S3-F2 — non utilizzato al momento

## 5. Quality gates

1. verify_build_info must pass
2. build all matrix targets must pass
3. safety tests must pass
4. contract tests MQTT/HASS must pass

## 6. Versioning policy V6

1. Ogni modifica sostanziale in `fermentatore_v6` aggiorna `fermentatore_v6/CHANGELOG.md`
2. `FIRMWARE_VERSION` in `fermentatore_v6/config.h` deve seguire il changelog
3. `display.cpp` e `connettivita.cpp` devono esporre la versione firmware

## 7. Tooling roadmap

1. Aggiungere script check topic schema JSON
2. Aggiungere contract test runner offline
3. ~~Aggiungere benchmark script pre/post autotuning~~ — non prioritario, autotuning disabilitato
4. Automatizzare export report KPI per HASS
5. **Nuovo (2026-09-19)**: il collaudo automatico ([06_COLLAUDO_F1.md](06_COLLAUDO_F1.md)) copre già
   in buona parte i punti 1-2 e 4 per F1, in modo diverso da come erano stati immaginati qui (test
   reali su hardware invece di contract test offline). Da valutare se questa roadmap va aggiornata o
   chiusa a favore del collaudo.
