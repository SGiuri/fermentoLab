---
applyTo: "firmware/**/fermentatore_v*/**"
---

## Version Management Policy

Each fermentatore_vN project has a `FIRMWARE_VERSION` macro defined in its `config.h`.
The version must follow the pattern `N.MINOR.PATCH` where `N` matches the folder number
(e.g. `fermentatore_v4` → `4.x.x`, `fermentatore_v3` → `3.x.x`).

### When to update the version

Update `FIRMWARE_VERSION` in `config.h` **every time a new entry is added to `CHANGELOG.md`**
(i.e. after every substantial change). Never update the version without a matching CHANGELOG entry.

> **Nota**: il hook `.github/hooks/firmware-version-sync.ps1` aggiorna automaticamente
> `FIRMWARE_VERSION` in `config.h` ogni volta che l'agente scrive su `CHANGELOG.md`.
> L'agente non deve delegare questo aggiornamento all'utente.

### Increment rules

- PATCH (x.x.**Z**): bugfix, minor tweak — e.g. `4.0.0` → `4.0.1`
- MINOR (x.**Y**.0): new feature or behaviour change — e.g. `4.0.1` → `4.1.0`
- MAJOR (**X**.0.0): breaking change, major restructure — e.g. `4.1.0` → `5.0.0`

### Where the version is used — keep these in sync

1. `config.h` — `#define FIRMWARE_VERSION "N.x.x"`
2. `display.cpp` — shown on the OLED boot screen (splash screen inside `display_init()`)
3. `connettivita.cpp` — added as `"firmware"` field in `mqtt_pubblica_stato()` JSON payload

When `FIRMWARE_VERSION` changes, no other source files need editing beyond those three.
