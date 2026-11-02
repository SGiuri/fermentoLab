# Test host del nodo attuatori V7: compila i sorgenti reali dello sketch con mock Arduino/ESP32
# (MSVC, Visual Studio Build Tools) ed esegue gli scenari di guasto e le simulazioni 48 h.
# Lo sketch si compila due volte:
#   - sorgente locale (SORGENTE_TEMPERATURA_REMOTA=0): tutti gli scenari della 6.5.1, regressione;
#   - sorgente remota (=1, quella che va sul chip): la temperatura arriva via MQTT come dal nodo sensori.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File firmware/fermentatore_v7/test_attuatori/run_tests.ps1
#   ... -Sorgente remota -Scenario remoto_muto -Dettagli
param(
  [string]$Sketch = (Join-Path $PSScriptRoot "..\nodo_attuatori"),
  [ValidateSet("entrambe", "locale", "remota")]
  [string]$Sorgente = "entrambe",
  [string]$Scenario = "",
  [switch]$Dettagli
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scenari_locale = @(
  "uscite",
  "sensore_guasto",
  "sensore_recupero",
  "glitch_singolo",
  "comandi",
  "profilo_avvio_zero",
  "profilo_validazione",
  "stato_json",
  "sensore_diagnostica",
  "diagnostica_reset",
  "diagnostica_reset_rele",
  "controllo_didattico",
  "isteresi_regolabile",
  "stima_calcolo",
  "stima_weekend",
  "stima_blackout_a",
  "stima_blackout_b",
  "stima_intermittente",
  "sensore_bloccato",
  "power_cycle_continui",
  "wifi_assente",
  "hotspot_ripiego",
  "persistenza_a",
  "persistenza_b",
  "profilo_pausa_blackout_a",
  "profilo_pausa_blackout_b",
  "profilo_pausa_sensore",
  "sim_onoff_48h",
  "sim_pi_48h",
  "sim_pid_48h",
  "sim_pi_lab_48h"
)

# Con la temperatura remota non valgono gli scenari che verificano power-cycle e riavvii della sonda locale
# (sensore_guasto, sensore_bloccato, power_cycle_continui, stima_weekend) né quelli che controllano la sonda
# senza rete (wifi_assente: senza rete la temperatura remota non arriva). Li sostituiscono i remoto_*.
$scenari_remota = @(
  "uscite",
  "sensore_recupero",
  "glitch_singolo",
  "comandi",
  "profilo_avvio_zero",
  "profilo_validazione",
  "stato_json",
  "diagnostica_reset",
  "diagnostica_reset_rele",
  "controllo_didattico",
  "isteresi_regolabile",
  "stima_calcolo",
  "stima_intermittente",
  "hotspot_ripiego",
  "persistenza_a",
  "persistenza_b",
  "profilo_pausa_blackout_a",
  "profilo_pausa_blackout_b",
  "profilo_pausa_sensore",
  "sim_onoff_48h",
  "sim_pi_48h",
  "sim_pid_48h",
  "sim_pi_lab_48h",
  "remoto_normale",
  "remoto_muto",
  "remoto_null",
  "remoto_stantio",
  "remoto_senza_rete",
  "remoto_weekend"
)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere non trovato: installare Visual Studio Build Tools (C++)" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Toolchain MSVC C++ non trovata" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

$arduinoJson = Join-Path $env:USERPROFILE "Documents\Arduino\libraries\ArduinoJson\src"
if (-not (Test-Path (Join-Path $arduinoJson "ArduinoJson.h"))) { throw "ArduinoJson non trovato in $arduinoJson" }

$sketchPath = (Resolve-Path $Sketch).Path
$mock = Join-Path $PSScriptRoot "mock"

function Compila([string]$nome, [int]$remota) {
  $build = Join-Path $PSScriptRoot "build_$nome"
  $src = Join-Path $build "src"
  $obj = Join-Path $build "obj"
  if (Test-Path $build) { Remove-Item -Recurse -Force $build }
  New-Item -ItemType Directory -Force $src, $obj | Out-Null

  # Sorgenti reali dello sketch. Esclusi: credenziali, display (U8g2), pwm (non usato),
  # wifi_connection (sostituito da stub). Il .ino diventa un .cpp.
  $esclusi = @("secrets.h", "display.cpp", "pwm.cpp", "pwm.h", "wifi_connection.ino")
  Get-ChildItem $sketchPath -File | Where-Object {
    ($_.Extension -in ".h", ".cpp", ".ino") -and ($esclusi -notcontains $_.Name)
  } | ForEach-Object {
    $n = if ($_.Extension -eq ".ino") { $_.BaseName + ".cpp" } else { $_.Name }
    Copy-Item $_.FullName (Join-Path $src $n)
  }
  @"
#pragma once
// Credenziali fittizie per i test host
#define HOTSPOT_PASS "test"
#define OTA_PASSWORD "test"
#define MQTT_SERVER  "broker.test"
#define MQTT_USER    "test"
#define MQTT_PASS    "test"
#define WIFI_MAC_PASSWORD_MAP if (macAddress == "AA:BB:CC:DD:EE:FF") { return "test"; }
"@ | Set-Content -Encoding ASCII (Join-Path $src "secrets.h")

  $sorgenti = @(Get-ChildItem $src -Filter *.cpp | ForEach-Object { "`"$($_.FullName)`"" })
  $sorgenti += "`"$(Join-Path $mock 'mock_arduino.cpp')`""
  $sorgenti += "`"$(Join-Path $PSScriptRoot 'test_firmware.cpp')`""
  $rsp = @(
    "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/W3", "/O2",
    "/D_CRT_SECURE_NO_WARNINGS", "/wd4244", "/wd4267", "/wd4305",
    "/DSORGENTE_TEMPERATURA_REMOTA=$remota",
    "/I`"$mock`"", "/I`"$src`"", "/I`"$arduinoJson`"",
    "/Fo`"$obj\\`"", "/Fe`"$(Join-Path $build 'test_firmware.exe')`""
  ) + $sorgenti
  $rsp -join "`r`n" | Set-Content -Encoding ASCII (Join-Path $build "cl.rsp")
  @"
@echo off
call "$vcvars" >nul
cl @"$(Join-Path $build 'cl.rsp')"
"@ | Set-Content -Encoding ASCII (Join-Path $build "build.bat")

  Write-Host "Compilazione test host ($nome)..."
  & cmd /c (Join-Path $build "build.bat") | Where-Object { $_ -match "error|warning C4(?!244|267|305)" } | Out-Host
  if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita ($nome)" }
  return $build
}

function Esegui([string]$nome, [string]$build, [string[]]$lista) {
  $falliti = @()
  Push-Location $build
  try {
    foreach ($s in $lista) {
      $argomenti = @($s)
      if ($Dettagli) { $argomenti += "-v" }
      & (Join-Path $build "test_firmware.exe") @argomenti | Out-Host
      if ($LASTEXITCODE -ne 0) { $falliti += "$nome/$s" }
    }
  } finally {
    Pop-Location
  }
  return ,$falliti
}

$falliti = @()
$totale = 0
if ($Sorgente -in "entrambe", "locale") {
  $b = Compila "locale" 0
  $lista = if ($Scenario) { @($Scenario) } else { $scenari_locale }
  $falliti += Esegui "locale" $b $lista
  $totale += $lista.Count
}
if ($Sorgente -in "entrambe", "remota") {
  $b = Compila "remota" 1
  $lista = if ($Scenario) { @($Scenario) } else { $scenari_remota }
  $falliti += Esegui "remota" $b $lista
  $totale += $lista.Count
}

Write-Host ""
if ($falliti.Count -gt 0) {
  Write-Host "Scenari falliti: $($falliti -join ', ')" -ForegroundColor Red
  exit 1
}
Write-Host "Tutti gli scenari superati ($totale)" -ForegroundColor Green
