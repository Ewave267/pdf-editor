# SPDX-License-Identifier: GPL-3.0-only
# Initialize the installed x64 MSVC SDK and persist its environment for CI steps.
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'MSVC setup requires Windows PowerShell 7.' }
if (!$env:GITHUB_ENV) { throw 'This setup requires a GitHub Actions environment file.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'No installed x64 MSVC toolchain found.' }
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
$shell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
& $shell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC compiler is unavailable after setup.' }
if (!$env:VCToolsRedistDir -or !(Test-Path $env:VCToolsRedistDir)) {
    throw 'App-local MSVC runtime directory is unavailable after setup.'
}
Get-ChildItem Env: | ForEach-Object {
    if (!$before.ContainsKey($_.Name) -or $before[$_.Name] -cne $_.Value) {
        if ($_.Value.Contains("`n") -or $_.Value.Contains("`r")) {
            throw "Unexpected multiline toolchain variable: $($_.Name)"
        }
        "$($_.Name)=$($_.Value)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    }
}
Write-Host 'Configured the installed MSVC x64 compiler and app-local runtime.'
