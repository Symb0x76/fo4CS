<#
.SYNOPSIS
    Validate fo4CS runtime logs against the diagnostics event contract.

.DESCRIPTION
    Reads every *.log in a folder produced by collect-runtime-log.ps1 (or any
    folder / single log file) and checks the structured event markers written by
    src/Diagnostics/LogEvents.h. Each event line has the shape

        event=<CODE> <free text>

    Checks per log:
      * required events present (default: BOOT, DEVICE_READY, HOOK_INSTALL,
        FEATURE_STATE) unless the plugin is expected to be passive
      * no more than one BOOT (a second BOOT means the DLL initialized twice)
      * ERROR events are listed and fail the run unless -AllowErrors
      * event counts summary

    This script reports log and static status only. It does not prove
    gameplay or visual behaviour; those observations are recorded separately.

.EXAMPLE
    pwsh -File tools/validate-runtime-log.ps1 -Path dist/logs/20260908-101500-PreNG
    pwsh -File tools/validate-runtime-log.ps1 -Path "$env:USERPROFILE\Documents\My Games\Fallout4\F4SE\NuclearGFX.log"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Path,

    [string[]]$RequiredEvents = @('BOOT', 'DEVICE_READY', 'HOOK_INSTALL', 'FEATURE_STATE'),

    # Plugins that legitimately never install hooks or create a device because they
    # ride on another plugin's. On community-shaders everything is in one DLL, so
    # the list is empty; it stays for parity with the main branch.
    [string[]]$PassivePlugins = @(),

    [switch]$AllowErrors
)

$ErrorActionPreference = 'Stop'

$item = Get-Item -LiteralPath $Path
$logFiles = if ($item.PSIsContainer) {
    Get-ChildItem -LiteralPath $item.FullName -File -Filter *.log
} else {
    @($item)
}
if (-not $logFiles) {
    Write-Error "No .log files found at $Path"
    exit 2
}

$eventPattern = [regex]'(?m)^\s*event=(?<code>[A-Z_]+)\b(?<rest>.*)$'
$overallFailed = $false

# Deliberately not the ?? operator: this has to run under Windows PowerShell 5.1,
# which is what bare `powershell` resolves to even on a machine with pwsh 7.
function Get-Count($table, $key) {
    if ($table.ContainsKey($key)) { return [int]$table[$key] }
    return 0
}

foreach ($log in $logFiles) {
    $plugin = [System.IO.Path]::GetFileNameWithoutExtension($log.Name)
    $text = Get-Content -LiteralPath $log.FullName -Raw
    # Not $matches: that is an automatic variable the regex engine also writes.
    $eventMatches = $eventPattern.Matches($text)

    $counts = @{}
    $errors = New-Object System.Collections.Generic.List[string]
    foreach ($m in $eventMatches) {
        $code = $m.Groups['code'].Value
        $counts[$code] = 1 + (Get-Count $counts $code)
        if ($code -eq 'ERROR') { $errors.Add($m.Value.Trim()) }
    }

    $problems = New-Object System.Collections.Generic.List[string]
    $isPassive = $PassivePlugins -contains $plugin
    foreach ($req in $RequiredEvents) {
        if ($isPassive -and $req -ne 'BOOT') { continue }
        if (-not $counts.ContainsKey($req)) { $problems.Add("missing required event $req") }
    }
    $bootCount = Get-Count $counts 'BOOT'
    if ($bootCount -gt 1) {
        $problems.Add("BOOT appears $bootCount times (duplicate initialization)")
    }
    if ($errors.Count -gt 0 -and -not $AllowErrors) {
        $problems.Add("$($errors.Count) ERROR event(s)")
    }

    $status = 'PASS'
    if ($problems.Count -gt 0) {
        $status = 'FAIL'
        $overallFailed = $true
    }
    Write-Host ("[{0}] {1}  ({2} event lines, {3} total lines)" -f $status, $log.Name, $eventMatches.Count, ($text -split "`n").Count)
    foreach ($k in ($counts.Keys | Sort-Object)) {
        Write-Host ("      {0,-16} {1}" -f $k, $counts[$k])
    }
    foreach ($p in $problems) { Write-Host "      ! $p" -ForegroundColor Red }
    foreach ($e in $errors | Select-Object -First 10) { Write-Host "      $e" -ForegroundColor DarkYellow }
    if ($errors.Count -gt 10) { Write-Host "      ... $($errors.Count - 10) more ERROR lines" -ForegroundColor DarkYellow }
}

if ($overallFailed) {
    Write-Host 'Runtime log validation FAILED.' -ForegroundColor Red
    exit 1
}
Write-Host 'Runtime log validation passed (log/static status only; gameplay is validated manually).' -ForegroundColor Green
exit 0
