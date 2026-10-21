# CorridorKey for Avid Media Composer

Community AVX2 plugin that runs [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0) inside **Avid Media Composer** for AI green/blue screen keying.

Product overview: [corridorkey.com](https://corridorkey.com/)

This project is not affiliated with Avid or Corridor Digital.

---

## Requirements

- Windows 10 (22H2+) or Windows 11
- **NVIDIA** GPU with a current Game Ready / Studio driver ([download](https://www.nvidia.com/Download/index.aspx))
- **Avid Media Composer** (Standard, Ultimate, or equivalent — not Media Composer First)

Editors do not need to install Python or download model weights separately; the installer bundles the runtime.

Approximate VRAM: about 4 GB up to 1440p, about 6 GB for 4K (same guidance as CorridorKey 2.0).

---

## Install

1. Install and activate Media Composer; complete its first launch.
2. Install a current NVIDIA driver; reboot if Windows requests it.
3. Run **`Setup-CorridorKeyAVX.exe`** (release asset, or built under `packaging\dist\`).
4. Approve the UAC prompt and finish the wizard.
5. Fully quit Media Composer if it was open, then start it again.

Uninstall from **Settings → Apps → CorridorKey for Avid**.

---

## Use in Media Composer

1. Place a green or blue screen clip on the timeline.
2. Open **Tools → Effect Palette** (`Ctrl+8`).
3. Category **Image** → drag **CorridorKey** onto the clip.
4. Open **Effect Editor**.

### Controls

| Control | Description |
|--------|-------------|
| **Mode** | **Auto** — one-click key with no hint. **External Hint (FG alpha)** — use this clip’s alpha channel as an Alpha Hint when it is not flat opaque. |
| **Output** | Processed, Matte, Foreground, or Checkerboard |
| **Despill / Unmix** | Spill removal strength (0–100%) |
| **Colorspace** | sRGB or Linear |
| **Time Awareness** | Analyze the clip for temporal consistency (slower first pass; reduces flicker) |
| **Temporal Window** | Frames per temporal window when Time Awareness is on (1–80) |
| **Hint Quality** | How strongly to trust the Alpha Hint (0–100%) |
| **Carry Hint Alpha** | Carry hint across temporal windows |

The first frame after apply may take up to about a minute while the model loads on the GPU. That is expected.

If the effect is missing from the palette: quit Media Composer, confirm the installer completed, restart Media Composer, and re-apply the effect (remove any older CorridorKey instance from the clip first).

Logs (when diagnosing failures): `%LOCALAPPDATA%\CorridorKeyAVX\logs\`

---

## Alpha Hint

Media Composer delivers flat black video if this effect declares a second input, even when that input is optional. Alpha Hint is therefore **not** a second Effect Palette track.

To guide the key:

1. Put your B&W guide matte into the **alpha channel** of the clip (or a nest) you are keying.
2. Set **Mode** to **External Hint (FG alpha)**.
3. If alpha is flat opaque, the plugin stays in Auto and does not pass a hint to the model.

---

## Build from source (developers)

You need:

- [AVX2 Connectivity Toolkit](https://www.avid.com/alliance-partner-program/avx-connectivity-toolkit) checked out as `AVX2SDK` (gitignored)
- [CorridorKey 2.0](https://github.com/nikopueringer/CorridorKey2.0) next to this repo as `CorridorKey2.0` (gitignored)
- Visual Studio 2022 (C++), CMake (VS-bundled is fine)
- [Inno Setup 6](https://jrsoftware.org/isinfo.php) for the installer (`winget install JRSoftware.InnoSetup`)

```powershell
scripts\build_plugin.ps1
scripts\build_installer.ps1
scripts\run_all_tests.ps1
```

| Script | Purpose |
|--------|---------|
| `scripts\build_plugin.ps1` | Build `CorridorKey.avx` |
| `scripts\build_installer.ps1` | Bundle runtime and compile `Setup-CorridorKeyAVX.exe` |
| `scripts\deploy_plugin.ps1` | Copy the built `.avx` into Avid AVX2 folders (admin; MC must be quit) |
| `scripts\run_all_tests.ps1` | Automated IPC / inference checks without Media Composer |

Installer output: `packaging\dist\Setup-CorridorKeyAVX.exe`

Dev-only copy without rebuilding the EXE: elevated `Install-CorridorKey.bat`. Prefer the Setup EXE for anything you distribute.

---

## License and credits

See [LICENSE](LICENSE).

CorridorKey 2.0 engine and weights remain under Corridor Digital’s terms ([upstream](https://github.com/nikopueringer/CorridorKey2.0)). Avid and Media Composer are trademarks of Avid Technology, Inc.

Support this community build: [buymeacoffee.com/ber2minsin](https://www.buymeacoffee.com/ber2minsin)
