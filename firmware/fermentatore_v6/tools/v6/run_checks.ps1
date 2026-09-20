Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path  # cartella "firmware": contiene fermentatore_v6/, indipendente da dove vive .git
if ([string]::IsNullOrWhiteSpace($repoRoot)) {
  throw "Unable to resolve git repository root"
}

Push-Location $repoRoot
try {
  powershell -NoProfile -ExecutionPolicy Bypass -File "fermentatore_v6/tools/v6/generate_build_info.ps1"
  powershell -NoProfile -ExecutionPolicy Bypass -File "fermentatore_v6/tools/v6/verify_build_info.ps1"
  Write-Host "V6 checks completed successfully"
} finally {
  Pop-Location
}
