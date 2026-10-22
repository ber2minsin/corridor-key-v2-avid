# One-click installer (elevated). Called by Install-CorridorKey.bat at repo root
# or by a release zip. Puts CorridorKey.avx into Avid's AVX2 plug-ins folder and
# wires a self-contained runtime under Program Files\CorridorKeyAVX.

#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$PackagingDir = $PSScriptRoot
$RepoRoot = (Resolve-Path (Join-Path $PackagingDir "..")).Path
$Payload = Join-Path $PackagingDir "payload"
$AppDir = Join-Path $env:ProgramFiles "CorridorKeyAVX"
$AvxDir = Join-Path $env:ProgramFiles "Avid\AVX2_Plug-ins"
$AvxDirMc = Join-Path $env:ProgramFiles "Avid\Avid Media Composer\AVX2_Plug-Ins"

Write-Host ""
Write-Host "========================================"
Write-Host "  Corridor Key 2.0 - Installer"
Write-Host "========================================"
Write-Host ""

# Prefer built plugin, then packaging payload
$avxSrc = $null
foreach ($c in @(
    (Join-Path $Payload "CorridorKey.avx"),
    (Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"),
    (Join-Path $RepoRoot "plugin\build\bin\Debug\CorridorKey.avx")
)) {
    if (Test-Path $c) { $avxSrc = $c; break }
}
if (-not $avxSrc) {
    throw "CorridorKey.avx not found. Build the plugin first (scripts\build_plugin.ps1)."
}

# Runtime sources
$runtimeSrc = Join-Path $RepoRoot "runtime"
if (-not (Test-Path (Join-Path $runtimeSrc "server.py"))) {
    $runtimeSrc = Join-Path $Payload "runtime"
}
if (-not (Test-Path (Join-Path $runtimeSrc "server.py"))) {
    throw "runtime\server.py missing."
}

# CorridorKey2.0 engine (dev clone or payload copy)
$ckSrc = Join-Path $RepoRoot "CorridorKey2.0"
if (-not (Test-Path (Join-Path $ckSrc "corridorkey"))) {
    $ckSrc = Join-Path $Payload "CorridorKey2.0"
}
if (-not (Test-Path (Join-Path $ckSrc "corridorkey"))) {
    throw "CorridorKey2.0 package not found next to the installer. Clone/download it first, or run packaging\bundle_runtime.ps1."
}
if (-not (Test-Path (Join-Path $ckSrc "weights\corridorkey_v2.safetensors"))) {
    throw "CorridorKey 2.0 model weights are missing under CorridorKey2.0\weights. Run CorridorKey2.0\scripts\download_weights.py once on the build machine before packaging."
}

# Python: prefer bundled embed, else CK2.0 venv, else create venv under AppDir
$bundledPy = Join-Path $Payload "python\python.exe"
$ckVenvPy = Join-Path $ckSrc ".venv\Scripts\python.exe"

Write-Host "[1/5] Creating folders..."
New-Item -ItemType Directory -Force -Path $AppDir | Out-Null
New-Item -ItemType Directory -Force -Path $AvxDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $AppDir "runtime") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $AppDir "logs") | Out-Null

Write-Host "[2/5] Installing plugin into Avid AVX2_Plug-ins..."
Copy-Item $avxSrc (Join-Path $AvxDir "CorridorKey.avx") -Force
Copy-Item $avxSrc (Join-Path $AppDir "CorridorKey.avx") -Force
Write-Host "      -> $AvxDir\CorridorKey.avx"
if (Test-Path (Split-Path $AvxDirMc -Parent)) {
    New-Item -ItemType Directory -Force -Path $AvxDirMc | Out-Null
    Copy-Item $avxSrc (Join-Path $AvxDirMc "CorridorKey.avx") -Force
    Write-Host "      -> $AvxDirMc\CorridorKey.avx"
}

Write-Host "[3/5] Installing AI runtime files..."
Copy-Item (Join-Path $runtimeSrc "*") (Join-Path $AppDir "runtime") -Force -Recurse

# Link or copy CorridorKey2.0 (junction is fast for local installs; copy for portable payload)
$ckDst = Join-Path $AppDir "CorridorKey2.0"
if (Test-Path $ckDst) {
    cmd /c rmdir "$ckDst" 2>$null
    if (Test-Path $ckDst) {
        Remove-Item $ckDst -Recurse -Force -ErrorAction SilentlyContinue
    }
}
# Prefer directory junction to avoid multi-GB copy on developer machines
cmd /c mklink /J "$ckDst" "$ckSrc" | Out-Null
if (-not (Test-Path $ckDst)) {
    Write-Host "      Junction failed; copying CorridorKey2.0 (this can take several minutes)..."
    robocopy $ckSrc $ckDst /E /XD .git __pycache__ /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Failed to copy CorridorKey2.0" }
}

Write-Host "[4/5] Setting up Python for the AI worker..."
$pythonDir = Join-Path $AppDir "python"
$workerPy = Join-Path $pythonDir "python.exe"

