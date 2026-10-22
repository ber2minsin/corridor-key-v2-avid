#Requires -RunAsAdministrator
# Copy built CorridorKey.avx into both Avid AVX2 folders. Quit Media Composer first.
$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$avx = Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"
if (-not (Test-Path $avx)) { throw "Build missing: $avx" }

$mc = Get-Process AvidMediaComposer -ErrorAction SilentlyContinue
if ($mc) {
    throw "Quit Avid Media Composer completely first (still running pid $($mc.Id -join ','))."
}

$targets = @(
    (Join-Path $env:ProgramFiles "Avid\AVX2_Plug-ins\CorridorKey.avx"),
    (Join-Path $env:ProgramFiles "Avid\Avid Media Composer\AVX2_Plug-Ins\CorridorKey.avx"),
    (Join-Path $env:ProgramFiles "CorridorKeyAVX\CorridorKey.avx")
)

foreach ($t in $targets) {
    $dir = Split-Path $t -Parent
    if (-not (Test-Path $dir)) {
        if ($t -like "*CorridorKeyAVX*") { continue }
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    if (-not (Test-Path $dir)) { Write-Host "skip missing dir $dir"; continue }
    Copy-Item $avx $t -Force
    $i = Get-Item $t
    Write-Host ("OK {0}  {1} bytes  {2}" -f $t, $i.Length, $i.LastWriteTime)
}

# Keep installed runtime in sync with repo (pipe protocol / worker).
$pfRuntime = Join-Path $env:ProgramFiles "CorridorKeyAVX\runtime"
$repoRuntime = Join-Path $RepoRoot "runtime"
if (Test-Path $pfRuntime) {
    foreach ($py in @("server.py", "inference_session.py", "ipc_protocol.py")) {
        $srcPy = Join-Path $repoRuntime $py
        if (Test-Path $srcPy) {
            Copy-Item $srcPy (Join-Path $pfRuntime $py) -Force
            Write-Host "OK synced runtime\$py"
        }
    }
}

# Kill stale CorridorKey workers so MC picks up the new server.py
Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -like '*server.py*' -and $_.CommandLine -like '*CorridorKey*' } |
    ForEach-Object {
        Write-Host "Killing stale worker pid $($_.ProcessId)"
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }

Write-Host ""
Write-Host "Done. Start Media Composer. Effect Palette -> Image -> Corridor Key 2.0."
Write-Host "Remove any old CorridorKey from the clip, then drag the new one on again."
