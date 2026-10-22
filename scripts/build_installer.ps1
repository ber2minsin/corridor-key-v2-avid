# Build Setup-CorridorKey-2.0.exe (Inno Setup). Developer / release machine only.
param(
    [switch]$SkipBundle,
    [switch]$SkipPluginBuild
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

# Ensure CorridorKey2.0 submodule is checked out (source only; weights via Setup)
if (-not (Test-Path (Join-Path $RepoRoot "CorridorKey2.0\scripts\download_weights.py"))) {
    Write-Host "Initializing CorridorKey2.0 submodule..."
    Push-Location $RepoRoot
    git submodule update --init --recursive CorridorKey2.0
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed" }
    Pop-Location
}

if (-not $SkipPluginBuild) {
    & (Join-Path $PSScriptRoot "build_plugin.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build_plugin.ps1 failed" }
}

if (-not $SkipBundle) {
    & (Join-Path $Packaging "bundle_runtime.ps1")
    if ($LASTEXITCODE -ne 0) { throw "bundle_runtime.ps1 failed" }
}

$payloadAvx = Join-Path $Packaging "payload\CorridorKey.avx"
$payloadPy = Join-Path $Packaging "payload\python\python.exe"
$payloadCk = Join-Path $Packaging "payload\CorridorKey2.0\scripts\download_weights.py"
$payloadPost = Join-Path $Packaging "payload\post_install.ps1"
foreach ($need in @($payloadAvx, $payloadPy, $payloadCk, $payloadPost)) {
    if (-not (Test-Path $need)) {
        throw "Payload incomplete (missing $need). Re-run without -SkipBundle."
    }
}

Write-Host "Compiling $Iss with $iscc ..."
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
& $iscc $Iss
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit $LASTEXITCODE" }

$exe = Join-Path $Dist "Setup-CorridorKey-2.0.exe"
if (-not (Test-Path $exe)) { throw "Expected output missing: $exe" }
$mb = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host ""
Write-Host "Installer ready: $exe ($mb MB)"
Write-Host "Slim package - models download when the user runs Setup (or on first use)."
