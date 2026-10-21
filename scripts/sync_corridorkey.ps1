# Update the CorridorKey2.0 submodule and refresh model weights via upstream scripts.
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [switch]$Remote,
    [switch]$SkipWeights
)

$ErrorActionPreference = "Stop"
$ck = Join-Path $RepoRoot "CorridorKey2.0"

if (-not (Test-Path (Join-Path $ck ".git")) -and -not (Test-Path (Join-Path $RepoRoot ".gitmodules"))) {
    throw "CorridorKey2.0 submodule is missing. Run: git submodule update --init --recursive"
}

Push-Location $RepoRoot
try {
    if ($Remote) {
        Write-Host "Updating submodule to latest origin/master..."
        git submodule update --init --remote --merge CorridorKey2.0
    } else {
        Write-Host "Initializing/checking out pinned submodule commit..."
        git submodule update --init --recursive CorridorKey2.0
    }
} finally {
    Pop-Location
}

if ($SkipWeights) {
    Write-Host "Skipped weight download (-SkipWeights)."
    exit 0
}

$pyCandidates = @(
    (Join-Path $ck ".venv\Scripts\python.exe"),
    (Join-Path $RepoRoot "packaging\payload\python\python.exe"),
    "python"
)
$py = $pyCandidates | Where-Object { $_ -eq "python" -or (Test-Path $_) } | Select-Object -First 1
if (-not $py) { throw "No Python interpreter found." }

Write-Host "Using Python: $py"
Write-Host "Downloading / verifying CorridorKey weights (upstream scripts/download_weights.py)..."
& $py -m pip install --upgrade "huggingface_hub>=0.20" | Out-Host
& $py (Join-Path $ck "scripts\download_weights.py") --package_root $ck
if ($LASTEXITCODE -ne 0) { throw "download_weights.py failed ($LASTEXITCODE)" }
Write-Host "CorridorKey2.0 assets are ready under $ck"
