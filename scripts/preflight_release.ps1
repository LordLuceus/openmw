# Release pre-flight: everything checkable WITHOUT building.
#
# Why this exists: the 2026-08-25 release took ~90 minutes because doc problems
# were found AFTER packaging, and each fix cost another full package (~11 min,
# dominated by the relink that re-baking the revision forces). Packaging is only
# cheap if it happens ONCE, which means every doc fault must be found before the
# first build.
#
# Run this until it is green. Only then reconfigure, build, and package.
#
# Usage: powershell -File scripts\preflight_release.ps1

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    $problems = @()
    $sw = [Diagnostics.Stopwatch]::StartNew()

    # --- 1. Tree state -----------------------------------------------------
    $dirty = @(git status --porcelain)
    if ($dirty.Count) { $problems += "working tree is dirty ($($dirty.Count) file(s)) -- commit before releasing" }

    git fetch origin --quiet 2>$null
    $ahead = (git rev-list --count origin/master..HEAD).Trim()
    if ($ahead -ne '0') { $problems += "$ahead commit(s) not pushed -- the release tag must point at a pushed commit" }

    # --- 2. Docs: HTML a11y gates + the dead-key check ---------------------
    # This covers broken tables (a paragraph splitting a Markdown table renders
    # every row below it as prose) and any key documented without a handler.
    $docOut = & powershell -NoProfile -File (Join-Path $PSScriptRoot 'build_docs.ps1') -Check 2>&1
    if ($LASTEXITCODE -ne 0) {
        $problems += @($docOut | Where-Object { $_ -match '^\s*-\s' } | ForEach-Object { "docs: " + $_.ToString().Trim() })
    }

    # --- 3. Encoding: no CRLF, no BOM in any player doc --------------------
    foreach ($f in Get-ChildItem -Filter 'ACCESSIBILITY_*.md') {
        $b = [IO.File]::ReadAllBytes($f.FullName)
        if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB) { $problems += "$($f.Name): has a UTF-8 BOM" }
        $crlf = 0
        for ($i = 1; $i -lt $b.Length; $i++) { if ($b[$i] -eq 10 -and $b[$i - 1] -eq 13) { $crlf++ } }
        if ($crlf) { $problems += "$($f.Name): $crlf CRLF line ending(s)" }
    }

    # --- 4. Bare URLs -----------------------------------------------------
    # A bare autolink's anchor text IS the URL, so a screen reader reads out the
    # whole query string. Every link needs human-readable text.
    foreach ($f in Get-ChildItem -Filter 'ACCESSIBILITY_*.md') {
        $txt = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($f.FullName))
        foreach ($m in [regex]::Matches($txt, '(?<![(\[<`])\bhttps?://[^\s<>)\]]+')) {
            $problems += "$($f.Name): bare URL '$($m.Value)' -- give it link text"
        }
    }

    # --- 5. Changelog has an entry for today ------------------------------
    $clog = Join-Path $repo 'ACCESSIBILITY_CHANGELOG.md'
    if (Test-Path $clog) {
        $txt = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($clog))
        $today = Get-Date -Format 'yyyy-MM-dd'
        if ($txt -notmatch [regex]::Escape($today)) {
            Write-Host "  note: no changelog entry dated $today (fine if nothing player-facing landed today)" -ForegroundColor DarkYellow
        }
    }

    # --- 6. Tag must not already exist ------------------------------------
    $tag = "beta-" + (Get-Date -Format 'yyyy-MM-dd')
    if (@(git tag --list $tag).Count) { $problems += "tag $tag already exists locally -- delete it or pick another date" }
    if (@(git ls-remote --tags origin $tag 2>$null).Count) { $problems += "tag $tag already exists on origin" }

    $sw.Stop()
    Write-Host ''
    if ($problems.Count) {
        Write-Host 'PRE-FLIGHT FAILED:' -ForegroundColor Red
        $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
        Write-Host ''
        Write-Host 'Fix these BEFORE building. Each miss caught here saves a full ~11 min package.' -ForegroundColor Yellow
        exit 1
    }
    Write-Host ("  pre-flight passed in {0:N1}s -- safe to reconfigure, build and package" -f $sw.Elapsed.TotalSeconds) -ForegroundColor Green
    exit 0
}
finally { Pop-Location }
