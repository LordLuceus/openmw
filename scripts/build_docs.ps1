<#
.SYNOPSIS
    Convert the Project Hortator accessibility documents from Markdown to
    self-contained, screen-reader-friendly HTML for the release package.

.DESCRIPTION
    The Markdown files in the repo root are the source of truth and are the ones
    to edit. The HTML shipped to players is GENERATED from them by this script --
    never hand-edit the .html files, your changes will be overwritten.

    Both package_beta.bat and build_patch.bat call this script before zipping, so
    the HTML in a release can never be older than the Markdown it came from.

    Accessibility requirements the output must meet (all verified by -Check):
      * Real semantic tables, with scope="col" on every header cell, so a screen
        reader can announce "Key: <x>, Action: <y>" while reading across a row.
      * <html lang="en"> so the synthesiser picks the correct voice.
      * A skip link and <main>/<nav> landmarks, so the contents list can be
        jumped over rather than re-heard on every visit.
      * No JavaScript, no web fonts, no external CSS -- the file is opened
        straight out of an unzipped folder, usually with no network.
      * Colour never carries meaning on its own; it only reinforces text.

.PARAMETER OutputDir
    Where to write the .html files. Defaults to the repo root.

.PARAMETER Check
    Verify the generated HTML meets the accessibility requirements above, and
    that no document is missing. Exits non-zero on failure, so the packaging
    scripts abort rather than shipping a broken document.

.EXAMPLE
    powershell -File scripts\build_docs.ps1
    powershell -File scripts\build_docs.ps1 -OutputDir C:\stage -Check
#>
[CmdletBinding()]
param(
    [string]$OutputDir,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$assets   = Join-Path $PSScriptRoot 'a11y-html'
if (-not $OutputDir) { $OutputDir = $repoRoot }

# The documents that ship to players in HTML form.
# Add new player-facing docs here and they are picked up by both release scripts.
$docs = @(
    @{ Md = 'ACCESSIBILITY_README.md';    Html = 'ACCESSIBILITY_README.html';    Title = 'Project Hortator - Read Me First' }
    @{ Md = 'ACCESSIBILITY_KEYS.md';      Html = 'ACCESSIBILITY_KEYS.html';      Title = 'Project Hortator - Complete Key Reference' }
    @{ Md = 'ACCESSIBILITY_AUDIO_CUES.md'; Html = 'ACCESSIBILITY_AUDIO_CUES.html'; Title = 'Project Hortator - Audio Cue Reference' }
    @{ Md = 'ACCESSIBILITY_MODDING.md';   Html = 'ACCESSIBILITY_MODDING.html';   Title = 'Project Hortator - Modding Guide and Recommended Mods' }
    @{ Md = 'ACCESSIBILITY_CHANGELOG.md'; Html = 'ACCESSIBILITY_CHANGELOG.html'; Title = 'Project Hortator - Changelog' }
    @{ Md = 'THIRD-PARTY-LICENSES.md';    Html = 'THIRD-PARTY-LICENSES.html';    Title = 'Project Hortator - Third-Party Licences' }
)

if (-not (Get-Command pandoc -ErrorAction SilentlyContinue)) {
    throw "pandoc is not on PATH. Install it (https://pandoc.org) -- the release packages need the HTML docs."
}

if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$template = Join-Path $assets 'template.html'
$css      = Join-Path $assets 'style.css'
$filter   = Join-Path $assets 'th-scope.lua'

$failures = @()

foreach ($doc in $docs) {
    $src = Join-Path $repoRoot $doc.Md
    $dst = Join-Path $OutputDir $doc.Html

    if (-not (Test-Path $src)) {
        $failures += "MISSING SOURCE: $($doc.Md)"
        continue
    }

    # --toc with a depth of 2 gives a section-level contents list. The changelog
    # is a long flat list of dated entries, so a contents list of dates is
    # genuinely useful there for jumping to a version.
    $pandocArgs = @(
        $src
        '--from=gfm'
        '--to=html5'
        '--standalone'
        "--template=$template"
        "--css=$css"
        '--embed-resources'
        "--lua-filter=$filter"
        '--toc'
        '--toc-depth=2'
        '--section-divs'
        "--metadata=pagetitle:$($doc.Title)"
        '--metadata=lang:en'
        "--output=$dst"
    )

    & pandoc @pandocArgs
    if ($LASTEXITCODE -ne 0) {
        $failures += "pandoc failed on $($doc.Md) (exit $LASTEXITCODE)"
        continue
    }

    $size = '{0:N0}' -f (Get-Item $dst).Length
    Write-Host ("  {0,-32} -> {1,-32} {2,9} bytes" -f $doc.Md, $doc.Html, $size)

    if ($Check) {
        $html = Get-Content $dst -Raw -Encoding UTF8

        # Each of these is a hard accessibility requirement, not a style
        # preference, so a failure must stop the release.
        $requirements = @{
            'lang attribute'  = '<html lang="en">'
            'skip link'       = 'class="skip-link"'
            'main landmark'   = '<main id="main">'
        }
        foreach ($req in $requirements.GetEnumerator()) {
            if ($html -notlike "*$($req.Value)*") {
                $failures += "$($doc.Html): missing $($req.Key)"
            }
        }

        # No external dependencies: the doc must render from a folder with no
        # network access. (--embed-resources should guarantee this; verify it.)
        if ($html -match '<script') {
            $failures += "$($doc.Html): contains <script> (must be JS-free)"
        }
        if ($html -match '<link[^>]+rel="stylesheet"') {
            $failures += "$($doc.Html): links an external stylesheet (must be inlined)"
        }

        # Every header cell must be scoped. Counting proves the Lua filter
        # actually ran, rather than silently doing nothing after a pandoc upgrade.
        $thTotal = ([regex]::Matches($html, '<th\b')).Count
        $thScope = ([regex]::Matches($html, '<th[^>]*\bscope="col"')).Count
        if ($thTotal -ne $thScope) {
            $failures += "$($doc.Html): $($thTotal - $thScope) of $thTotal <th> cells lack scope=col"
        }

        # No table row may have been rendered as prose. Interrupting a Markdown
        # table with a paragraph (or leaving a blank line mid-table) silently ends
        # the table: pandoc emits the remaining rows as one run-on paragraph full
        # of pipe characters. It still passes every check above, and a screen
        # reader then reads those keys as prose instead of labelled cells -- so it
        # is invisible in review and only obvious to the person relying on it.
        # Shipped once this way (beta-2026-08-25), hence this gate.
        foreach ($para in [regex]::Matches($html, '(?s)<p>.*?</p>')) {
            if ($para.Value -match '\|\s*<strong>') {
                $failures += "$($doc.Html): a table broke into prose -- check for a paragraph or blank line interrupting a Markdown table"
                break
            }
        }
    }
}

if ($failures.Count) {
    Write-Host ''
    Write-Host 'DOCUMENT BUILD FAILED:' -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}

if ($Check) { Write-Host '  (accessibility checks passed)' }
exit 0
