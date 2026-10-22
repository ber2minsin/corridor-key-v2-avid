# Full uninstall of Corridor Key 2.0 for Avid (requires Administrator).
# Prefer Windows Settings -> Apps -> Corridor Key 2.0 when installed via Setup EXE.

#Requires -RunAsAdministrator
$ErrorActionPreference = "Continue"

Write-Host "Uninstalling Corridor Key 2.0..."

foreach ($p in @(
    "$env:ProgramFiles\Avid\AVX2_Plug-ins\CorridorKey.avx",
    "$env:ProgramFiles\Avid\AVX2_Plug-Ins\CorridorKey.avx",
    "$env:ProgramFiles\Avid\Avid Media Composer\AVX2_Plug-Ins\CorridorKey.avx"
)) {
    if (Test-Path $p) {
        Remove-Item $p -Force
        Write-Host "Removed $p"
    }
}

$app = Join-Path $env:ProgramFiles "CorridorKeyAVX"
if (Test-Path $app) {
    foreach ($j in @("python", "CorridorKey2.0")) {
        $jp = Join-Path $app $j
        if (Test-Path $jp) {
            $item = Get-Item $jp -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                cmd /c rmdir "$jp" | Out-Null
                Write-Host "Removed junction $jp"
            }
        }
    }
    Remove-Item $app -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "Removed $app"
}

Remove-Item "HKLM:\Software\CorridorKeyAVX" -Recurse -Force -ErrorAction SilentlyContinue
[Environment]::SetEnvironmentVariable("CORRIDORKEY_AVX_ROOT", $null, "Machine")
[Environment]::SetEnvironmentVariable("CORRIDORKEY_AVX_ROOT", $null, "User")

Write-Host "Done. Restart Media Composer if it is open."
