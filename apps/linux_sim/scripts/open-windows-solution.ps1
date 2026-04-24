param(
    [string]$Preset = "windows-vs2022-debug"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $scriptDir "generate-windows-solution.ps1") -Preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$root = Resolve-Path (Join-Path $scriptDir "..")
$solutionPath = Join-Path $root "build\\simulator\\TrailMateCardputerZero.sln"
if ($Preset -eq "windows-vs2022-test-debug") {
    $solutionPath = Join-Path $root "build\\test\\TrailMateCardputerZero.sln"
}

Start-Process $solutionPath
