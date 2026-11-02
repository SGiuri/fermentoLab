# CLAUDE.md — Fermentatore ESP32

Guidance for Claude Code (or any agent/contributor) working in this repository. Renamed from
`AGENTS.md` and rewritten on 2026-09-19 to reflect the project as it is today, not as it was at
`v0.0.1` (2026-05-25).

> **Layout (2026-09-20).** The git repository root is `FermentaLab/` and contains `firmware/`
> (everything that used to be the repository: `fermentatore_v6/` and `obsolete/`), `homeassistant/`,
> `hardware/` and the repo-level files (`.github/`, `.githooks/`, `.gitignore`, `LICENSE`). Paths in this
> file are relative to the repository root. The scripts in `firmware/fermentatore_v6/tools/v6/` locate
> themselves relative to their own folder, so they work wherever `.git` lives.

## Project overview

ESP32 Arduino firmware for two independent fermentation temperature controllers, **F1** and **F2**,
each with dual relay control (heating + cooling), a DS18B20 sensor, an OLED display, MQTT/Home
Assistant integration and Over-The-Air updates. F1 and F2 run the same firmware, distinguished by a
compile-time `fermenter_id`.

- **Status (2026-11-02).** Two beers are in progress, and their records are in
  `firmware/fermentatore_v6/storico_birre/`:
  - **F1, witbier (since 2026-10-23).** Controlled by the F1 ESP32, which since 2026-10-28 runs the
    **V7 actuator node 7.0.0** (`firmware/fermentatore_v7/nodo_attuatori/`, derived from v6 6.5.1). It takes
    its temperature over MQTT from the **V7 sensor node** (ESP32-S3, 3 probes).
  - **F2, October lager.** Controlled by an Inkbird. Home Assistant only watches it, through the V7 probe and
    the iSpindel, on the "F2 Cotta" dashboard. F2 has no relays/ESP32 of its own yet.
  - The 2026-10-03 collaudo deadline is past: F1 went into production on 2026-10-02. Phase A of the collaudo
    has no recorded conclusion (see `docs/10_PUNTI_APERTI.md §1`).
  - Home Assistant setup: [`docs/05_HOME_ASSISTANT_F1.md §5`](firmware/fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md#5-dashboard).
- **Where to work.** `firmware/fermentatore_v6/` is still the home of the documentation, the HA packages and
  dashboards, the profiles and the collaudo, and of the 6.x single-board sketch (6.5.1; the fallback for F1).
  New firmware work goes into `firmware/fermentatore_v7/`, which has its own README and CHANGELOG. The next
  V7 step is a 4-relay actuator node that also drives F2.
- **`firmware/obsolete/`** holds the frozen v2-v5 line (`fermentatore_v2` … `fermentatore_v5_autotuning`),
  moved there on 2026-09-19. It is not being developed further; keep it only for reference. Do not
  add new features there.
- Full V6 documentation index: **[`firmware/fermentatore_v6/docs/00_INDICE.md`](firmware/fermentatore_v6/docs/00_INDICE.md)**
  — start there for anything beyond this file (hardware/pin map, MQTT/HA contract, Home Assistant
  setup, the automated collaudo suite, open items). This file (`CLAUDE.md`) stays high-level and
  points into `docs/` rather than duplicating it.

## Repository layout

| Path | Contents |
|---|---|
| `firmware/fermentatore_v6/fermentatore_v6/` | The active firmware sketch (`.ino` + `.cpp`/`.h` modules) |
| `firmware/fermentatore_v6/docs/` | All V6 documentation, numbered — see the index above |
| `firmware/fermentatore_v6/hass_spec/packages/` | Home Assistant YAML packages (MQTT entities, alarms, didactic/collaudo modes) for F1 (and generated F2 equivalents) |
| `firmware/fermentatore_v6/hass_spec/dashboards/` | Home Assistant Lovelace dashboards (some generated — see `docs/00_INDICE.md`) |
| `firmware/fermentatore_v6/hass_spec/appdaemon/collaudo_f1/` | The automated test-suite engine (AppDaemon app) that runs real tests against F1 |
| `firmware/fermentatore_v6/tests/host/` | Firmware unit/scenario tests, compiled with MSVC against Arduino/ESP32 mocks |
| `firmware/fermentatore_v6/tests/collaudo/` | Offline simulator that runs the real collaudo engine against a modeled fermenter |
| `firmware/fermentatore_v6/storico_birre/` | One JSON record per beer brewed (dates, plan profile snapshot, which entities measured it and when, readings, notes) for the HA "Storico birre" dashboard. The live copies are on the Lab in `/config/storico_birre/`, where notes added from the dashboard land; format in its `README.md` |
| `firmware/fermentatore_v6/tools/v6/` | Target configurator, build-info manifest tooling (see caveat below), E2E test script |
| `firmware/fermentatore_v7/` | Started 2026-10-27: sensors split from actuators. `nodo_sensori_s3/` = ESP32-S3 Super Mini reading 3 DS18B20 (F1, chiller, F2), publishing `fermentazione/v7/sensori/temp/<id>`, with per-probe power-cycle, limited node reboots and OTA. `nodo_attuatori/` = the 6.5.1 for the F1 WROOM, with its temperature taken from `temp/f1` (`SORGENTE_TEMPERATURA_REMOTA`), no probe power-cycle or reboots, and the stima window fix. Same v6 topics and NVS. `test_attuatori/` runs the host tests in both source modes. Own `hass_spec/`, README (wiring, flash procedure) and CHANGELOG. The 4-relay F1+F2 actuator is still to be written |
| `firmware/obsolete/fermentatore_v2` … `_v5_autotuning` | Frozen legacy versions — bang-bang, then bidirectional PID, then a standalone relay-feedback autotuner |
| `README.md` | Public overview for someone rebuilding the project from GitHub: hardware schematic, BOM, firmware and Home Assistant setup, testing, known issues. The old v2-v5 README is `firmware/obsolete/README.md` |
| `hardware/` | `schema_elettrico_f1.svg` (wiring schematic, generated by `genera_schema.py`) and `bom_f1.csv` |
| `homeassistant/` | Sanitized snapshot of the Lab `/config` files that sit outside packages/dashboards (`configuration.yaml`, `scripts.yaml`, `scripts_py/avvia_fermentazione_v6.py`, templates for secrets) and `legacy_v3/` |
| `MODALITA_ACCESSO_LAB.md` | How an agent reaches a Home Assistant instance (MCP, REST, SSH), with placeholders instead of addresses |

## Build & flash

- **Toolchain**: Arduino IDE — no PlatformIO, no `CMakeLists.txt`.
- **Flash via USB**: open `firmware/fermentatore_v6/fermentatore_v6/fermentatore_v6.ino`, select the ESP32
  board (`ESP32 Dev Module` for WROOM).
- **Flash via OTA**: the ESP32 checks for a specific hotspot **only at boot** (SSID/password in
  `secrets.h`); connect your PC to that same hotspot before uploading. On networks that block OTA
  traffic (common on corporate/managed WiFi), a personal phone hotspot is the reliable option — if
  the PC silently rejoins the main WiFi, OTA discovery stops finding the device.
- **Required libraries** (Arduino Library Manager): `PubSubClient`, `ArduinoJson` (v6.x), `OneWire`,
  `DallasTemperature`, `U8g2`.
- **Choosing a target** (F1/F2, WROOM/S3): `firmware/fermentatore_v6/tools/v6/configure_target.ps1` writes
  `DEVICE_FERMENTER_ID`, `DEVICE_HARDWARE_TARGET` and the pin map into `config.h` — see
  [`docs/02_HARDWARE_E_TARGET.md`](firmware/fermentatore_v6/docs/02_HARDWARE_E_TARGET.md). This is a
  compile-time choice, not a runtime one.
- **Host tests**: `powershell -File firmware/fermentatore_v6/tests/host/run_tests.ps1` compiles the real
  sketch sources against mocks (MSVC Build Tools required) and runs 30 scenarios including 48h
  thermal simulations. The V7 actuator has its own suite, `firmware/fermentatore_v7/test_attuatori/run_tests.ps1`
  (60 scenarios: local and remote temperature source) — see [`docs/07_TEST_PLAN.md`](firmware/fermentatore_v6/docs/07_TEST_PLAN.md).
- **On-hardware collaudo**: for changes that need verifying against real F1 behavior (timing,
  sensor recovery, relay switching, MQTT contract edge cases), use the automated suite described in
  [`docs/06_COLLAUDO_F1.md`](firmware/fermentatore_v6/docs/06_COLLAUDO_F1.md) rather than re-deriving manual
  test steps — it already has 19 scripted tests with numeric pass criteria.

## Versioning & changelog

Policy in [`.github/instructions/versioning.instructions.md`](.github/instructions/versioning.instructions.md):
every substantial change to `fermentatore_v6` gets a `firmware/fermentatore_v6/CHANGELOG.md` entry, and
`FIRMWARE_VERSION` in `firmware/fermentatore_v6/fermentatore_v6/config.h` follows it. Keep these three files
in sync on a version bump: `config.h`, `display.cpp` (OLED splash), `connettivita.cpp`
(`"firmware"` field in the `esp/state` payload).

> **Caveat**: `firmware/fermentatore_v6/v6_manifest.json` and its `generate_build_info.ps1`/
> `verify_build_info.ps1` tooling are a *separate*, currently-stale mechanism — the manifest still
> says `6.1.4` while the real v6 firmware is `6.5.1` in source (F1 runs the V7 actuator `7.0.0`), and these scripts never read or write
> `FIRMWARE_VERSION` in `config.h`. Don't rely on `verify_build_info.ps1` passing as a signal that
> the version is correct. See [`docs/09_TOOLING.md`](firmware/fermentatore_v6/docs/09_TOOLING.md) and
> [`docs/10_PUNTI_APERTI.md`](firmware/fermentatore_v6/docs/10_PUNTI_APERTI.md).

The root `CHANGELOG.md` is a separate, stale log from the initial 2026-05-25 scaffolding of the V6
planning documents (schema `v0.0.x`) — it is not the live changelog; `firmware/fermentatore_v6/CHANGELOG.md`
is.

## Naming conventions

The firmware codebase uses **Italian names** for domain concepts — this is intentional, keep it
consistent when adding code.

| Pattern | Example (from `fermentatore_v6`) |
|---------|-----------------------------------|
| `module_verb()` functions | `controller_apply_inputs()`, `mqtt_pubblica_controllo()` |
| `g_` prefix for globals | `g_controller`, `g_config`, `g_sensore`, `g_setpoint` |
| PascalCase structs | `ControllerState`, `ControllerConfig`, `ProfiloFermentazione`, `SensorState` |
| Italian domain names | `riscaldatore`, `refrigeratore`, `isteresi`, `setpoint`, `uscite_rele`, `fallback_attivo` |

## Module map (`firmware/fermentatore_v6/fermentatore_v6/`)

| Module | Role |
|--------|------|
| `fermentatore_v6.ino` | Boot sequence, main loop, MQTT command dispatch, glue between modules |
| `controller` | Shared control state/config and the P/PI/PD/PID math (`apply_pid_family`): anti-windup, filtered derivative on measurement, deadband, 600s PWM window |
| `controller_onoff` | on_off, on_off_hyst_sym, on_off_hyst_asym — latched hysteresis |
| `uscite_rele` | The *only* module that writes the relay GPIOs — enforces min ON/OFF times, direction-change delay, NaN-safe shutoff |
| `sensor_watchdog` | DS18B20 read/validate cycle, incoherent-oscillation filter, power-cycle recovery via `PIN_SENSOR_PWR` |
| `command_processor` | Parses/validates every `ha/cmd/*` payload, produces `reason_code`s |
| `connettivita` | WiFi + MQTT lifecycle, LWT, heartbeat (`esp/state`, `esp/controllo`), acks |
| `profilo` | Fermentation profile JSON parsing, ramp interpolation, NVS persistence, its own FSM (`STANDBY`/`RUNNING`/`HOLDING`) |
| `display` | U8g2 OLED rendering |
| `autotune` / `pwm` | Relay-feedback autotuning and its PWM helper — **implemented but disabled** (`AUTOTUNE_ABILITATO 0` in `config.h`); see [`docs/01_PROGETTO_INIZIALE_E_BACKLOG.md §6`](firmware/fermentatore_v6/docs/01_PROGETTO_INIZIALE_E_BACKLOG.md#6-autotuning--storia-e-stato) |
| `log` | Dual-channel logging (Serial + MQTT `esp/log`) |
| `diag_reset` | Reset diagnostics (6.4.2): hardware reset code, last loop tick and last relay switch kept in RTC memory across resets, per-section loop timing; published on `esp/diag` |
| `regolazione_stimata` | Estimated control with a lost sensor (6.5.0): learns the thermal load (relay duty, mean temperature, slope) over the last 2-4 h of normal control and drives the relays open-loop from it when the DS18B20 is lost, for at most 72 h; persisted in RTC and NVS; published as `stima` in `esp/state`. Home Assistant watches it with the iSpindel (`fermentatore_v6_f1_ispindel.yaml`) |

Note there are **two FSMs**: `RuntimeState` (`STOPPED`/`RUNNING`/`AUTOTUNING`, in `controller.h`) for
whether the controller is active, and a separate profile FSM (`STANDBY`/`RUNNING`/`HOLDING`, in
`profilo.cpp`) for where the fermentation profile is in time.

## Hardware

- Relay logic is **ACTIVE LOW** (`RELAY_ON = LOW`, `RELAY_OFF = HIGH`).
- Full pin map for WROOM (in use on F1) and S3 Super Mini (not in use), plus the target configurator
  and a pre-collaudo electrical checklist: [`docs/02_HARDWARE_E_TARGET.md`](firmware/fermentatore_v6/docs/02_HARDWARE_E_TARGET.md).
- **F2 has no hardware yet** — building it (schematic, BOM, enclosure, wiring, provisioning, HA
  config, collaudo) is an open task with its own checklist: [`docs/10_PUNTI_APERTI.md §2`](firmware/fermentatore_v6/docs/10_PUNTI_APERTI.md).
- A real fault was found and is being fixed on F1 (2026-09-19): the ESP32 spontaneously resets a
  fraction of a second after the cooling pump relay switches off, likely electrical noise on the
  supply or the EN pin. Keep this in mind for any new relay-driving hardware (F2, or F1 repairs) —
  see [`docs/06_COLLAUDO_F1.md §7`](firmware/fermentatore_v6/docs/06_COLLAUDO_F1.md#7-risultati-della-prima-sessione).
- F1's DS18B20, wired to the relay board, was lost for hours several times: 01-02/10, 25-27/10. GPIO
  power-cycles and reboots did not help, and only a full power-off sometimes did. The suspected cause is VDD on
  fixed 3.3 V instead of GPIO25, and the dedicated probe in v6 was never root-caused. That is why V7 moved the
  probes to a separate sensor node. **Never publish by hand on `fermentazione/v7/sensori/temp/f1`**: the F1
  actuator takes it as a real measurement.

## MQTT / Home Assistant

- Full topic and payload contract: [`docs/04_CONTRATTO_HASS.md`](firmware/fermentatore_v6/docs/04_CONTRATTO_HASS.md).
  Sequence diagrams and the command-validation pipeline: [`docs/03_PROTOCOLLO_MQTT.md`](firmware/fermentatore_v6/docs/03_PROTOCOLLO_MQTT.md).
- **Mailbox pattern**: the MQTT callback only records the incoming command; the main loop parses and
  acts on it. Never call blocking MQTT/WiFi APIs from inside the callback.
- Home Assistant setup for F1 (modes, packages, alarms, dashboards, deploy procedure into the Lab
  instance): [`docs/05_HOME_ASSISTANT_F1.md`](firmware/fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md).

### Working with Home Assistant instances

This project actively drives one or more real Home Assistant instances over the network (dashboards,
MQTT commands, automations) — treat any HA access as touching a real system, not a sandbox.

- **There is a lab/bench instance and a separate personal/production instance.** Unless the
  repository owner explicitly says otherwise in the moment, only ever read from or write to the lab
  instance. Never call tools or send commands against the personal instance.
- The MQTT broker the lab instance talks to is the **same broker used by real hardware** (F1). A
  command published from Home Assistant reaches the physical device — commands are fine for bench
  testing, but never enable MQTT discovery on that broker (it could leak into the personal instance).
- Back up the Home Assistant instance (Supervisor → Backup, or the API) before changing its
  configuration, packages, or dashboards.
- `secrets.h` (per firmware target) and any Home Assistant tokens are **never committed**. `secrets.h`
  is gitignored — use `secrets.h.template` in each version folder as a starting point.

### Secrets and publishing

The repository is meant to be published on GitHub. Before adding or committing anything:

- **Never write real credentials, tokens, private IPs (Tailscale `100.x`, LAN `10.x`/`192.168.x`), e-mail
  addresses, hostnames of the broker or of internal servers** into a tracked file, doc, script or
  commit message. Use placeholders (`<LAB_IP>`, `<your-cluster-id>.s1.eu.hivemq.cloud`, …).
- Real values live only in ignored files: `firmware/**/secrets.h`, `homeassistant/secrets.yaml`,
  `*.local.md` (e.g. `MODALITA_ACCESSO_LAB.local.md`, which maps the placeholders to the owner's real
  addresses and keys), `.env`. Add a template next to every new secrets file.
- Before a push, scan the working tree **and the history** for the actual secret values (a value removed
  in a later commit is still published by the older one). If history is dirty, rewrite it and rotate the
  credential.
- Home Assistant details of the Lab (URLs, MCP server names, SSH key): see `MODALITA_ACCESSO_LAB.md`.

## Testing

- **Firmware host tests** (fast, no hardware): [`docs/07_TEST_PLAN.md §1.1`](firmware/fermentatore_v6/docs/07_TEST_PLAN.md).
- **Automated on-hardware collaudo** (real F1, hours to days): a resumable AppDaemon app runs
  scripted tests over real MQTT and judges the result from Home Assistant's history with numeric
  criteria. See [`docs/06_COLLAUDO_F1.md`](firmware/fermentatore_v6/docs/06_COLLAUDO_F1.md) — it also has an
  offline simulator (`tests/collaudo/simula_collaudo.py`) that runs the *same* engine against a
  modeled fermenter, useful for validating engine changes in seconds instead of hours before trying
  them on real hardware.

## Known gotchas

- `config.h` is UTF-8 **with a BOM**, CRLF line endings. Preserve both when editing — a plain
  find-and-replace that rewrites the file as LF/no-BOM will make the next diff huge and can trip up
  the Arduino toolchain's encoding assumptions on some setups.
- If an editor (e.g. VS Code) has a firmware or HA config file open while it's being edited outside
  that editor, the editor can silently overwrite the change on its next auto-save. Before editing
  files that might be open elsewhere, and before flashing, verify the on-disk content and `git
  status` match what you expect.

## See also

- [`firmware/fermentatore_v6/docs/00_INDICE.md`](firmware/fermentatore_v6/docs/00_INDICE.md) — full documentation index
- [`firmware/fermentatore_v6/docs/10_PUNTI_APERTI.md`](firmware/fermentatore_v6/docs/10_PUNTI_APERTI.md) — open items, including F2 onboarding
- [`README.md`](README.md) — public project overview (hardware, BOM, firmware, Home Assistant, testing)
- [`MODALITA_ACCESSO_LAB.md`](MODALITA_ACCESSO_LAB.md) — reaching a Home Assistant instance from an agent
