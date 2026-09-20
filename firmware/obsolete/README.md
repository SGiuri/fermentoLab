# Legacy firmware v2–v5 (frozen)

This folder keeps the earlier firmware generations — bang-bang (v2–v4), bidirectional PID (v5) and the
standalone relay-feedback autotuner (v5_autotuning). They are **not developed any further**; the active
system is [`../fermentatore_v6`](../fermentatore_v6/) and the project overview is the
[repository README](../../README.md).

The text below is the original README of that line. Paths such as `obsolete/fermentatore_v4/` or
`fermentatore_v6/` refer to the sibling folders of this file (`fermentatore_v4/`, `../fermentatore_v6/`).
Anything that says "your credentials" applies to the `secrets.h` file you create yourself from the
`secrets.h.template` in each folder — no credentials are stored in the repository.

---

## Features (legacy v2-v5, see note above)

- **Bang-bang thermostat** (v4, production) with hysteresis and compressor protection
- **PID bidirectional control** (v5, in development) with asymmetric gains for heater and cooler
- **Ziegler–Nichols relay-feedback autotuner** (v5_autotuning) for automatic PID gain calculation
- **Fermentation profile management**: multi-step JSON profiles with linear ramps, stored in NVS
- **Home Assistant integration**: real-time telemetry, remote commands, LWT-based availability sensor
- **Over-The-Air updates** via personal hotspot
- **OLED display** (SSD1306 128×64): current temp, setpoint, step name (scrolling), relay status, WiFi icon
- **Robust FSM**: WiFi reconnect, MQTT reconnect, sensor watchdog, offline duty-cycle fallback

---

## Hardware

### Bill of Materials

| Component | Description |
|-----------|-------------|
| ESP32 DevKit v1 | WROOM32 class, any vendor |
| DS18B20 | OneWire digital temperature sensor |
| 4.7 kΩ resistor | DS18B20 pull-up to 3.3 V |
| 2-channel relay module | Active LOW, 5 V supply |
| SSD1306 OLED | 128×64 I2C (0x3C or 0x3D) |
| Heating belt | 30 W silicone heating belt |
| Cooling pump + glycol chiller | 1/5 HP glycol chiller with circulation pump |

### Pin Map

| GPIO | Function |
|------|----------|
| 33 | DS18B20 data (OneWire) |
| 16 | Heat relay — **Active LOW** |
| 17 | Cool relay — **Active LOW** |
| 21 | I2C SDA (OLED) |
| 22 | I2C SCL (OLED) |

> **Important:** relay logic is **ACTIVE LOW** (`RELAY_ON = LOW`, `RELAY_OFF = HIGH`).

### Wiring Diagram

```
ESP32 3V3 ──┬──[4.7kΩ]──── DS18B20 DQ ── GPIO33
            └──────────── DS18B20 VDD
ESP32 GND ─────────────── DS18B20 GND

ESP32 GPIO16 ──── Relay IN1 (heat)
ESP32 GPIO17 ──── Relay IN2 (cool)
Relay VCC  ──── 5V
Relay GND  ──── ESP32 GND

ESP32 GPIO21 ──── OLED SDA
ESP32 GPIO22 ──── OLED SCL
ESP32 3V3   ──── OLED VCC
ESP32 GND   ──── OLED GND
```

---

## Project Versions

