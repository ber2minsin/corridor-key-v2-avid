"""Verify model path: no hint → Auto; with hint → External, both produce mattes."""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runtime"))
sys.path.insert(0, str(ROOT / "CorridorKey2.0" / "src"))

MP4 = ROOT / "green-screen-footage-for-test-avid.mp4"
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

    # Synthetic hint: keep center person-ish region white, rest black
    yy, xx = np.mgrid[0:h, 0:w]
    mask = ((xx - w * 0.45) ** 2 / (w * 0.18) ** 2 + (yy - h * 0.45) ** 2 / (h * 0.35) ** 2) < 1.0
    hint = np.zeros_like(rgba)
    hint[..., 0] = mask.astype(np.float32)
    hint[..., 1] = hint[..., 0]
    hint[..., 2] = hint[..., 0]
    hint[..., 3] = 1.0

    session = InferenceSession(CK)
    session.load()

    # No hint — must not set has_alpha_hint
    req_auto = FrameRequest(
        width=w, height=h, mode=0, has_alpha_hint=False,
        output_mode=OutputMode.MATTE, frame_number=10,
    )
    out_auto = session.process_frame(rgba, None, req_auto)
    a0 = float(out_auto[..., 0].max())  # matte in RGB
    print(f"no-hint matte max={a0:.4f}")

    req_hint = FrameRequest(
        width=w, height=h, mode=1, has_alpha_hint=True,
        output_mode=OutputMode.MATTE, frame_number=11,
        hint_quality=0.9,
    )
    out_hint = session.process_frame(rgba, hint, req_hint)
    a1 = float(out_hint[..., 0].max())
    print(f"with-hint matte max={a1:.4f}")

    if a0 < 0.1:
        print("FAIL no-hint empty matte")
        return 1
    if a1 < 0.1:
        print("FAIL with-hint empty matte")
        return 1
    print("PASS hint optional paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