function Remove-PythonDir {
    if (Test-Path $pythonDir) {
        cmd /c rmdir "$pythonDir" 2>$null | Out-Null
        if (Test-Path $pythonDir) {
            Remove-Item $pythonDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

if (Test-Path $bundledPy) {
    Write-Host "      Using bundled embedded Python from payload..."
    Remove-PythonDir
    cmd /c mklink /J "$pythonDir" "$(Join-Path $Payload 'python')" | Out-Null
    if (-not (Test-Path $workerPy)) {
        robocopy (Join-Path $Payload "python") $pythonDir /E /NFL /NDL /NJH /NJS /nc /ns /np | Out-Null
    }
} elseif (Test-Path $ckVenvPy) {
    Write-Host "      Using CorridorKey2.0 virtual environment..."
    Remove-PythonDir
    # Junction Scripts so plugin finds python\python.exe (and DLLs live nearby)
    cmd /c mklink /J "$pythonDir" "$(Join-Path $ckSrc '.venv\Scripts')" | Out-Null
    if (-not (Test-Path $workerPy)) {
        throw "CorridorKey2.0 venv python.exe missing at $ckVenvPy"
    }
} else {
    Write-Host "      Creating a local Python venv (requires network once)..."
    $sysCmd = Get-Command python -ErrorAction SilentlyContinue
    $sysPy = $null
    if ($sysCmd) { $sysPy = $sysCmd.Source }
    if (-not $sysPy) {
        throw "No Python found to bootstrap the runtime. Re-run packaging\bundle_runtime.ps1 on a build PC."
    }
    & $sysPy -m venv (Join-Path $AppDir "venv")
    $venvPy = Join-Path $AppDir "venv\Scripts\python.exe"
    & $venvPy -m pip install --upgrade pip
    & $venvPy -m pip install -r (Join-Path $AppDir "runtime\requirements.txt")
    & $venvPy -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cu128
    & $venvPy -m pip install "imageio[ffmpeg]" pywin32
    Remove-PythonDir
    cmd /c mklink /J "$pythonDir" "$(Join-Path $AppDir 'venv\Scripts')" | Out-Null
}

if (-not (Test-Path (Join-Path $AppDir "python\python.exe"))) {
    throw "Failed to place python.exe for the worker under $AppDir\python"
}

Write-Host "[5/5] Writing config + registry..."
$config = @{
    install_root = $AppDir
    ck_root      = $ckDst
    python_exe   = (Join-Path $AppDir "python\python.exe")
    server_py    = (Join-Path $AppDir "runtime\server.py")
    avx_path     = (Join-Path $AvxDir "CorridorKey.avx")
} | ConvertTo-Json
Set-Content -Path (Join-Path $AppDir "config.json") -Value $config -Encoding UTF8

New-Item -Path "HKLM:\Software\CorridorKeyAVX" -Force | Out-Null
Set-ItemProperty -Path "HKLM:\Software\CorridorKeyAVX" -Name "InstallRoot" -Value $AppDir
[Environment]::SetEnvironmentVariable("CORRIDORKEY_AVX_ROOT", $AppDir, "Machine")

# Quick worker ping (optional -- proves Python can start)
Write-Host ""
Write-Host "Verifying worker can start..."
$pyExe = Join-Path $AppDir "python\python.exe"
$ok = $false
try {
    $probeOut = Join-Path $AppDir "logs\probe.txt"
    $probeErr = Join-Path $AppDir "logs\probe.err.txt"
    $probePy = Join-Path $AppDir "logs\probe_import.py"
    $runtimeEsc = ($AppDir + "\runtime") -replace "\\", "\\"
    $probeCode = "import sys`r`nsys.path.insert(0, r'$AppDir\runtime')`r`nimport ipc_protocol`r`nprint('OK')"
    Set-Content -Path $probePy -Value $probeCode -Encoding ASCII
    # Quote paths that contain spaces (e.g. C:\Program Files\...)
    $probe = Start-Process -FilePath $pyExe -ArgumentList @("`"$probePy`"") -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $probeOut -RedirectStandardError $probeErr
    if ($probe.ExitCode -eq 0) { $ok = $true }
} catch {
    $ok = $false
}
if ($ok) {
    Write-Host "Worker import check: OK"
} else {
    Write-Host "Worker import check: skipped/warning (plugin will still try to launch on first use)."
    $probeErr = Join-Path $AppDir "logs\probe.err.txt"
    if (Test-Path $probeErr) {
        Get-Content $probeErr -ErrorAction SilentlyContinue | Select-Object -First 5
    }
}

Write-Host ""
Write-Host "========================================"
Write-Host "  Install complete"
Write-Host "========================================"
Write-Host ""
Write-Host "Plugin:  $AvxDir\CorridorKey.avx"
Write-Host "Runtime: $AppDir"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Restart Avid Media Composer."
Write-Host "  2. Open the Effect Palette -> Key -> CorridorKey."
Write-Host "  3. Drag it onto a green/blue screen clip."
Write-Host "  4. First use may take up to a minute while the AI loads -- that is normal."
Write-Host ""
