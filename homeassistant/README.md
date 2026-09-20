# Home Assistant side of FermentaLab

This folder holds the **Home Assistant configuration files that sit next to the firmware**: the parts of
`/config` that are not packages or dashboards. It is a sanitized snapshot of the bench ("Lab") instance
taken on 2026-09-20. Everything that is specific to a fermenter lives elsewhere:

| What | Where |
|---|---|
| MQTT entities, alarms, didactic / test modes (Home Assistant *packages*) | [`../firmware/fermentatore_v6/hass_spec/packages/`](../firmware/fermentatore_v6/hass_spec/packages/) |
| Lovelace dashboards (Laboratorio, Collaudo) | [`../firmware/fermentatore_v6/hass_spec/dashboards/`](../firmware/fermentatore_v6/hass_spec/dashboards/) |
| Automated on-hardware test engine (AppDaemon app) | [`../firmware/fermentatore_v6/hass_spec/appdaemon/collaudo_f1/`](../firmware/fermentatore_v6/hass_spec/appdaemon/collaudo_f1/) |
| Fermentation profile catalog (`*.json`) | [`../firmware/fermentatore_v6/profili/`](../firmware/fermentatore_v6/profili/) |
| Step-by-step Home Assistant guide (Italian) | [`../firmware/fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md`](../firmware/fermentatore_v6/docs/05_HOME_ASSISTANT_F1.md) |

## Files here

| File | Purpose |
|---|---|
| `configuration.yaml` | Enables `packages`, registers the YAML dashboards, defines the `notify.fermentazione_email` SMTP service and the `shell_command` used to start a fermentation. All credentials are `!secret` references. |
| `scripts.yaml` | The Home Assistant scripts the dashboards call (start / stop / pause / resume, send a profile, reboot, safe mode change…). |
| `automations.yaml`, `mqtt.yaml`, `sensors.yaml`, `templates.yaml`, `scenes.yaml` | Included by `configuration.yaml`. Contain some entities left from the older V3 setup; safe to trim to what you need. |
| `influxdb.yaml` | Optional InfluxDB export. Host / user / password come from `secrets.yaml`. |
| `scripts_py/avvia_fermentazione_v6.py` | Reads a profile JSON, converts days to hours and publishes it (plus the control settings) to the firmware through the Home Assistant REST API (`mqtt.publish`). It runs as `shell_command.avvia_fermentazione_v6` inside Home Assistant. |
| `scripts_py/mqtt_broker.json.template` | The script reads `ha_url` and `ha_token` from `/config/scripts_py/mqtt_broker.json` (a long-lived access token). Copy the template and fill it in; the real file is git-ignored. |
| `secrets.yaml.template` | Keys expected in `/config/secrets.yaml`: SMTP for e-mail alerts, optional InfluxDB. |
| `legacy_v3/` | The original V3-era configuration and notes, kept for reference only. Not used by V6. |

## Installing on a Home Assistant instance

1. Back up the instance first (Settings → System → Backups).
2. Enable the **MQTT** integration and point it at the **same broker** the firmware uses (for example a free
   HiveMQ Cloud cluster, TLS on port 8883). Do **not** enable MQTT discovery on a shared broker.
3. Copy the files of this folder into `/config` (merge with what you have; do not overwrite blindly), copy
   `secrets.yaml.template` to `/config/secrets.yaml` and fill it, create `/config/scripts_py/mqtt_broker.json`
   from the template, and copy the profile JSONs to `/config/profili_fermentazione/`.
4. Copy the packages into `/config/packages/` and the dashboards into `/config/`.
5. Check the configuration (Developer tools → Check configuration), reload what you changed, or restart.
6. Optional: install the **AppDaemon** add-on and copy the `collaudo_f1` app to
   `/addon_configs/<appdaemon-slug>/apps/collaudo_f1/` to run the automated test suite
   ([`06_COLLAUDO_F1.md`](../firmware/fermentatore_v6/docs/06_COLLAUDO_F1.md)).

## Secrets

`secrets.yaml` is git-ignored (`**/secrets.yaml`); only the template is versioned. `scripts_py/mqtt_broker.json`
is git-ignored too. Never paste a Home Assistant token or an MQTT password into a file that gets committed. If
you work with an AI assistant, give it access through your own MCP server or SSH key configuration (see
[`../MODALITA_ACCESSO_LAB.md`](../MODALITA_ACCESSO_LAB.md)) and keep addresses and tokens in local, ignored files.

## Notes

- The script entity id `script.f1_v6_avvia_unificato` differs from its service name
  `script.f1_v6_avvia_unified`; the dashboards call the service. Do not "fix" one of them.
- The older `f1_v6_dashboard.yaml` (dashboard "F1 V6", still on the Lab) is not part of this repository, so
  its entry was removed from `configuration.yaml` here. The Laboratorio and Collaudo dashboards are.
