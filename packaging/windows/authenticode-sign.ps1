# M5-11 (DEC-006 决策 5): Authenticode signing of the shipped binaries and
# the installer — FAIL CLOSED: without an injected certificate
# (MIRAGE_CODESIGN_PFX + MIRAGE_CODESIGN_PFX_PASSWORD, release-machine
# secrets) the script refuses to produce unsigned-but-renamed artifacts.
# The certificate itself lives only in the release machine's secret store
# (shell-binary-locking key isolation); it is never in the repository.
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [string]$PfxPath = $env:MIRAGE_CODESIGN_PFX,
    [string]$PfxPassword = $env:MIRAGE_CODESIGN_PFX_PASSWORD,
    [string]$TimestampServer = "http://timestamp.digicert.com"
)
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($PfxPath) -or [string]::IsNullOrEmpty($PfxPassword)) {
    Write-Error ("Authenticode signing refused: MIRAGE_CODESIGN_PFX / " +
                 "MIRAGE_CODESIGN_PFX_PASSWORD are not set (fail closed; the " +
                 "release certificate never lives in the repository)")
    exit 1
}
$signtool = Get-Command signtool -ErrorAction SilentlyContinue
if ($null -eq $signtool) {
    Write-Error "signtool not found on PATH (install the Windows SDK Signing Tools)"
    exit 1
}

foreach ($file in $Files) {
    if (-not (Test-Path $file)) {
        Write-Error "missing artifact: $file"
        exit 1
    }
    & signtool sign /fd SHA256 /td SHA256 /tr $TimestampServer /f $PfxPath /p $PfxPassword $file
    if ($LASTEXITCODE -ne 0) {
        Write-Error "signing failed for $file (exit $LASTEXITCODE)"
        exit 1
    }
    & signtool verify /pa /all $file
    if ($LASTEXITCODE -ne 0) {
        Write-Error "signature verification failed for $file"
        exit 1
    }
    Write-Output "signed: $file"
}
exit 0
