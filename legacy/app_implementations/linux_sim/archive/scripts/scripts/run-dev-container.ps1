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

function Ensure-WslDriveMounted {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WindowsPath
    )

    $resolvedPath = (Resolve-Path -LiteralPath $WindowsPath).Path
    $driveName = $resolvedPath.Substring(0, 1).ToLowerInvariant()
    if ($driveName -eq 'c') {
        return
    }

    $mountPoint = "/mnt/$driveName"
    & wsl.exe -d $Distro --cd /mnt/c bash -lc "test -d '$mountPoint'"
    if ($LASTEXITCODE -eq 0) {
        return
    }

    $driveSpec = "$($driveName.ToUpperInvariant()):"
    & wsl.exe -d $Distro -u root --cd /mnt/c bash -lc "mkdir -p '$mountPoint' && mount -t drvfs '$driveSpec' '$mountPoint'"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to mount Windows drive '$($driveName.ToUpperInvariant()):' into WSL at $mountPoint"
    }
}

function Add-EnvIfPresent {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string]$TargetName = $Name,
        [switch]$ConvertPath
    )

    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        return
    }

    if ($ConvertPath.IsPresent) {
        if (Test-Path -LiteralPath $value) {
            Ensure-WslDriveMounted -WindowsPath $value
            $value = Convert-ToWslPath -WindowsPath $value
        } elseif ($value -match '^[A-Za-z]:[\\/]' ) {
            throw "Environment variable '$Name' points to a Windows path that does not exist: $value"
        }
    }

    $script:wslArguments += "$TargetName=$value"
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

$gpsScalarEnvNames = @(
    "TRAIL_MATE_GPS_VALID",
    "TRAIL_MATE_GPS_ENABLED",
    "TRAIL_MATE_GPS_POWERED",
    "TRAIL_MATE_GPS_LAT",
    "TRAIL_MATE_GPS_LNG",
    "TRAIL_MATE_GPS_ALT_M",
    "TRAIL_MATE_GPS_SPEED_MPS",
    "TRAIL_MATE_GPS_COURSE_DEG",
    "TRAIL_MATE_GPS_SATS",
    "TRAIL_MATE_GPS_HDOP",
    "TRAIL_MATE_GPS_FIX",
    "TRAIL_MATE_GPS_BAUD",
    "TRAIL_MATE_GPS_DEVICE"
)

foreach ($envName in $gpsScalarEnvNames) {
    Add-EnvIfPresent -Name $envName
}

Add-EnvIfPresent -Name "TRAIL_MATE_SD_ROOT" -TargetName "DEV_CONTAINER_TRAIL_MATE_SD_ROOT_HOST" -ConvertPath
Add-EnvIfPresent -Name "TRAIL_MATE_SETTINGS_ROOT" -TargetName "DEV_CONTAINER_TRAIL_MATE_SETTINGS_ROOT_HOST" -ConvertPath
Add-EnvIfPresent -Name "TRAIL_MATE_GPS_NMEA_FILE" -TargetName "DEV_CONTAINER_TRAIL_MATE_GPS_NMEA_FILE_HOST" -ConvertPath

$wslArguments += "bash"
$wslArguments += "scripts/run-dev-container.sh"

if ($SimulatorArgs -and $SimulatorArgs.Count -gt 0) {
    $wslArguments += "--"
    $wslArguments += $SimulatorArgs
}

& wsl.exe @wslArguments
exit $LASTEXITCODE
