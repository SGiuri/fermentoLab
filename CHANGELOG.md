# Changelog

## [v0.0.3] - 2026-05-25
### Changed
- Moved all V6 planning/tooling artifacts from repository root into `fermentatore_v6/` to keep V6 self-contained.
- Updated git hook verification path to the new V6 script location under `fermentatore_v6/tools/v6/`.

### Added
- Added V6 test strategy document at `fermentatore_v6/docs/V6_TEST_PLAN.md`.
- Added V6 tooling and automation plan at `fermentatore_v6/docs/V6_TOOLING_PLAN.md`.

## [v0.0.2] - 2026-05-25
### Added
- Extended V6 planning artifacts with integrated autotuning workstream in `V6_MASTER_PLAN.md`.
- Extended executable backlog with Milestone M6 (autotuning) and M7 (HASS docs and validation) in `V6_EXECUTION_BACKLOG.md`.
- Added detailed Home Assistant configuration guide in `HASS_CONFIGURATORE_V6.md` covering control-mode switching and autotuning command workflow.
- Added detailed Home Assistant monitoring guide in `HASS_MONITORAGGIO_V6.md` covering KPIs, dashboards, alerts, and troubleshooting.

## [v0.0.1] - 2026-05-25
### Added
- Added V6 single-source manifest at `v6_manifest.json` for firmware version/build target metadata.
- Added metadata generation script at `tools/v6/generate_build_info.ps1` producing `generated/v6_build_info.h` and `generated/v6_build_info.json`.
- Added consistency verification script at `tools/v6/verify_build_info.ps1` to validate generated artifacts against the manifest.
- Added git hook support with `tools/v6/install_hooks.ps1` and `.githooks/pre-commit` to enforce verification before commits.
