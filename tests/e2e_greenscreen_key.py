"""E2E: run CorridorKey inference on the user's greenscreen mp4 and verify a non-empty matte."""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runtime"))
sys.path.insert(0, str(ROOT / "CorridorKey2.0" / "src"))

MP4 = ROOT / "green-screen-footage-for-test-avid.mp4"
OUT = ROOT / "tests" / "out_e2e_greenscreen"
CK = ROOT / "CorridorKey2.0"


def main() -> int:
    import imageio.v2 as iio
    from inference_session import FrameRequest, InferenceSession
    from ipc_protocol import OutputMode

    if not MP4.exists():
        print("FAIL missing", MP4)
        return 2

    reader = iio.get_reader(str(MP4))
    frame = reader.get_data(10)
    reader.close()
    h, w = frame.shape[:2]
    rgba = np.zeros((h, w, 4), dtype=np.float32)
    rgba[..., :3] = frame.astype(np.float32) / 255.0
    rgba[..., 3] = 1.0
    print(f"input {w}x{h} mean RGB={rgba[..., :3].mean(axis=(0, 1))}")

    session = InferenceSession(CK)
    session.load()
    req = FrameRequest(
        width=w,
        height=h,
        output_mode=OutputMode.PROCESSED,
        frame_number=10,
    )
    packed = session._run_single(rgba, None, req)
    a = packed[..., 3]
    print(
        f"alpha mean={float(a.mean()):.4f} max={float(a.max()):.4f} "
        f"fg_mean={float(packed[..., :3].mean()):.4f}"
    )
    OUT.mkdir(parents=True, exist_ok=True)
    iio.imwrite(str(OUT / "input.png"), (rgba[..., :3] * 255).astype(np.uint8))
    iio.imwrite(str(OUT / "alpha.png"), (np.clip(a, 0, 1) * 255).astype(np.uint8))
    composed = packed[..., :3] * a[..., None]
    iio.imwrite(str(OUT / "processed.png"), (np.clip(composed, 0, 1) * 255).astype(np.uint8))
    matte = np.stack([a, a, a], axis=-1)
    iio.imwrite(str(OUT / "matte.png"), (np.clip(matte, 0, 1) * 255).astype(np.uint8))

    if float(a.max()) < 0.1:
        print("FAIL empty matte")
        return 1
    if float(a.mean()) < 0.01:
        print("FAIL near-empty matte")
        return 1
    print("PASS wrote", OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
