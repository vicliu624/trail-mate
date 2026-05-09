param(
    [string]$BuildDir = "build/simulator",
    [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$resolvedBuildDir = Join-Path $root $BuildDir
$generatorArgs = @()

if ($env:OS -eq "Windows_NT") {
    $generatorArgs = @("-G", "Visual Studio 17 2022", "-A", "x64")
} elseif (Get-Command ninja -ErrorAction SilentlyContinue) {
    $generatorArgs = @("-G", "Ninja")
}

cmake -S $root -B $resolvedBuildDir @generatorArgs `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DBUILD_TESTING=ON"

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $resolvedBuildDir --target trailmate_cardputer_zero_simulator --config $BuildType
exit $LASTEXITCODE