| Version | Status | Control | Key Features |
|---------|--------|---------|--------------|
| [`fermentatore_v6`](../fermentatore_v6/) | **Active** | on_off, hysteresis, P/PI/PD/PID | Two independent fermenters (F1/F2), full MQTT/HA contract, automated on-hardware test suite. See [`fermentatore_v6/docs/00_INDICE.md`](../fermentatore_v6/docs/00_INDICE.md) |
| [`obsolete/fermentatore_v2`](fermentatore_v2/) | Legacy, frozen | Bang-bang | First working prototype, Mosquitto MQTT |
| [`obsolete/fermentatore_v3`](fermentatore_v3/) | Legacy, frozen | Bang-bang | HiveMQ Cloud TLS, U8g2 OLED, duty-cycle fallback |
| [`obsolete/fermentatore_v4`](fermentatore_v4/) | Legacy, frozen | Bang-bang + hysteresis | Last v4-line production build before v6; OTA hotspot, WiFi event-driven, display WiFi icon, min-cycle lockout |
| [`obsolete/fermentatore_v5`](fermentatore_v5/) | Legacy, frozen | Bidirectional PID | Replaced by v6's unified controller (on_off/P/PI/PD/PID in one firmware) |
| [`obsolete/fermentatore_v5_autotuning`](fermentatore_v5_autotuning/) | Legacy, frozen | Relay-feedback | One-shot Ziegler–Nichols autotuner, publishes Kp/Ki/Kd; ported into v6 and then disabled there (`AUTOTUNE_ABILITATO 0`) |

The sections below (wiring, MQTT reference, profile JSON, autotuning) describe the legacy v2-v5 line
and remain accurate for browsing `obsolete/`, but do not apply to `fermentatore_v6`, whose topic
schema, payloads and algorithms differ — see [`fermentatore_v6/docs/00_INDICE.md`](../fermentatore_v6/docs/00_INDICE.md).

---

## Getting Started

### Prerequisites

