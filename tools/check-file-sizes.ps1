<#
.SYNOPSIS
    Enforces the fo4CS translation-unit size rule.

.DESCRIPTION
    Scans src/**/*.cpp and src/**/*.h. Files above -WarnLines are reported as
    warnings; files above -FailLines fail the run (exit code 1). Third-party
    vendor headers under include/ and extern/ are not checked.

.EXAMPLE
    pwsh -File tools/check-file-sizes.ps1
    pwsh -File tools/check-file-sizes.ps1 -FailLines 1000
#>
[CmdletBinding()]
param(
    [int]$WarnLines = 600,
    [int]$FailLines = 800,
    [string]$Root = (Join-Path $PSScriptRoot '..')
)

$ErrorActionPreference = 'Stop'
$srcRoot = Join-Path $Root 'src'
if (-not (Test-Path $srcRoot)) {
    Write-Error "src/ not found under $Root"
    exit 2
}

$files = Get-ChildItem -Path $srcRoot -Recurse -File -Include *.cpp, *.h, *.hpp
$rows = foreach ($file in $files) {
    # ReadAllLines counts blank lines too (Measure-Object -Line does not), so the
    # result matches `wc -l`.
    $lines = [System.IO.File]::ReadAllLines($file.FullName).Length
    [pscustomobject]@{
        Lines = $lines
        File  = [System.IO.Path]::GetRelativePath($Root, $file.FullName)
    }
}

$rows = $rows | Sort-Object Lines -Descending
$failures = @($rows | Where-Object Lines -gt $FailLines)
$warnings = @($rows | Where-Object { $_.Lines -gt $WarnLines -and $_.Lines -le $FailLines })

Write-Host "fo4CS file-size check: warn > $WarnLines, fail > $FailLines"
Write-Host ("Top 10 by line count:")
$rows | Select-Object -First 10 | Format-Table -AutoSize | Out-String | Write-Host

foreach ($w in $warnings) {
    Write-Warning ("{0} lines  {1}" -f $w.Lines, $w.File)
}
foreach ($f in $failures) {
    Write-Host ("FAIL {0} lines  {1}" -f $f.Lines, $f.File) -ForegroundColor Red
}

if ($failures.Count -gt 0) {
    Write-Host ("{0} file(s) exceed {1} lines." -f $failures.Count, $FailLines) -ForegroundColor Red
    exit 1
}
Write-Host "OK: no file exceeds $FailLines lines." -ForegroundColor Green
exit 0
