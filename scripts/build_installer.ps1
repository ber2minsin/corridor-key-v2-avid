# Build Setup-CorridorKeyAVX.exe (Inno Setup). Developer / release machine only.
param(
    [switch]$SkipBundle,
    [switch]$FromScratch
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Packaging = Join-Path $RepoRoot "packaging"
$Iss = Join-Path $Packaging "CorridorKeyAVX.iss"
$Dist = Join-Path $Packaging "dist"

$isccCandidates = @(
    "$env:LocalAppData\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    throw "Inno Setup 6 not found. Install with: winget install JRSoftware.InnoSetup"
}

if (-not $SkipBundle) {
    $bundleArgs = @{ }
    if ($FromScratch) { $bundleArgs.FromScratch = $true }
    & (Join-Path $Packaging "bundle_runtime.ps1") @bundleArgs
}

$payloadAvx = Join-Path $Packaging "payload\CorridorKey.avx"
$payloadPy = Join-Path $Packaging "payload\python\python.exe"
$payloadCk = Join-Path $Packaging "payload\CorridorKey2.0\weights\corridorkey_v2.safetensors"
foreach ($need in @($payloadAvx, $payloadPy, $payloadCk)) {
    if (-not (Test-Path $need)) {
        throw "Payload incomplete (missing $need). Re-run without -SkipBundle."
    }
}

Write-Host "Compiling $Iss with $iscc ..."
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
& $iscc $Iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit $LASTEXITCODE" }

$exe = Join-Path $Dist "Setup-CorridorKeyAVX.exe"
if (-not (Test-Path $exe)) { throw "Expected output missing: $exe" }
$mb = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host ""
Write-Host "Installer ready: $exe ($mb MB)"
Write-Host "Give users that file. They double-click it — no .bat, no Python."
