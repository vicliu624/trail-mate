[CmdletBinding()]
param(
    [string]$Distro = "Ubuntu-24.04",
    [ValidateSet("simulator", "shell")]
    [string]$Mode = "simulator",
    [string]$BuildType = "Debug",
    [string]$Platform,
    [string]$ImageTag,
    [switch]$SkipImageBuild,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$SimulatorArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-ToWslPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WindowsPath
    )

    $resolvedPath = (Resolve-Path -LiteralPath $WindowsPath).Path
    $driveName = $resolvedPath.Substring(0, 1).ToLowerInvariant()
    $restPath = $resolvedPath.Substring(2).Replace('\', '/')
    return "/mnt/$driveName$restPath"
}

$repositoryRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$repositoryRootWsl = Convert-ToWslPath -WindowsPath $repositoryRoot

$wslArguments = @(
    "-d", $Distro,
    "--cd", $repositoryRootWsl,
    "env",
    "DEV_CONTAINER_MODE=$Mode",
    "DEV_CONTAINER_BUILD_TYPE=$BuildType"
)

if ($Platform) {
    $wslArguments += "DEV_CONTAINER_PLATFORM=$Platform"
}

if ($ImageTag) {
    $wslArguments += "DEV_CONTAINER_IMAGE=$ImageTag"
}

if ($SkipImageBuild.IsPresent) {
    $wslArguments += "DEV_CONTAINER_SKIP_BUILD=1"
}

$wslArguments += "bash"
$wslArguments += "scripts/run-dev-container.sh"

if ($SimulatorArgs -and $SimulatorArgs.Count -gt 0) {
    $wslArguments += "--"
    $wslArguments += $SimulatorArgs
}

& wsl.exe @wslArguments
exit $LASTEXITCODE
