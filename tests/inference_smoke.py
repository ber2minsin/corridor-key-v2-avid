"""Optional GPU smoke: run one CK2.0 frame through InferenceSession.

Skipped automatically if CUDA / weights are unavailable.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runtime"))

from inference_session import FrameRequest, InferenceSession  # noqa: E402
from ipc_protocol import OutputMode  # noqa: E402


def main() -> int:
    try:
        import torch
    except ImportError:
        print("SKIP: torch not installed")
        return 0
    if not torch.cuda.is_available():
        print("SKIP: no CUDA GPU")
        return 0

    ck = ROOT / "CorridorKey2.0"
    if not (ck / "weights" / "corridorkey_v2.safetensors").exists():
        print("SKIP: weights missing")
        return 0

    fixture = ROOT / "tests" / "fixtures" / "greenscreen_64.png"
    if not fixture.exists():
        from imageio.v2 import imwrite

        h, w = 64, 96
        img = np.zeros((h, w, 3), dtype=np.uint8)
        img[:] = (0, 180, 0)
        img[16:48, 32:64] = (200, 160, 140)
        imwrite(fixture, img)

    from imageio.v2 import imread

    rgb = np.asarray(imread(fixture)).astype(np.float32) / 255.0
    rgba = np.concatenate([rgb, np.ones((*rgb.shape[:2], 1), np.float32)], axis=-1)

    session = InferenceSession(ck)
    print("Loading model (first time can take a while)...")
    session.load()
    req = FrameRequest(width=rgba.shape[1], height=rgba.shape[0], output_mode=OutputMode.MATTE)
    print("Processing one frame...")
    out = session.process_frame(rgba, None, req)
    assert out.shape == rgba.shape
    # Subject should have higher alpha than pure green corners on a matte view
    # (matte mode puts alpha into RGB)
    corner = float(out[2, 2, 0])
    center = float(out[32, 48, 0])
    print(f"matte corner={corner:.3f} center={center:.3f}")
    print("inference_smoke: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
