# Fast sanity check: every key the docs name must be handled somewhere in the
# accessibility code.
#
# Why this exists: the 2026-08-25 release shipped `R` documented as re-reading a
# book paragraph when no such handler existed, so the key was silent. For a blind
# player a documented-but-dead key is worse than an undocumented one -- pressing it
# produces nothing, which is indistinguishable from the screen reader having died.
# A human audit found it in ~50 minutes; this finds the same class in under a
# second, so it can run on every release instead of occasionally.
#
# Scope, honestly stated: this proves a key token appears in a dispatch site
# SOMEWHERE. It does NOT prove the key works in the CONTEXT the doc claims (the
# book-R bug was exactly that -- R is handled, just not in bookwindow). Context
# claims still need eyes. This is a floor, not a ceiling.
#
# Usage: powershell -File scripts\check_doc_keys.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

# Doc spellings that differ from the SDL scancode name.
$alias = @{
    'ENTER'     = @('RETURN', 'KP_ENTER')
    'ESC'       = @('ESCAPE')
    'PAGEDOWN'  = @('PAGEDOWN')
    'PAGEUP'    = @('PAGEUP')
    'DEL'       = @('DELETE')
    'INS'       = @('INSERT')
    'BACKTICK'  = @('GRAVE')
    '`'         = @('GRAVE')
    '~'         = @('GRAVE')
    '/'         = @('SLASH', 'KP_DIVIDE')
    'SPACEBAR'  = @('SPACE')
}

# Category names and similar nouns that legitimately appear in a key cell.
# Anything added here must be justified -- it is how a real miss could be silenced.
$notKeys = @(
    'ALL', 'DETECTED', 'LOCATIONS', 'WAYPOINTS', 'TERRAIN', 'ACTORS', 'DOORS',
    'CONTAINERS', 'ITEMS', 'ACTIVATORS', 'NOT', 'NUMBER', 'RECORDID', 'TARGET',
    'NOTE', 'AND', 'OR', 'YES', 'NO', 'ON', 'OFF'
)

$handled = Get-ChildItem (Join-Path $repo 'apps\openmw') -Recurse -Include *.cpp |
    Select-String -Pattern 'SDL_SCANCODE_([A-Z0-9_]+)' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique

$failures = @()

foreach ($docName in @('ACCESSIBILITY_KEYS.md', 'ACCESSIBILITY_README.md')) {
    $path = Join-Path $repo $docName
    if (-not (Test-Path $path)) { continue }
    $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($path))

    # Only the KEY COLUMN of a table row: "| **Ctrl + K** | ... |". Bold text in
    # running prose is emphasis, not a key claim, and scanning it drowns the real
    # signal in words like **same** and **much safer**.
    $tokens = [regex]::Matches($text, '(?m)^\|\s*\*\*([^*]+)\*\*[^|]*\|') |
        ForEach-Object { $_.Groups[1].Value } |
        ForEach-Object { $_ -split '\s*/\s*' } |
        ForEach-Object { ($_ -replace 'Ctrl \+ |Shift \+ |Alt \+ ', '').Trim() } |
        ForEach-Object { $_.ToUpper() -replace 'PAGE DOWN', 'PAGEDOWN' -replace 'PAGE UP', 'PAGEUP' -replace ' ', '' } |
        Where-Object { $_ -match '^[A-Z0-9`~/]+$' -and $_.Length -le 10 } |
        Sort-Object -Unique

    foreach ($tok in $tokens) {
        if ($notKeys -contains $tok) { continue }
        $candidates = if ($alias.ContainsKey($tok)) { $alias[$tok] } else { @($tok) }
        $found = $false
        foreach ($c in $candidates) { if ($handled -contains $c) { $found = $true; break } }
        if (-not $found) {
            $failures += "${docName}: documents '$tok' but no SDL_SCANCODE_$tok is handled in apps/openmw"
        }
    }
}

Write-Host ''
if ($failures.Count) {
    Write-Host 'DOC KEY CHECK FAILED:' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'A documented key that no handler implements is silent in play.' -ForegroundColor Yellow
    Write-Host 'Either remove the claim, or add the alias/prose word if it is a false positive.' -ForegroundColor Yellow
    exit 1
}
Write-Host ("  doc key check passed ({0} scancodes handled in code)" -f $handled.Count) -ForegroundColor Green
exit 0
