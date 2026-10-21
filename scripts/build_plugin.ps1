param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$cmake = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $cmake = "cmake"
}

if (-not $env:AVX2SDK_ROOT) {
    $env:AVX2SDK_ROOT = Join-Path $RepoRoot "AVX2SDK"
}

$build = Join-Path $RepoRoot "plugin\build"
& $cmake -S (Join-Path $RepoRoot "plugin") -B $build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config $Config -j 8
exit $LASTEXITCODE
