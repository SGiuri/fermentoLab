# Test host del firmware v6: compila i sorgenti reali dello sketch con mock Arduino/ESP32
# (MSVC, Visual Studio Build Tools) ed esegue gli scenari di guasto e le simulazioni 48 h.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File fermentatore_v6/tests/host/run_tests.ps1
#   ... -Scenario sensore_guasto -Dettagli
param(
  [string]$Sketch = (Join-Path $PSScriptRoot "..\..\fermentatore_v6"),
  [string]$Scenario = "",
  [switch]$Dettagli
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scenari = @(
  "uscite",
  "sensore_guasto",
  "sensore_recupero",
  "glitch_singolo",
  "comandi",
  "profilo_avvio_zero",
  "profilo_validazione",
  "stato_json",
  "controllo_didattico",
  "isteresi_regolabile",
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

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere non trovato: installare Visual Studio Build Tools (C++)" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Toolchain MSVC C++ non trovata" }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

$arduinoJson = Join-Path $env:USERPROFILE "Documents\Arduino\libraries\ArduinoJson\src"
if (-not (Test-Path (Join-Path $arduinoJson "ArduinoJson.h"))) { throw "ArduinoJson non trovato in $arduinoJson" }

$sketchPath = (Resolve-Path $Sketch).Path
$build = Join-Path $PSScriptRoot "build"
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
  $nome = if ($_.Extension -eq ".ino") { $_.BaseName + ".cpp" } else { $_.Name }
  Copy-Item $_.FullName (Join-Path $src $nome)
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

$mock = Join-Path $PSScriptRoot "mock"
$sorgenti = @(Get-ChildItem $src -Filter *.cpp | ForEach-Object { "`"$($_.FullName)`"" })
$sorgenti += "`"$(Join-Path $mock 'mock_arduino.cpp')`""
$sorgenti += "`"$(Join-Path $PSScriptRoot 'test_firmware.cpp')`""
$rsp = @(
  "/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/W3", "/O2",
  "/D_CRT_SECURE_NO_WARNINGS", "/wd4244", "/wd4267", "/wd4305",
  "/I`"$mock`"", "/I`"$src`"", "/I`"$arduinoJson`"",
  "/Fo`"$obj\\`"", "/Fe`"$(Join-Path $build 'test_firmware.exe')`""
) + $sorgenti
$rsp -join "`r`n" | Set-Content -Encoding ASCII (Join-Path $build "cl.rsp")
@"
@echo off
call "$vcvars" >nul
cl @"$(Join-Path $build 'cl.rsp')"
"@ | Set-Content -Encoding ASCII (Join-Path $build "build.bat")

Write-Host "Compilazione test host..."
& cmd /c (Join-Path $build "build.bat")
if ($LASTEXITCODE -ne 0) { throw "Compilazione fallita" }

$daEseguire = if ($Scenario) { @($Scenario) } else { $scenari }
$falliti = @()
Push-Location $build
try {
  foreach ($s in $daEseguire) {
    $argomenti = @($s)
    if ($Dettagli) { $argomenti += "-v" }
    & (Join-Path $build "test_firmware.exe") @argomenti
    if ($LASTEXITCODE -ne 0) { $falliti += $s }
  }
} finally {
  Pop-Location
}

Write-Host ""
if ($falliti.Count -gt 0) {
  Write-Host "Scenari falliti: $($falliti -join ', ')" -ForegroundColor Red
  exit 1
}
Write-Host "Tutti gli scenari superati ($($daEseguire.Count))" -ForegroundColor Green
