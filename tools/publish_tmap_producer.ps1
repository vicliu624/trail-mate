param(
    [Parameter(Mandatory = $true)][string]$CenterRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet('linux-arm', 'linux-arm64', 'linux-x64')][string]$Runtime = 'linux-arm'
)
$ErrorActionPreference = 'Stop'
$center = (Resolve-Path -LiteralPath $CenterRoot).Path
$project = Join-Path $center 'tools/TmapTool/TmapTool.csproj'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) { throw 'Center TmapTool project not found.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
& dotnet publish $project -c Release -r $Runtime --self-contained true -o $output --disable-build-servers
if ($LASTEXITCODE -ne 0) { throw "TMAP producer publish failed: $LASTEXITCODE" }
Copy-Item -LiteralPath (Join-Path $center 'LICENSE') -Destination (Join-Path $output 'Center-LICENSE')
$revision = (& git -C $center rev-parse HEAD).Trim()
$files = Get-ChildItem -LiteralPath $output -File | Sort-Object Name | ForEach-Object {
    [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[ordered]@{ producer = 'TrailMateCenter.TmapTool'; sourceRevision = $revision; runtime = $Runtime; files = @($files) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'producer-manifest.json') -Encoding utf8
