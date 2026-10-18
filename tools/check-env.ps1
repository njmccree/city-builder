<#
.SYNOPSIS
  Verifies the Windows toolchain for [PC] Tasks and prints a PASS/FAIL table.
.DESCRIPTION
  Run from a "Developer PowerShell for VS 2022" so cl.exe is on PATH.
  Exit code 1 if any required check fails; optional tools only warn.
  Set GODOT_EXE to the Godot editor path if it is not on PATH.
#>
[CmdletBinding()]
param()

$results = New-Object System.Collections.Generic.List[object]

function Get-Version([string]$text) {
    $m = [regex]::Match($text, '\d+(\.\d+)+')
    if ($m.Success) { return [version]$m.Value }
    return $null
}

function Add-Check {
    param(
        [string]$Name,
        [string]$Exe,
        [string[]]$VersionArgs = @('--version'),
        [version]$Min = $null,
        [bool]$Required = $true,
        [string]$Hint = ''
    )
    $cmd = Get-Command $Exe -ErrorAction SilentlyContinue
    $status = 'FAIL'
    $detail = $Hint
    if ($cmd) {
        # cl.exe prints its banner to stderr and exits non-zero without sources.
        $out = (& $cmd.Source @VersionArgs 2>&1 | Out-String)
        $ver = Get-Version $out
        if ($Min -and (-not $ver -or $ver -lt $Min)) {
            $detail = "found $ver, need >= $Min. $Hint"
        } else {
            $status = 'PASS'
            $detail = if ($ver) { "$ver ($($cmd.Source))" } else { $cmd.Source }
        }
    }
    if ($status -eq 'FAIL' -and -not $Required) { $status = 'WARN' }
    $results.Add([pscustomobject]@{ Tool = $Name; Status = $status; Detail = $detail })
}

Add-Check 'Git' 'git' -Min ([version]'2.30') -Hint 'Install Git for Windows.'
Add-Check 'Git LFS' 'git-lfs' -Min ([version]'3.0') -Hint 'Install Git LFS, then run: git lfs install'
Add-Check 'CMake' 'cmake' -Min ([version]'3.28') -Hint 'Need >= 3.28 (EnTT v4 requires it; the CMake bundled with VS 2022 17.8 is 3.27). Install a newer CMake and put it before VS on PATH.'
Add-Check 'Ninja' 'ninja' -Min ([version]'1.10') -Hint 'Install Ninja or use the one bundled with VS (Developer PowerShell).'
Add-Check 'Python 3' 'python' -Min ([version]'3.8') -Hint 'Install Python 3 and tick "Add to PATH".'

# 19.3x+ is VS 2022; the banner is "Microsoft (R) C/C++ Optimizing Compiler Version 19.xx".
Add-Check 'MSVC (cl.exe)' 'cl' -VersionArgs @() -Min ([version]'19.30') `
    -Hint 'Not on PATH: open "Developer PowerShell for VS 2022" (Start menu) and re-run.'

$godot = if ($env:GODOT_EXE) { $env:GODOT_EXE } else { 'godot' }
Add-Check 'Godot 4 editor' $godot -Min ([version]'4.0') -Required $false `
    -Hint 'Optional until docs/versions.md pins a version. Set GODOT_EXE or add to PATH.'
Add-Check 'clang-format' 'clang-format' -Required $false -Hint 'Optional locally; needed before committing C++ (ships with VS "C++ Clang tools" or LLVM).'
Add-Check 'VS Code' 'code' -Required $false -Hint 'Optional.'

$results | Format-Table -AutoSize -Wrap | Out-String | Write-Host

$failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($failed.Count -gt 0) {
    Write-Host "$($failed.Count) required check(s) FAILED." -ForegroundColor Red
    exit 1
}
Write-Host 'All required checks passed.' -ForegroundColor Green
exit 0
