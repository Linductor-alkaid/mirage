# M5-11 (DEC-031): build the Mirage Windows installer with NSIS.
# FAIL CLOSED: makensis must be on PATH (choco install nsis on the release
# machine); a missing toolchain aborts instead of producing a partial
# installer. The product binaries must already sit in the release tree
# (built by the MSVC release configuration; see the M5-11 verification
# record for the payload layout).
param(
    [string]$BinDir = "..\..\build\release\bin",
    [string]$Version = "0.1.0"
)
$ErrorActionPreference = "Stop"

$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if ($null -eq $makensis) {
    Write-Error "makensis not found on PATH (install NSIS: choco install nsis)"
    exit 1
}
foreach ($binary in @("mirage.exe", "mirage-service.exe", "mirage-tray.exe", "mirage-native.exe")) {
    if (-not (Test-Path (Join-Path $BinDir $binary))) {
        Write-Error "release tree is missing $binary (build the MSVC release configuration first)"
        exit 1
    }
}

$defines = @("-DMIRAGE_VERSION=$Version", "-DMIRAGE_BIN_DIR=$BinDir")
if (-not (Test-Path (Join-Path $BinDir "assets"))) { Write-Error "native assets missing"; exit 1 }

& makensis @defines "mirage.nsi"
if ($LASTEXITCODE -ne 0) {
    Write-Error "makensis failed with exit code $LASTEXITCODE"
    exit 1
}
Write-Output "installer: mirage-installer.exe"
exit 0
