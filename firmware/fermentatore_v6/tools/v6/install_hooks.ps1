Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if ([string]::IsNullOrWhiteSpace($repoRoot)) {
  throw "Unable to resolve git repository root from $PSScriptRoot"
}
$hooksPath = Join-Path $repoRoot ".githooks"

if (-not (Test-Path $hooksPath)) {
  throw "Missing hooks folder: $hooksPath"
}

Push-Location $repoRoot
try {
  git config core.hooksPath .githooks
} finally {
  Pop-Location
}

Write-Host "Configured git hooks path to .githooks"
Write-Host "Pre-commit verification is now enabled."
