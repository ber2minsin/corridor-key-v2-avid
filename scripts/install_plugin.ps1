# Developer helper: copy built .avx into Avid plug-ins folder and point at this repo.
# End users should use Setup-CorridorKeyAVX.exe instead.

param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$avx = Join-Path $RepoRoot "plugin\build\bin\Release\CorridorKey.avx"
if (-not (Test-Path $avx)) {
    throw "Build the plugin first (scripts\build_plugin.ps1). Missing $avx"
}

$destDir = Join-Path ${env:ProgramFiles} "Avid\AVX2_Plug-ins"
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
}

Copy-Item $avx (Join-Path $destDir "CorridorKey.avx") -Force
[Environment]::SetEnvironmentVariable("CORRIDORKEY_AVX_ROOT", $RepoRoot, "User")
Write-Host "Installed CorridorKey.avx to $destDir"
Write-Host "Set user env CORRIDORKEY_AVX_ROOT=$RepoRoot"
Write-Host "Restart WindTunnel / Media Composer."