1. [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. ESP32 board support — install via Boards Manager: **`esp32` by Espressif Systems**
3. Required libraries (Arduino Library Manager):
   - **PubSubClient** — MQTT client
   - **ArduinoJson** v6.x — JSON parsing
   - **OneWire** — DS18B20 protocol
   - **DallasTemperature** — DS18B20 wrapper
   - **U8g2** — SSD1306 OLED rendering

### Setup

```bash
git clone https://github.com/YOUR_USERNAME/esp32-fermentation-controller.git
cd esp32-fermentation-controller
```

For each version you want to build, copy the secrets template (legacy versions are under `obsolete/`):

```bash
cp obsolete/fermentatore_v4/secrets.h.template obsolete/fermentatore_v4/secrets.h
```

Edit `secrets.h` and fill in your credentials:

```cpp
#define HOTSPOT_PASS  "your_hotspot_password"
#define OTA_PASSWORD  "your_ota_password"
#define MQTT_SERVER   "your-broker.s1.eu.hivemq.cloud"
#define MQTT_USER     "your_mqtt_user"
#define MQTT_PASS     "your_mqtt_password"
#define WIFI_MAC_PASSWORD_MAP \
  if (macAddress == "AA:BB:CC:DD:EE:FF") { return "your_wifi_password"; }
```

> **Find your ESP32 MAC address:** flash any sketch that calls `Serial.println(WiFi.macAddress())` once.

Also check `config.h` for these non-sensitive settings:

| Define | Default | Description |
|--------|---------|-------------|
| `WIFI_SSID` | `"IoT"` | Main WiFi network name |
| `HOTSPOT_SSID` | `"HotIoT"` | Hotspot name for OTA updates |
| `MQTT_PORT` | `8883` | MQTT broker port (TLS) |
| `MQTT_CLIENT_ID` | `"esp32_f1_v4"` | Unique client identifier |

### Flash via USB

1. Open `obsolete/fermentatore_v4/fermentatore_v4.ino` in Arduino IDE (for the active `fermentatore_v6`,
   open `fermentatore_v6/fermentatore_v6/fermentatore_v6.ino` instead)
2. Select your ESP32 board: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
3. Select the correct COM port
4. Click **Upload**

### Flash via OTA (Over-The-Air)

1. Enable a personal hotspot on your phone:
   - SSID: `HotIoT` (or whatever you set in `config.h`)
   - Password: value in `HOTSPOT_PASS` (your `secrets.h`)
2. Power on the ESP32 — it will scan for the hotspot at boot
3. Connect your PC to the same hotspot
4. In Arduino IDE: **Tools → Port** → select `fermentatore-v4 at <IP>`
5. Click **Upload**
6. Disable the hotspot; the ESP32 will reconnect to the main WiFi automatically

> PID control (v5) continues running independently during OTA upload.

---

## Firmware Architecture

### Finite State Machine

```
 Power on
     │
     ▼
WIFI_CONNESSIONE ──[timeout]──► restart
     │ connected
     ▼
MQTT_CONNESSIONE ──[timeout]──► restart
     │ connected
     ▼
RICHIESTA_PROFILO ──[timeout/no profile]──► STANDBY
     │ profile received
     ▼
   RUNNING ◄──────────────────────────────────────────┐
     │                                                 │
     ├─[profile complete]──► HOLDING                  │
     ├─[cmd/stop]──────────► STANDBY ─[cmd/profilo]───┘
     └─[sensor offline]────► ALLARME_SENSORE
```

### Module Map

| Module | File | Responsibility |
|--------|------|----------------|
| Main FSM | `fermentatore_vN.ino` | State machine, boot sequence, loop |
| Thermostat | `termostato.cpp/h` | Relay ON/OFF control (v4: bang-bang; v5: PID PWM) |
| Profile | `profilo.cpp/h` | JSON parse, ramp interpolation, NVS persistence |
| Connectivity | `connettivita.cpp/h` | WiFi + MQTT lifecycle, `EventiMQTT` mailbox |
| Display | `display.cpp/h` | U8g2 OLED rendering, boot splash, status screen |
| Sensor watchdog | `sensor_watchdog.cpp/h` | DS18B20 timeout → `ALLARME_SENSORE` |
| WiFi connection | `wifi_connection.ino` | MAC-based password lookup, event-driven disconnect |
| Log | `log.cpp/h` | Dual-channel logging (Serial + MQTT) |
| Duty cycle | `duty_cycle.cpp/h` | v4 only: PWM fallback when sensor invalid |
| Autotuner | `autotuner.cpp/h` | v5_autotuning only: Ziegler–Nichols relay-feedback |

### Naming Conventions

The codebase uses **Italian names** throughout — this is intentional.

| Pattern | Example |
|---------|---------|
| `module_verb()` functions | `termostato_aggiorna()`, `mqtt_pubblica_stato()` |
| `g_` prefix for globals | `g_stato`, `g_setpoint`, `g_temp_controllo` |
| PascalCase structs | `StatoTermostato`, `ProfiloFermentazione`, `EventiMQTT` |
| Italian variable names | `riscaldatore`, `refrigeratore`, `isteresi`, `setpoint` |

---

## Fermentation Profile (JSON)

Profiles are sent via MQTT topic `fermentazione/f1/cmd/profilo` and persisted in NVS (survives reboot):

```json
{
  "profilo": "Stout_US04",
  "avvio_epoch": 1745000000,
  "isteresi": 0.5,
  "min_cycle_sec": 5,
  "allarme_timeout_minuti": 5,
  "steps": [
    {
      "step": "Fermentazione Primaria",
      "temp": 19.0,
      "duration_hours": 120.0
    },
    {
      "step": "Rampa Diacetile",
      "temp": 21.0,
      "ramp_hours": 24.0
    },
    {
      "step": "Diacetile Rest",
      "temp": 21.0,
      "duration_hours": 48.0
    }
  ]
}
```

For v5 PID control, add gain overrides to the profile:

```json
"kp_risc": 5.0, "ki_risc": 0.005, "kd_risc": 0.0,
"kp_frigo": 1.0, "ki_frigo": 0.001, "kd_frigo": 0.0,
"pid_period_s": 60, "min_pompa_on_s": 5
```

| Field | Unit | Description |
|-------|------|-------------|
| `avvio_epoch` | Unix timestamp | Profile start time (0 = start immediately) |
| `isteresi` | °C | Hysteresis band (v4 only) |
| `min_cycle_sec` | seconds | Minimum relay on/off time (compressor protection) |
| `allarme_timeout_minuti` | minutes | Sensor offline timeout before alarm |
| `duration_hours` | hours | Step duration (flat temperature) |
| `ramp_hours` | hours | Linear ramp to next step's temperature |

---

## MQTT Reference

**Broker:** HiveMQ Cloud (TLS, port 8883)  
**Topic prefix:** `fermentazione/f1/`

### ESP32 → Home Assistant

| Topic | Retain | Description |
|-------|--------|-------------|
| `stato` | ✓ | Full state JSON, published every 60 s |
| `ack` | — | Command acknowledgement |
| `allarme` | — | Sensor/offline alarm |
| `pong` | — | Response to `cmd/ping` |
| `richiedi_profilo` | — | Boot broadcast requesting active profile |
| `log` | — | Diagnostic log messages |

**`stato` payload example:**
```json
{
  "temp_reale": 19.2,
  "setpoint": 19.0,
  "step_nome": "Fermentazione Primaria",
  "step_index": 0,
  "ore_step": 12.5,
  "ore_totali": 12.5,
  "riscaldatore": false,
  "refrigeratore": false,
  "allarme": false,
  "profilo": "Stout_US04",
  "stato_fsm": "RUNNING",
  "firmware": "4.3.6"
}
```

### Home Assistant → ESP32

| Topic | QoS | Payload |
|-------|-----|---------|
| `cmd/profilo` | 1 | Full profile JSON (see above) |
| `cmd/stop` | 1 | *(empty)* — stop profile, go to STANDBY |
| `cmd/temp_standby` | 0 | `{"temp": 18.0}` |
| `cmd/ping` | 0 | *(empty)* |
| `cmd/reboot` | 1 | *(empty)* |
| `cmd/test_no_wifi` | 1 | *(empty)* — test offline fallback mode |

---

## Home Assistant Integration

This section describes the legacy v2-v5 integration. For `fermentatore_v6` (F1/F2, on `HA Lab`), see
[`fermentatore_v6/docs/04_CONTRATTO_HASS.md`](../fermentatore_v6/docs/04_CONTRATTO_HASS.md) for the full
MQTT/entity contract and [`fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md`](../fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md)
for modes, packages, alarms and dashboards. The `HASS_SPEC_v5_v5A.txt` file referenced by older docs
no longer exists in this repository; the working v5 configuration lived alongside `obsolete/fermentatore_v5`.

The legacy integration uses:
- **Numeric sensors:** temperature, setpoint, PID output, duty cycles
- **Text sensors:** step name, profile name, FSM state
- **Binary sensors:** heater on, cooler on, alarm active, device online (via LWT)
- **Buttons:** ping, stop, reboot
- **Number input:** standby temperature adjustment

The Last Will Testament (`allarme` topic with `esp32_offline`) is used to track device availability.

---

## PID Autotuning (v5_autotuning)

The autotuner runs a two-phase relay-feedback test to measure the process dynamics and compute Ziegler–Nichols PID gains automatically.

**Sequence:**
1. **Heat test** (SP = 25 °C, max 24 h) — measures heater time constant
2. **5-minute stabilisation pause**
3. **Cool test** (SP = 14 °C, max 2 h) — measures cooler time constant
4. Publishes results (with `retain=true`) to:
   - `autotuning/f1/risultati/riscaldamento`
   - `autotuning/f1/risultati/raffreddamento`

**Result payload:**
```json
{
  "fase": "riscaldamento",
  "kp": 4.8,
  "ki": 0.0048,
  "kd": 0.0,
  "Tu_s": 1200,
  "Au_C": 0.8
}
```

Transfer the computed gains to your fermentation profile JSON (`kp_risc`, `ki_risc`, etc.) to use them in v5.

**Safety limits:** test aborts if temperature exceeds 32 °C (heat) or drops below 8 °C (cool).

---

## Security

Credentials are **never** stored in version-controlled files:

- `secrets.h` is listed in `.gitignore` — it exists only on your local machine
- Use `secrets.h.template` in each version folder as a starting point
- After publishing this project publicly, **change your MQTT credentials** on your broker

---

## Contributing

Contributions are welcome. Please:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-improvement`
3. Commit your changes
4. Open a Pull Request

For bug reports or feature requests, open a GitHub Issue on the repository.

---

## License

MIT License — see [LICENSE](../../LICENSE) for details.

Copyright © 2026 Simone Giuri
