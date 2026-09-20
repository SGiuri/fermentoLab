param(
  [string]$ManifestPath = "v6_manifest.json",
  [string]$OutputDir = "generated"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param([string]$PathFromRepoRoot)
  $repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
  return Join-Path $repoRoot $PathFromRepoRoot
}

function Read-JsonFile {
  param([string]$Path)
  if (-not (Test-Path $Path)) {
    throw "Missing file: $Path"
  }
  return (Get-Content -Path $Path -Raw | ConvertFrom-Json)
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$manifestFullPath = Resolve-RepoPath $ManifestPath
$outputFullDir = Resolve-RepoPath $OutputDir
$generatedJsonPath = Join-Path $outputFullDir "v6_build_info.json"
$generatedHeaderPath = Join-Path $outputFullDir "v6_build_info.h"

if (-not (Test-Path $generatedJsonPath) -or -not (Test-Path $generatedHeaderPath)) {
  throw "Generated files missing. Run fermentatore_v6/tools/v6/generate_build_info.ps1 first."
}

$manifestRaw = Get-Content -Path $manifestFullPath -Raw
$manifest = $manifestRaw | ConvertFrom-Json
$generated = Read-JsonFile $generatedJsonPath

$sha = [System.Security.Cryptography.SHA256]::Create()
try {
  $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes($manifestRaw)
  $hashBytes = $sha.ComputeHash($manifestBytes)
  $expectedHash = ([System.BitConverter]::ToString($hashBytes)).Replace("-", "").ToLowerInvariant()
} finally {
  $sha.Dispose()
}

$errors = New-Object System.Collections.Generic.List[string]

if ($generated.schema_version -ne $manifest.schema_version) {
  $errors.Add("schema_version mismatch")
}
if ($generated.project -ne $manifest.project) {
  $errors.Add("project mismatch")
}
if ($generated.firmware_version -ne $manifest.firmware_version) {
  $errors.Add("firmware_version mismatch")
}
if ($generated.build_channel -ne $manifest.build_channel) {
  $errors.Add("build_channel mismatch")
}
if ($generated.default_target.hardware_target -ne $manifest.default_target.hardware_target) {
  $errors.Add("default_target.hardware_target mismatch")
}
if ($generated.default_target.fermenter_id -ne $manifest.default_target.fermenter_id) {
  $errors.Add("default_target.fermenter_id mismatch")
}
if ([int]$generated.target_count -ne [int]$manifest.targets.Count) {
  $errors.Add("target_count mismatch")
}
if ($generated.manifest_sha256 -ne $expectedHash) {
  $errors.Add("manifest_sha256 mismatch")
}

$headerRaw = Get-Content -Path $generatedHeaderPath -Raw
if ($headerRaw -notmatch [Regex]::Escape("#define V6_FIRMWARE_VERSION `"$($manifest.firmware_version)`"")) {
  $errors.Add("header firmware macro mismatch")
}
if ($headerRaw -notmatch [Regex]::Escape("#define V6_MANIFEST_SHA256 `"$expectedHash`"")) {
  $errors.Add("header hash macro mismatch")
}

if ($errors.Count -gt 0) {
  Write-Error "V6 build info verification failed:"
  foreach ($e in $errors) {
    Write-Error " - $e"
  }
  exit 1
}

Write-Host "V6 build info verification OK"
exit 0
