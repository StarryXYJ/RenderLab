#Requires -Version 7.0
param(
    [string]$NativeBuildDir = "out/build/windows-release",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$OfflinePackageCache
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$nativeDir = [IO.Path]::GetFullPath($NativeBuildDir, $repoRoot)
$managedDir = Join-Path $repoRoot "src/editor/managed"

if (!(Test-Path (Join-Path $nativeDir "CMakeCache.txt")))
{
    throw "Configure CMake first; see manual/build.md. Native build: $nativeDir"
}

& cmake --build $nativeDir --config $Configuration --target renderlab_editor_bridge
if ($LASTEXITCODE -ne 0) { throw "Native bridge build failed ($LASTEXITCODE)." }

Push-Location $managedDir
try
{
    $restoreArgs = @("restore", "--locked-mode")
    if ($OfflinePackageCache)
    {
        $packageCache = [IO.Path]::GetFullPath($OfflinePackageCache, $repoRoot)
        if (!(Test-Path -LiteralPath $packageCache -PathType Container))
        {
            throw "Offline package cache does not exist: $packageCache"
        }
        $offlineSource = Join-Path $repoRoot "out/nuget-offline-source"
        New-Item -ItemType Directory -Force -Path $offlineSource | Out-Null
        $restoreArgs += @("--source", $offlineSource,
            "-p:RestorePackagesPath=$(Join-Path $repoRoot 'out/nuget-packages')",
            "-p:RestoreFallbackFolders=$packageCache")
    }
    & dotnet @restoreArgs
    if ($LASTEXITCODE -ne 0) { throw "Managed dependency restore failed ($LASTEXITCODE)." }
    # This prototype uses no licensed Avalonia products; skip build-service telemetry.
    & dotnet build --no-restore -c $Configuration "-p:NativeBuildDir=$nativeDir" "-p:UsedAvaloniaProducts="
    if ($LASTEXITCODE -ne 0) { throw "Managed host build failed ($LASTEXITCODE)." }
}
finally
{
    Pop-Location
}
