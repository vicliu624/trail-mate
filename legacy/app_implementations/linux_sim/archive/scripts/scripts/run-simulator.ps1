param(
    [string]$BuildDir = "build/simulator",
    [string]$BuildType = "Debug",
    [int]$Scale = 1,
    [int]$AutoExitMs = 0
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$resolvedBuildDir = Join-Path $root $BuildDir

& (Join-Path $PSScriptRoot "build-simulator.ps1") -BuildDir $BuildDir -BuildType $BuildType
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$candidate = Join-Path $resolvedBuildDir "trailmate_cardputer_zero_simulator.exe"
if (-not (Test-Path $candidate)) {
    $candidate = Join-Path $resolvedBuildDir $BuildType
    $candidate = Join-Path $candidate "trailmate_cardputer_zero_simulator.exe"
}

$arguments = @("--scale", $Scale)
if ($AutoExitMs -gt 0) {
    $arguments += @("--auto-exit-ms", $AutoExitMs)
}

& $candidate @arguments
exit $LASTEXITCODE
