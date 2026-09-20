param(
  [Parameter(Mandatory = $true)]
  [string]$TargetId,

  [int]$PinDs18b20,
  [int]$PinSensorPwr,
  [int]$PinHeatRelay,
  [int]$PinCoolRelay,
  [int]$PinOledSda,
  [int]$PinOledScl
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $PSBoundParameters.ContainsKey("PinDs18b20")) { $PinDs18b20 = -1 }
if (-not $PSBoundParameters.ContainsKey("PinSensorPwr")) { $PinSensorPwr = -1 }
if (-not $PSBoundParameters.ContainsKey("PinHeatRelay")) { $PinHeatRelay = -1 }
if (-not $PSBoundParameters.ContainsKey("PinCoolRelay")) { $PinCoolRelay = -1 }
if (-not $PSBoundParameters.ContainsKey("PinOledSda")) { $PinOledSda = -1 }
if (-not $PSBoundParameters.ContainsKey("PinOledScl")) { $PinOledScl = -1 }

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path  # cartella "firmware": contiene fermentatore_v6/, indipendente da dove vive .git
if ([string]::IsNullOrWhiteSpace($repoRoot)) {
  throw "Unable to resolve git repository root"
}

$manifestPath = Join-Path $repoRoot "fermentatore_v6/v6_manifest.json"
$configPath = Join-Path $repoRoot "fermentatore_v6/fermentatore_v6/config.h"

if (-not (Test-Path $manifestPath)) {
  throw "Manifest not found: $manifestPath"
}
if (-not (Test-Path $configPath)) {
  throw "Config not found: $configPath"
}

$manifest = Get-Content -Path $manifestPath -Raw | ConvertFrom-Json
$target = $manifest.targets | Where-Object { $_.id -eq $TargetId } | Select-Object -First 1
if ($null -eq $target) {
  $valid = ($manifest.targets | ForEach-Object { $_.id }) -join ", "
  throw "Unknown target id '$TargetId'. Valid values: $valid"
}

# Default profiles can be overridden by command-line pins.
$profilePins = @{}
switch ($target.hardware_target) {
  "esp32-wroom" {
    $profilePins = @{
      ds18 = 33
      pwr  = 25
      heat = 16
      cool = 17
      sda  = 21
      scl  = 22
    }
  }
  "esp32-s3-supermini" {
    # Conservative S3 defaults. Override with -Pin* arguments if your board revision differs.
    $profilePins = @{
      ds18 = 4
      pwr  = 5
      heat = 6
      cool = 7
      sda  = 8
      scl  = 9
    }
  }
  default {
    throw "Unsupported hardware target '$($target.hardware_target)'"
  }
}

if ($PinDs18b20 -ge 0) { $profilePins.ds18 = $PinDs18b20 }
if ($PinSensorPwr -ge 0) { $profilePins.pwr = $PinSensorPwr }
if ($PinHeatRelay -ge 0) { $profilePins.heat = $PinHeatRelay }
if ($PinCoolRelay -ge 0) { $profilePins.cool = $PinCoolRelay }
if ($PinOledSda -ge 0) { $profilePins.sda = $PinOledSda }
if ($PinOledScl -ge 0) { $profilePins.scl = $PinOledScl }

$content = Get-Content -Path $configPath -Raw

$replacements = @{
  '#define DEVICE_FERMENTER_ID\s+"[^"]+".*' = ('#define DEVICE_FERMENTER_ID  "{0}"           // AUTO-CONFIG' -f $target.fermenter_id)
  '#define DEVICE_HARDWARE_TARGET\s+"[^"]+".*' = ('#define DEVICE_HARDWARE_TARGET "{0}" // AUTO-CONFIG' -f $target.hardware_target)
  '#define PIN_DS18B20\s+\d+.*' = ('#define PIN_DS18B20       {0} // AUTO-CONFIG' -f $profilePins.ds18)
  '#define PIN_SENSOR_PWR\s+\d+.*' = ('#define PIN_SENSOR_PWR    {0} // AUTO-CONFIG' -f $profilePins.pwr)
  '#define PIN_RISCALDATORE\s+\d+.*' = ('#define PIN_RISCALDATORE  {0} // AUTO-CONFIG' -f $profilePins.heat)
  '#define PIN_REFRIGERATORE\s+\d+.*' = ('#define PIN_REFRIGERATORE {0} // AUTO-CONFIG' -f $profilePins.cool)
  '#define PIN_OLED_SDA\s+\d+.*' = ('#define PIN_OLED_SDA      {0} // AUTO-CONFIG' -f $profilePins.sda)
  '#define PIN_OLED_SCL\s+\d+.*' = ('#define PIN_OLED_SCL      {0} // AUTO-CONFIG' -f $profilePins.scl)
}

foreach ($pattern in $replacements.Keys) {
  if ($content -notmatch $pattern) {
    throw "Expected pattern not found in config.h: $pattern"
  }
  $content = [regex]::Replace($content, $pattern, $replacements[$pattern], [System.Text.RegularExpressions.RegexOptions]::Multiline)
}

Set-Content -Path $configPath -Value $content -Encoding UTF8

Write-Host "Configured V6 target: $TargetId"
Write-Host "  fermenter_id     = $($target.fermenter_id)"
Write-Host "  hardware_target  = $($target.hardware_target)"
Write-Host "  ota_hostname     = fermentatore-v6-$($target.fermenter_id)"
Write-Host "  PIN_DS18B20      = $($profilePins.ds18)"
Write-Host "  PIN_SENSOR_PWR   = $($profilePins.pwr)"
Write-Host "  PIN_RISCALDATORE = $($profilePins.heat)"
Write-Host "  PIN_REFRIGERATORE= $($profilePins.cool)"
Write-Host "  PIN_OLED_SDA     = $($profilePins.sda)"
Write-Host "  PIN_OLED_SCL     = $($profilePins.scl)"
