param(
    [string]$Preset = "windows-vs2022-debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")

if ($env:OS -ne "Windows_NT") {
    throw "This script is intended for Windows only."
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$solutionPath = Join-Path $root "build\\simulator\\TrailMateCardputerZero.sln"
if ($Preset -eq "windows-vs2022-test-debug") {
    $solutionPath = Join-Path $root "build\\test\\TrailMateCardputerZero.sln"
}

if (-not (Test-Path $solutionPath)) {
    throw "Expected Visual Studio solution was not generated: $solutionPath"
}

Write-Output "Generated Visual Studio solution:"
Write-Output $solutionPath
