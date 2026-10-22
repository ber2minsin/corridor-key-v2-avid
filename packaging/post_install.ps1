# Run by Setup-CorridorKey-2.0.exe after files are copied.
# Installs Python deps (CUDA torch) and downloads Corridor Key 2.0 weights via upstream script.
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
$runtimeDir = Join-Path $InstallRoot "runtime"
$runtimeReq = Join-Path $runtimeDir "requirements.txt"
$serverPy = Join-Path $runtimeDir "server.py"
$logDir = Join-Path $env:LOCALAPPDATA "CorridorKeyAVX\logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir "post_install.log"

function Log([string]$Line) {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Add-Content -Path $log -Value "$ts $Line"
    Write-Host $Line
}

function Assert-NotJunction([string]$Path, [string]$Label) {
    if (-not (Test-Path $Path)) { throw "$Label missing: $Path" }
    $item = Get-Item $Path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw ("$Label is still a junction/symlink to '{0}'. " +
               "Uninstall Corridor Key 2.0, delete '{1}' if it remains, then re-run Setup-CorridorKey-2.0.exe.") -f `
            (($item.Target -join ', ')), $Path
    }
}

function Ensure-HubPin {
    # transformers / C-RADIO need huggingface-hub 0.34–0.x; never float to 2.x
    & $py -m pip install --upgrade "huggingface_hub>=0.34,<1.0"
    if ($LASTEXITCODE -ne 0) { throw "Failed to pin huggingface_hub<1.0" }
}

function Test-WorkerReady {
    Write-Step "Verifying worker can start"
    # Exact spawn shape the AVX plugin uses (absolute server.py path, no PYTHONPATH).
    & $py $serverPy --help
    if ($LASTEXITCODE -ne 0) {
        throw "runtime\server.py failed to start (embeddable Python import path). Reinstall required."
    }
    $code = @"
import sys
from pathlib import Path
runtime = Path(r'''$runtimeDir''')
sys.path.insert(0, str(runtime))
import numpy, torch, huggingface_hub, transformers, ipc_protocol, inference_session
hub = huggingface_hub.__version__
if int(hub.split('.')[0]) >= 1:
    raise SystemExit(f'huggingface_hub {hub} is too new; need <1.0')
if not torch.cuda.is_available():
    raise SystemExit('torch.cuda.is_available() is False; NVIDIA driver / CUDA torch required')
print('VERIFY_OK', 'numpy', numpy.__version__, 'torch', torch.__version__, 'hub', hub)
"@
    $tmp = Join-Path $env:TEMP "ckavx_verify_worker.py"
    Set-Content -Path $tmp -Value $code -Encoding UTF8
    try {
        & $py $tmp
        if ($LASTEXITCODE -ne 0) { throw "Worker verification failed (exit $LASTEXITCODE)" }
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

Start-Transcript -Path (Join-Path $logDir "post_install_transcript.txt") -Append | Out-Null
try {
    Log "InstallRoot=$InstallRoot"
    if (-not (Test-Path $py)) { throw "Bundled python missing: $py" }
    if (-not (Test-Path $ck)) { throw "CorridorKey2.0 missing: $ck" }
    if (-not (Test-Path $serverPy)) { throw "runtime\server.py missing: $serverPy" }

    Assert-NotJunction $pythonDir "python"
    Assert-NotJunction $ck "CorridorKey2.0"

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
        if ($LASTEXITCODE -ne 0) { throw "pip install runtime requirements failed" }
    }
    $ckReq = Join-Path $ck "requirements.txt"
    if (Test-Path $ckReq) {
        & $py -m pip install -r $ckReq
        if ($LASTEXITCODE -ne 0) { throw "pip install CorridorKey2.0 requirements failed" }
    }

    # Re-pin after requirement files (they may float huggingface-hub to 2.x)
    Write-Step "Pinning huggingface_hub for transformers / C-RADIO"
    Ensure-HubPin

    if (-not $SkipWeights) {
        Write-Step "Downloading CorridorKey 2.0 weights (~5 GB) via official script"
        Ensure-HubPin
        & $py (Join-Path $ck "scripts\download_weights.py") --package_root $ck
        if ($LASTEXITCODE -ne 0) { throw "download_weights.py failed" }
        $w = Join-Path $ck "weights\corridorkey_v2.safetensors"
        if (-not (Test-Path $w)) { throw "Weights still missing after download: $w" }
        Log "Weights OK: $w"
    } else {
        Log "Skipped weight download; first Media Composer render will fetch them."
    }

    Test-WorkerReady

    Write-Step "Done"
    Log "post_install completed successfully"
    Write-Host ""
    Write-Host "Corridor Key 2.0 is ready." -ForegroundColor Green
    Write-Host "Fully quit and restart Media Composer, then apply Corridor Key 2.0 from Effect Palette -> Image."
} catch {
    Log "ERROR: $_"
    Write-Host "Setup step failed: $_" -ForegroundColor Red
    Write-Host "Log: $log"
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
