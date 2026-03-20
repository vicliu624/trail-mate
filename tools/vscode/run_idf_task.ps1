param(
    [ValidateSet('reconfigure', 'build', 'flash', 'monitor', 'flash-monitor')]
    [string]$Action = 'build',
    [string]$Target = 'tab5',
    [string]$BuildDir = '',
    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    return Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

function Get-WorkspaceSettings([string]$RepoRoot) {
    $settingsPath = Join-Path $RepoRoot '.vscode\settings.json'
    if (-not (Test-Path $settingsPath)) {
        return $null
    }

    try {
        return Get-Content $settingsPath -Raw | ConvertFrom-Json
    }
    catch {
        Write-Warning "Failed to parse ${settingsPath}: $_"
        return $null
    }
}

function Resolve-IdfPath([string]$RepoRoot, $Settings) {
    $candidates = @()
    if ($env:IDF_PATH) {
        $candidates += $env:IDF_PATH
    }
    if ($Settings -and $Settings.PSObject.Properties.Name -contains 'idf.currentSetup' -and $Settings.'idf.currentSetup') {
        $candidates += [string]$Settings.'idf.currentSetup'
    }
    $frameworkRoot = 'C:\ProgramData\Espressif\frameworks'
    if (Test-Path $frameworkRoot) {
        $candidates += (Get-ChildItem $frameworkRoot -Directory -Filter 'esp-idf-v*' | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
    }

    foreach ($candidate in $candidates) {
        if (-not $candidate) {
            continue
        }
        $normalized = $candidate.TrimEnd('\', '/')
        if (Test-Path (Join-Path $normalized 'tools\idf.py')) {
            return $normalized
        }
    }

    throw 'Unable to resolve ESP-IDF path. Set IDF_PATH or update .vscode/settings.json.'
}

function Resolve-PythonExe($Settings) {
    $candidates = @()
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidates += (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe')
    }
    if ($Settings -and $Settings.PSObject.Properties.Name -contains 'idf.pythonBinPathWin' -and $Settings.'idf.pythonBinPathWin') {
        $candidates += [string]$Settings.'idf.pythonBinPathWin'
    }
    $pythonEnvRoot = 'C:\ProgramData\Espressif\python_env'
    if (Test-Path $pythonEnvRoot) {
        $candidates += (Get-ChildItem $pythonEnvRoot -Directory -Filter 'idf*_py*_env' | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'Scripts\python.exe' })
    }
    $candidates += 'python.exe'

    foreach ($candidate in $candidates) {
        try {
            $command = Get-Command $candidate -ErrorAction Stop
            return $command.Source
        }
        catch {
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    throw 'Unable to resolve Python executable for ESP-IDF.'
}

function Resolve-NinjaExe() {
    $candidates = @()
    $toolsRoot = 'C:\ProgramData\Espressif\tools\ninja'
    if (Test-Path $toolsRoot) {
        $candidates += (Get-ChildItem $toolsRoot -Directory | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'ninja.exe' })
    }
    $candidates += 'ninja.exe'

    foreach ($candidate in $candidates) {
        try {
            $command = Get-Command $candidate -ErrorAction Stop
            return $command.Source
        }
        catch {
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    return $null
}

function Resolve-Port($Settings, [string]$RequestedPort) {
    if ($RequestedPort) {
        return $RequestedPort
    }
    if ($env:ESPPORT) {
        return $env:ESPPORT
    }
    if ($Settings) {
        if ($Settings.PSObject.Properties.Name -contains 'idf.portWin' -and $Settings.'idf.portWin') {
            return [string]$Settings.'idf.portWin'
        }
        if ($Settings.PSObject.Properties.Name -contains 'idf.port' -and $Settings.'idf.port') {
            return [string]$Settings.'idf.port'
        }
    }
    return 'COM6'
}

$repoRoot = Get-RepoRoot
$settings = Get-WorkspaceSettings $repoRoot
$idfPath = Resolve-IdfPath $repoRoot $settings
$pythonExe = Resolve-PythonExe $settings
$ninjaExe = Resolve-NinjaExe
$portValue = Resolve-Port $settings $Port
if (-not $BuildDir) {
    $BuildDir = "build.$Target"
}

$env:IDF_PATH = $idfPath
$env:IDF_PYTHON_ENV_PATH = Split-Path -Parent (Split-Path -Parent $pythonExe)
if ($ninjaExe) {
    $env:PATH = "$(Split-Path -Parent $ninjaExe);$env:PATH"
}
$espRomElfRoot = 'C:\ProgramData\Espressif\tools\esp-rom-elfs'
if ((-not $env:ESP_ROM_ELF_DIR) -and (Test-Path $espRomElfRoot)) {
    $latestRomDir = Get-ChildItem $espRomElfRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if ($latestRomDir) {
        $env:ESP_ROM_ELF_DIR = $latestRomDir.FullName
    }
}

$idfPy = Join-Path $idfPath 'tools\idf.py'
$baseArgs = @($idfPy, '-B', $BuildDir, "-DTRAIL_MATE_IDF_TARGET=$Target")
if ($Action -ne 'build' -and $portValue) {
    $baseArgs += @('-p', $portValue)
}

$actionArgs = switch ($Action) {
    'reconfigure' { ,@('reconfigure') }
    'build' { ,@('build') }
    'flash' { ,@('flash') }
    'monitor' { ,@('monitor') }
    'flash-monitor' { ,@('flash', 'monitor') }
}

Write-Host "[trail-mate] RepoRoot : $repoRoot"
Write-Host "[trail-mate] ESP-IDF  : $idfPath"
Write-Host "[trail-mate] Python   : $pythonExe"
Write-Host "[trail-mate] Target   : $Target"
Write-Host "[trail-mate] BuildDir : $BuildDir"
if ($portValue) {
    Write-Host "[trail-mate] Port     : $portValue"
}
if ($Action -eq 'flash-monitor') {
    Write-Warning 'Tab5 may re-enter ROM download mode after auto reset. Prefer running flash and monitor as two separate tasks.'
}

Push-Location $repoRoot
try {
    & $pythonExe @baseArgs @actionArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    if ($Action -eq 'flash') {
        Write-Host '[trail-mate] Flash completed. For Tab5, press RESET once manually, then start the monitor task.'
    }
}
finally {
    Pop-Location
}
