# Run every automated test we can without Media Composer.
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Continue"
$failed = 0
$results = @()

function Step($name, $scriptBlock) {
    Write-Host ""
    Write-Host "=== $name ===" -ForegroundColor Cyan
    try {
        & $scriptBlock
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "exit code $LASTEXITCODE"
        }
        $script:results += [pscustomobject]@{ Test = $name; Result = "PASS" }
        Write-Host "PASS: $name" -ForegroundColor Green
    } catch {
        $script:failed++
        $script:results += [pscustomobject]@{ Test = $name; Result = "FAIL: $_" }
        Write-Host "FAIL: $name - $_" -ForegroundColor Red
    }
}

$cmake = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$py = Join-Path $RepoRoot "CorridorKey2.0\.venv\Scripts\python.exe"
if (-not (Test-Path $py)) { $py = "python" }

Step "Build CorridorKey.avx (x64 Release)" {
    $env:AVX2SDK_ROOT = Join-Path $RepoRoot "AVX2SDK"
    & $cmake -S (Join-Path $RepoRoot "plugin") -B (Join-Path $RepoRoot "plugin\build") -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    & $cmake --build (Join-Path $RepoRoot "plugin\build") --config Release -j 8
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
    $avx = Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"
    if (-not (Test-Path $avx)) { throw "CorridorKey.avx missing after build" }
}

Step "IPC encode/decode unit tests" {
    & python (Join-Path $RepoRoot "tests\ipc_roundtrip\test_ipc_roundtrip.py")
    if ($LASTEXITCODE -ne 0) { throw "ipc_roundtrip failed" }
}

Step "Live IPC echo (pipe + shared memory)" {
    & python (Join-Path $RepoRoot "tests\ipc_roundtrip\test_live_echo.py")
    if ($LASTEXITCODE -ne 0) { throw "live_echo failed" }
}

Step "Inference smoke (CUDA + CK2.0 one frame)" {
    $env:PYTHONUNBUFFERED = "1"
    & $py (Join-Path $RepoRoot "tests\inference_smoke.py")
    if ($LASTEXITCODE -ne 0) { throw "inference_smoke failed" }
}

Step "E2E plugin path (worker LOAD_MODEL + PROCESS_FRAME)" {
    $env:PYTHONUNBUFFERED = "1"
    & $py (Join-Path $RepoRoot "tests\e2e_plugin_path.py")
    if ($LASTEXITCODE -ne 0) { throw "e2e_plugin_path failed" }
}

Write-Host ""
Write-Host "======== SUMMARY ========"
$results | Format-Table -AutoSize
if ($failed -gt 0) {
    Write-Host "$failed test(s) FAILED" -ForegroundColor Red
    exit 1
}
Write-Host "All automated tests PASSED" -ForegroundColor Green
Write-Host "Next: install Media Composer, run Setup-CorridorKey-2.0.exe, check Effect Palette -> Image -> Corridor Key 2.0"
exit 0
