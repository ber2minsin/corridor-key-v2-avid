# Assemble a slim installer payload (plugin + embeddable Python + CK source, no weights).
# Models and CUDA torch are downloaded by post_install.ps1 during Setup.
# Prefer: scripts\build_installer.ps1

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutDir = (Join-Path $PSScriptRoot "payload"),
    [string]$PythonEmbedUrl = "https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip"
)

$ErrorActionPreference = "Stop"
Write-Host "CorridorKey AVX - bundling slim installer payload"
Write-Host "Repo: $RepoRoot"
Write-Host "Out:  $OutDir"

function Robo-Mirror([string]$Src, [string]$Dst, [string[]]$ExtraXd = @()) {
    New-Item -ItemType Directory -Force -Path $Dst | Out-Null
    $xd = @(".git", "__pycache__", ".cache", ".venv", "venv") + $ExtraXd
    $args = @($Src, $Dst, "/MIR", "/NFL", "/NDL", "/NJH", "/NJS", "/nc", "/ns", "/np")
    foreach ($d in $xd) { $args += "/XD"; $args += $d }
    & robocopy @args | Out-Null
    # robocopy: 0-7 = success with various copy stats; >=8 = failure
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE): $Src -> $Dst" }
    $global:LASTEXITCODE = 0
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

# Ensure submodule is present
if (-not (Test-Path (Join-Path $ckSrc "scripts\download_weights.py"))) {
    Write-Host "Initializing CorridorKey2.0 submodule..."
    Push-Location $RepoRoot
    git submodule update --init --recursive CorridorKey2.0
    Pop-Location
}
if (-not (Test-Path (Join-Path $ckSrc "corridorkey"))) {
    throw "CorridorKey2.0 submodule missing at $ckSrc"
}

# 1) Plugin binary
$avx = Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"
if (-not (Test-Path $avx)) {
    throw "CorridorKey.avx not found at $avx. Run scripts\build_plugin.ps1 first."
}
Copy-Item $avx (Join-Path $OutDir "CorridorKey.avx") -Force
Write-Host "[1/5] Copied CorridorKey.avx"

# 2) Embeddable CPython (small; pip/torch installed at Setup time)
Write-Host "[2/5] Fetching embeddable CPython 3.12..."
$zip = Join-Path $env:TEMP "python-embed-ckavx.zip"
if (-not (Test-Path $zip)) {
    Invoke-WebRequest -Uri $PythonEmbedUrl -OutFile $zip
}
New-Item -ItemType Directory -Force -Path $pythonDir | Out-Null
Expand-Archive -Path $zip -DestinationPath $pythonDir -Force
Get-ChildItem $pythonDir -Filter "python*._pth" | ForEach-Object {
    $content = @(Get-Content $_.FullName)
    $content = $content | ForEach-Object {
        if ($_ -match '^#\s*import site') { 'import site' } else { $_ }
    }
    if ($content -notcontains 'import site') { $content += 'import site' }
    if ($content -notcontains 'Lib\site-packages') { $content += 'Lib\site-packages' }
    Set-Content $_.FullName $content
}
New-Item -ItemType Directory -Force -Path (Join-Path $pythonDir "Lib\site-packages") | Out-Null
if (-not (Test-Path (Join-Path $pythonDir "python.exe"))) {
    throw "embeddable python.exe missing after extract"
}
Write-Host "      python embed ready"

# 3) Worker sources
Write-Host "[3/5] Copying runtime worker..."
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
Copy-Item (Join-Path $RepoRoot "runtime\*.py") $runtimeDir -Force
Copy-Item (Join-Path $RepoRoot "runtime\requirements.txt") $runtimeDir -Force
Copy-Item (Join-Path $PSScriptRoot "post_install.ps1") (Join-Path $OutDir "post_install.ps1") -Force

# 4) CorridorKey2.0 source only (no weights / no local vfms blobs)
Write-Host "[4/5] Syncing CorridorKey2.0 source from submodule (no weights)..."
Robo-Mirror $ckSrc $ckOut @("weights", "vfms")
# Keep empty weight dirs so download_weights.py has targets
New-Item -ItemType Directory -Force -Path (Join-Path $ckOut "weights") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ckOut "vfms\cradio") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ckOut "vfms\moge") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $ckOut "vfms\rvm") | Out-Null
# Re-copy tracked gitkeeps if present
foreach ($gk in @(
    "weights\.gitkeep",
    "vfms\cradio\.gitkeep",
    "vfms\moge\.gitkeep",
    "vfms\rvm\.gitkeep"
)) {
    $srcGk = Join-Path $ckSrc $gk
    if (Test-Path $srcGk) {
        Copy-Item $srcGk (Join-Path $ckOut $gk) -Force
    }
}

# 5) Placeholder config (Setup rewrites paths)
Write-Host "[5/5] Writing payload config.json..."
$config = @{
    install_root = "C:\Program Files\CorridorKeyAVX"
    ck_root      = "C:\Program Files\CorridorKeyAVX\CorridorKey2.0"
    python_exe   = "C:\Program Files\CorridorKeyAVX\python\python.exe"
    server_py    = "C:\Program Files\CorridorKeyAVX\runtime\server.py"
    avx_path     = "C:\Program Files\Avid\AVX2_Plug-ins\CorridorKey.avx"
    version      = "2.0"
} | ConvertTo-Json
Set-Content -Path (Join-Path $OutDir "config.json") -Value $config -Encoding UTF8

Write-Host ""
Write-Host "Slim payload ready at $OutDir"
Write-Host "Weights and CUDA torch are installed by Setup via post_install.ps1"
exit 0
