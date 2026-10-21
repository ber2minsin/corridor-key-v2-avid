# Run by Setup-CorridorKeyAVX.exe after files are copied.
# Installs Python deps (CUDA torch) and downloads CorridorKey weights via upstream script.
param(
    [Parameter(Mandatory = $true)][string]$InstallRoot,
    [switch]$SkipTorch,
    [switch]$SkipWeights
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

function Write-Step([string]$Msg) {
    Write-Host ""
    Write-Host "=== $Msg ===" -ForegroundColor Cyan
}

$InstallRoot = $InstallRoot.TrimEnd('\')
$pythonDir = Join-Path $InstallRoot "python"
$py = Join-Path $pythonDir "python.exe"
$ck = Join-Path $InstallRoot "CorridorKey2.0"
$runtimeReq = Join-Path $InstallRoot "runtime\requirements.txt"
$logDir = Join-Path $env:LOCALAPPDATA "CorridorKeyAVX\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir "post_install.log"

function Log([string]$Line) {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Add-Content -Path $log -Value "$ts $Line"
    Write-Host $Line
}

Start-Transcript -Path (Join-Path $logDir "post_install_transcript.txt") -Append | Out-Null
try {
    Log "InstallRoot=$InstallRoot"
    if (-not (Test-Path $py)) { throw "Bundled python missing: $py" }
    if (-not (Test-Path $ck)) { throw "CorridorKey2.0 missing: $ck" }

    # Ensure embeddable Python can use site-packages / pip
    Get-ChildItem $pythonDir -Filter "python*._pth" -ErrorAction SilentlyContinue | ForEach-Object {
        $lines = Get-Content $_.FullName
        $lines = $lines | ForEach-Object {
            if ($_ -match '^#\s*import site') { 'import site' } else { $_ }
        }
        if ($lines -notcontains 'import site') { $lines += 'import site' }
        if ($lines -notcontains 'Lib\site-packages') { $lines += 'Lib\site-packages' }
        Set-Content $_.FullName $lines
    }

    Write-Step "Bootstrapping pip"
    $getPip = Join-Path $env:TEMP "get-pip-ckavx.py"
    if (-not (Test-Path $getPip)) {
        Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $getPip
    }
    & $py $getPip --no-warn-script-location
    & $py -m pip install --upgrade pip

    if (-not $SkipTorch) {
        Write-Step "Installing PyTorch (CUDA 12.8 wheels) - several GB, please wait"
        & $py -m pip install --upgrade torch torchvision --index-url https://download.pytorch.org/whl/cu128
        if ($LASTEXITCODE -ne 0) { throw "pip install torch failed" }
    }

    Write-Step "Installing CorridorKey AVX runtime requirements"
    if (Test-Path $runtimeReq) {
        & $py -m pip install -r $runtimeReq
    }
    $ckReq = Join-Path $ck "requirements.txt"
    if (Test-Path $ckReq) {
        & $py -m pip install -r $ckReq
    }

    if (-not $SkipWeights) {
        Write-Step "Downloading CorridorKey 2.0 weights (~5 GB) via official script"
        & $py -m pip install --upgrade "huggingface_hub>=0.20"
        & $py (Join-Path $ck "scripts\download_weights.py") --package_root $ck
        if ($LASTEXITCODE -ne 0) { throw "download_weights.py failed" }
        $w = Join-Path $ck "weights\corridorkey_v2.safetensors"
        if (-not (Test-Path $w)) { throw "Weights still missing after download: $w" }
        Log "Weights OK: $w"
    } else {
        Log "Skipped weight download; first Media Composer render will fetch them."
    }

    Write-Step "Done"
    Log "post_install completed successfully"
    Write-Host ""
    Write-Host "CorridorKey for Avid is ready." -ForegroundColor Green
    Write-Host "Fully quit and restart Media Composer, then apply CorridorKey from Effect Palette -> Image."
} catch {
    Log "ERROR: $_"
    Write-Host "Setup step failed: $_" -ForegroundColor Red
    Write-Host "Log: $log"
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
