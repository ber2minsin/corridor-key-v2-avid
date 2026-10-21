# Build-time only: assemble a self-contained payload for Setup-CorridorKeyAVX.exe.
# End users never run this. Prefer: scripts\build_installer.ps1

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutDir = (Join-Path $PSScriptRoot "payload"),
    [string]$PythonEmbedUrl = "https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip",
    [switch]$FromScratch,
    [switch]$SkipTorch
)

$ErrorActionPreference = "Stop"
Write-Host "CorridorKey AVX - bundling runtime for installer"
Write-Host "Repo: $RepoRoot"
Write-Host "Out:  $OutDir"

function Robo-Mirror([string]$Src, [string]$Dst, [string[]]$ExtraXd = @()) {
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
    $xd = @(".git", "__pycache__", ".cache") + $ExtraXd
    $args = @($Src, $Dst, "/MIR", "/NFL", "/NDL", "/NJH", "/NJS", "/nc", "/ns", "/np")
    foreach ($d in $xd) { $args += "/XD"; $args += $d }
    & robocopy @args | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $Src -> $Dst" }
}

if (Test-Path $OutDir) {
    Write-Host "Cleaning previous payload..."
    Remove-Item $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$pythonDir = Join-Path $OutDir "python"
$runtimeDir = Join-Path $OutDir "runtime"
$ckOut = Join-Path $OutDir "CorridorKey2.0"
$ckSrc = Join-Path $RepoRoot "CorridorKey2.0"
$ckVenvPy = Join-Path $ckSrc ".venv\Scripts\python.exe"
$ckSite = Join-Path $ckSrc ".venv\Lib\site-packages"

# 1) Plugin binary
$avx = Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"
if (-not (Test-Path $avx)) {
    throw "CorridorKey.avx not found at $avx. Run scripts\build_plugin.ps1 first."
}
Copy-Item $avx (Join-Path $OutDir "CorridorKey.avx") -Force
Write-Host "[1/5] Copied CorridorKey.avx"

# 2) Python runtime (prefer cloning the working CK2.0 venv base + site-packages)
Write-Host "[2/5] Assembling Python..."
$uvHome = $null
if ((-not $FromScratch) -and (Test-Path $ckVenvPy)) {
    $cfg = Get-Content (Join-Path $ckSrc ".venv\pyvenv.cfg") -ErrorAction SilentlyContinue
    foreach ($line in $cfg) {
        if ($line -match '^\s*home\s*=\s*(.+)$') {
            $cand = $Matches[1].Trim()
            if (Test-Path (Join-Path $cand "python.exe")) { $uvHome = $cand; break }
        }
    }
}

if ($uvHome -and (Test-Path $ckSite)) {
    Write-Host "      Using CK2.0 interpreter home + venv site-packages"
    Write-Host "      home: $uvHome"
    Robo-Mirror $uvHome $pythonDir
    $dstSite = Join-Path $pythonDir "Lib\site-packages"
    Write-Host "      Copying site-packages (several GB)..."
    Robo-Mirror $ckSite $dstSite
} else {
    Write-Host "      Falling back to embedded CPython + pip (slow / needs network)..."
    $zip = Join-Path $env:TEMP "python-embed-ckavx.zip"
    if (-not (Test-Path $zip)) {
        Invoke-WebRequest -Uri $PythonEmbedUrl -OutFile $zip
    }
    New-Item -ItemType Directory -Force -Path $pythonDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $pythonDir -Force
    Get-ChildItem $pythonDir -Filter "python*._pth" | ForEach-Object {
        $content = Get-Content $_.FullName
        $content = $content | ForEach-Object {
            if ($_ -match "^#import site") { "import site" } else { $_ }
        }
        if ($content -notcontains "import site") { $content += "import site" }
        Set-Content $_.FullName $content
    }
    $getPip = Join-Path $env:TEMP "get-pip.py"
    if (-not (Test-Path $getPip)) {
        Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $getPip
    }
    $py = Join-Path $pythonDir "python.exe"
    & $py $getPip
    & $py -m pip install --upgrade pip
    & $py -m pip install -r (Join-Path $RepoRoot "runtime\requirements.txt")
    if (-not $SkipTorch) {
        & $py -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128
        & $py -m pip install "imageio[ffmpeg]"
    }
}

$py = Join-Path $pythonDir "python.exe"
if (-not (Test-Path $py)) { throw "payload python.exe missing" }

# 3) Worker sources
Write-Host "[3/5] Copying runtime worker..."
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
Copy-Item (Join-Path $RepoRoot "runtime\*.py") $runtimeDir -Force
Copy-Item (Join-Path $RepoRoot "runtime\requirements.txt") $runtimeDir -Force

# 4) CorridorKey2.0 (code + weights, no venv)
if (-not (Test-Path (Join-Path $ckSrc "corridorkey"))) {
    throw "CorridorKey2.0 not found at $ckSrc"
}
if (-not (Test-Path (Join-Path $ckSrc "weights\corridorkey_v2.safetensors"))) {
    throw "Missing weights: CorridorKey2.0\weights\corridorkey_v2.safetensors"
}
Write-Host "[4/5] Syncing CorridorKey2.0 + weights..."
Robo-Mirror $ckSrc $ckOut @(".venv")

# 5) Placeholder config (installer rewrites {app} paths at install time)
Write-Host "[5/5] Writing payload config.json..."
$config = @{
    install_root = "C:\Program Files\CorridorKeyAVX"
    ck_root      = "C:\Program Files\CorridorKeyAVX\CorridorKey2.0"
    python_exe   = "C:\Program Files\CorridorKeyAVX\python\python.exe"
    server_py    = "C:\Program Files\CorridorKeyAVX\runtime\server.py"
    avx_path     = "C:\Program Files\Avid\AVX2_Plug-ins\CorridorKey.avx"
} | ConvertTo-Json
Set-Content -Path (Join-Path $OutDir "config.json") -Value $config -Encoding UTF8

Write-Host ""
Write-Host "Smoke-testing bundled python..."
& $py -c "import torch; print('torch', torch.__version__, 'cuda', torch.cuda.is_available())"
if ($LASTEXITCODE -ne 0) { throw "Bundled python failed torch import" }

$size = (Get-ChildItem $OutDir -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
Write-Host ("Payload ready: {0:N2} GB under {1}" -f ($size / 1GB), $OutDir)
Write-Host "Next: scripts\build_installer.ps1  (or compile packaging\CorridorKeyAVX.iss)"
