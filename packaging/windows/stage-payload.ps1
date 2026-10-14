# M5-11 (DEC-031) / M7-12: assemble the Windows product payload layout that
# mirage.nsi ships and the tray expects at runtime — every product process
# next to each other in one directory, with the asset set the frontend and
# tray load relative to the executable. The build tree scatters the exes
# across per-target directories, so the installer verification and any
# product-process run both go through this staging step first.
# FAIL CLOSED: a missing binary or asset aborts instead of staging a partial
# payload.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [Parameter(Mandatory = $true)][ValidateSet("Debug", "Release")][string]$Config,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = "Stop"

$apps = Join-Path $BuildDir "apps"
$sourceRoot = $PSScriptRoot

# The exes land in different per-target directories of the build tree; try
# each candidate in order.
function Resolve-Binary([string]$name, [string[]]$candidates) {
    foreach ($candidate in $candidates) {
        $path = Join-Path $candidate $name
        if (Test-Path $path) { return $path }
    }
    Write-Error "staging is missing $name (looked in: $($candidates -join '; ')); build the $Config configuration first"
    exit 1
}

$configApps = Join-Path $apps $Config
$binaries = @(
    @{ Name = "mirage.exe"; Candidates = @($configApps) },
    @{ Name = "mirage-launcher.exe"; Candidates = @($configApps) },
    @{ Name = "mirage-service.exe"; Candidates = @($configApps) },
    @{ Name = "mirage-tray.exe"; Candidates = @((Join-Path $apps "tray\$Config"), $configApps) },
    @{ Name = "mirage-native.exe"; Candidates = @((Join-Path $apps "native\$Config"), $configApps) }
)

$nativeAssets = Join-Path $sourceRoot "..\..\apps\native\assets"
$euiAssets = Join-Path $sourceRoot "..\..\third_party\eui-neo\assets"
$generatedFonts = Join-Path $BuildDir "apps\native\font-assets"
$assets = @(
    @{ Src = Join-Path $nativeAssets "mira.png" },
    @{ Src = Join-Path $nativeAssets "mira-ui.png" },
    @{ Src = Join-Path $nativeAssets "mira.ico" },
    @{ Src = Join-Path $nativeAssets "Phosphor-Regular.ttf" },
    @{ Src = Join-Path $nativeAssets "fonts\OFL-NotoSansSC.txt" },
    @{ Src = Join-Path $nativeAssets "fonts\MIT-Phosphor.txt" },
    @{ Src = Join-Path $euiAssets "Font Awesome 7 Free-Solid-900.otf" },
    @{ Src = Join-Path $generatedFonts "NotoSansSC-Regular.otf" }
)

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$assetDir = Join-Path $OutDir "assets"
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null

foreach ($binary in $binaries) {
    $resolved = Resolve-Binary $binary.Name $binary.Candidates
    Copy-Item $resolved (Join-Path $OutDir $binary.Name) -Force
    Write-Output "staged $($binary.Name)"
}
foreach ($asset in $assets) {
    if (-not (Test-Path $asset.Src)) {
        Write-Error "staging is missing asset $($asset.Src)"
        exit 1
    }
    Copy-Item $asset.Src (Join-Path $assetDir (Split-Path $asset.Src -Leaf)) -Force
}
$providers = Join-Path $nativeAssets "providers"
if (-not (Test-Path $providers)) {
    Write-Error "staging is missing the provider presets directory $providers"
    exit 1
}
Copy-Item $providers (Join-Path $assetDir "providers") -Recurse -Force
Write-Output "staged assets -> $assetDir"
Write-Output "payload ready: $OutDir"
exit 0
