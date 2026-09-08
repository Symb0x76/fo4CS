<#
.SYNOPSIS
    Snapshot and collect fo4CS runtime logs around a manual in-game session.

.DESCRIPTION
    fo4CS on `main` writes one log per plugin DLL to
    %USERPROFILE%\Documents\My Games\Fallout4\F4SE\<Plugin>.log
    (NuclearGFX.log, Overlay.log, FrameGen.log, Upscaler.log, Reflex.log).

    -Phase Before   Records the current size/mtime of each log so a later
                    -Phase After can tell which logs were rewritten by the
                    session. Writes tools/.runtime-log-snapshot.json.
    -Phase After    Copies every plugin log into
                    <OutputRoot>\<yyyyMMdd-HHmmss>-<Variant>\ together with a
                    session.json (variant, timestamps, which logs changed).
                    Prints the folder path so validate-runtime-log.ps1 can
                    consume it.

.EXAMPLE
    pwsh -File tools/collect-runtime-log.ps1 -Phase Before -Variant PreNG
    # ... BOSS plays the scenario, exits the game ...
    pwsh -File tools/collect-runtime-log.ps1 -Phase After -Variant PreNG
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Before', 'After')]
    [string]$Phase,

    [ValidateSet('PreNG', 'PostNG', 'PostAE', 'Unknown')]
    [string]$Variant = 'Unknown',

    [string]$LogDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\Fallout4\F4SE'),

    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\dist\logs'),

    # community-shaders ships one DLL; the other names exist only when the branch
    # is configured with COMMUNITY_SHADERS=OFF.
    [string[]]$Plugins = @('CommunityShaders')
)

$ErrorActionPreference = 'Stop'
$snapshotPath = Join-Path $PSScriptRoot '.runtime-log-snapshot.json'

function Get-LogState {
    param([string]$Plugin)
    $path = Join-Path $LogDirectory "$Plugin.log"
    if (Test-Path -LiteralPath $path) {
        $item = Get-Item -LiteralPath $path
        return [ordered]@{
            plugin  = $Plugin
            path    = $path
            exists  = $true
            length  = $item.Length
            written = $item.LastWriteTimeUtc.ToString('o')
        }
    }
    return [ordered]@{ plugin = $Plugin; path = $path; exists = $false; length = 0; written = $null }
}

$states = foreach ($p in $Plugins) { Get-LogState -Plugin $p }

if ($Phase -eq 'Before') {
    $snapshot = [ordered]@{
        takenUtc = (Get-Date).ToUniversalTime().ToString('o')
        variant  = $Variant
        logs     = $states
    }
    $snapshot | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $snapshotPath -Encoding UTF8
    Write-Host "Snapshot written: $snapshotPath"
    foreach ($s in $states) {
        $status = if ($s.exists) { "{0,8} bytes  {1}" -f $s.length, $s.written } else { 'missing' }
        Write-Host ("  {0,-12} {1}" -f $s.plugin, $status)
    }
    exit 0
}

# Phase After
$before = $null
if (Test-Path -LiteralPath $snapshotPath) {
    $before = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
    if ($Variant -eq 'Unknown' -and $before.variant) { $Variant = $before.variant }
}
else {
    Write-Warning "No Before snapshot found at $snapshotPath; change detection disabled."
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path $OutputRoot "$stamp-$Variant"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$collected = foreach ($s in $states) {
    $changed = $null
    if ($before) {
        $prev = $before.logs | Where-Object plugin -eq $s.plugin
        if ($prev) {
            $changed = -not ($prev.exists -eq $s.exists -and $prev.length -eq $s.length -and $prev.written -eq $s.written)
        }
    }
    if ($s.exists) {
        Copy-Item -LiteralPath $s.path -Destination (Join-Path $outDir "$($s.plugin).log")
    }
    [ordered]@{
        plugin    = $s.plugin
        collected = $s.exists
        changed   = $changed
        length    = $s.length
        written   = $s.written
    }
}

$session = [ordered]@{
    variant      = $Variant
    beforeUtc    = if ($before) { $before.takenUtc } else { $null }
    afterUtc     = (Get-Date).ToUniversalTime().ToString('o')
    logDirectory = $LogDirectory
    logs         = $collected
}
$session | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outDir 'session.json') -Encoding UTF8

Write-Host "Collected to: $outDir"
foreach ($c in $collected) {
    $flag = if ($c.changed -eq $true) { 'changed' } elseif ($c.changed -eq $false) { 'unchanged' } else { '' }
    $state = if ($c.collected) { "{0,8} bytes  {1}" -f $c.length, $flag } else { 'missing' }
    Write-Host ("  {0,-12} {1}" -f $c.plugin, $state)
}
Write-Output $outDir
