#Requires -Version 5.1
# firmware-version-sync.ps1
#
# PostToolUse hook: quando l'agente scrive su CHANGELOG.md in fermentatore_v*/,
# aggiorna automaticamente #define FIRMWARE_VERSION in config.h alla versione
# trovata nella prima riga ## [vX.Y.Z] del CHANGELOG.

$raw = [Console]::In.ReadToEnd()
if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

try { $data = $raw | ConvertFrom-Json } catch { exit 0 }

# Raccoglie tutti i filePath toccati dal tool call
$toolName = $data.toolName
$paths = [System.Collections.Generic.List[string]]::new()

if ($toolName -in @('replace_string_in_file', 'create_file')) {
    if ($data.toolInput.filePath) { [void]$paths.Add($data.toolInput.filePath) }
} elseif ($toolName -eq 'multi_replace_string_in_file') {
    foreach ($r in $data.toolInput.replacements) {
        if ($r.filePath) { [void]$paths.Add($r.filePath) }
    }
} else { exit 0 }

# Cerca un CHANGELOG.md in una cartella fermentatore_v*
$changelog_path = $paths |
    Where-Object { $_ -match '[/\\]fermentatore_v\d+[/\\]CHANGELOG\.md$' } |
    Select-Object -First 1
if (-not $changelog_path) { exit 0 }

$folder = Split-Path $changelog_path -Parent

# Legge la prima versione ## [vX.Y.Z] dal CHANGELOG
try { $changelog_content = Get-Content $changelog_path -Raw -ErrorAction Stop }
catch { exit 0 }
if ($changelog_content -notmatch '## \[v(\d+\.\d+\.\d+)\]') { exit 0 }
$new_version = $Matches[1]

# Legge e patcha config.h
$config_path = Join-Path $folder 'config.h'
if (-not (Test-Path $config_path)) { exit 0 }
try { $config_content = Get-Content $config_path -Raw -ErrorAction Stop }
catch { exit 0 }

$updated = $config_content -replace '(#define FIRMWARE_VERSION\s+")[^"]+(")', "`${1}${new_version}`${2}"
if ($updated -eq $config_content) { exit 0 }   # Gia' in sync, nulla da fare

# Scrive config.h (UTF-8 senza BOM, senza newline aggiuntivo)
$utf8_no_bom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($config_path, $updated, $utf8_no_bom)

# Informa l'agente nella sessione
@{ systemMessage = "Hook firmware-version-sync: FIRMWARE_VERSION aggiornato a ""$new_version"" in config.h" } |
    ConvertTo-Json -Compress |
    Write-Output

exit 0
