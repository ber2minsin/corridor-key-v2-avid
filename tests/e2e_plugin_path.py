"""End-to-end test of the same path the AVX plugin uses:

spawn worker → LOAD_MODEL → PROCESS_FRAME via pipe + shared memory → keyed matte.

This does not load Media Composer, but it proves the installed runtime contract.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runtime"))

from ipc_protocol import (  # noqa: E402
    FRAME_HEADER_SIZE,
    MAX_SHM_SIZE,
    PIPE_NAME,
    SHM_NAME_INPUT,
    SHM_NAME_OUTPUT,
    MessageType,
    decode_message,
    encode_frame_header,
    encode_message,
)


def _find_python() -> str:
    candidates = [
        ROOT / "CorridorKey2.0" / ".venv" / "Scripts" / "python.exe",
        Path(os.environ.get("CORRIDORKEY_AVX_ROOT", "")) / "python" / "python.exe",
        Path(r"C:\Program Files\CorridorKeyAVX\python\python.exe"),
    ]
    for c in candidates:
        if c and c.exists():
            return str(c)
    return sys.executable


def main() -> int:
    import mmap
    import win32file

    try:
        import torch
    except ImportError:
        print("SKIP: torch missing")
        return 0
    if not torch.cuda.is_available():
        print("SKIP: no CUDA")
        return 0

    py = _find_python()
    server = ROOT / "runtime" / "server.py"
    ck = ROOT / "CorridorKey2.0"
    proc = subprocess.Popen(
        [py, str(server), "--ck-root", str(ck)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        # Wait for pipe
        handle = None
        deadline = time.time() + 60
        while time.time() < deadline:
            try:
                handle = win32file.CreateFile(
                    PIPE_NAME,
                    win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                    0,
                    None,
                    win32file.OPEN_EXISTING,
                    0,
                    None,
                )
                break
            except Exception:
                time.sleep(0.5)
        if handle is None:
            out = proc.stdout.read() if proc.stdout else ""
            raise RuntimeError(f"Worker pipe not ready.\n{out}")

        def roundtrip(msg_type, body, timeout_read=600000):
            win32file.WriteFile(handle, encode_message(msg_type, body))
            # Large reads for long inference
            _, resp = win32file.ReadFile(handle, 1024 * 1024)
            return decode_message(resp)

        t, body = roundtrip(MessageType.PING, {})
        assert t == MessageType.PONG, (t, body)
        print("PING OK")

        print("LOAD_MODEL (may take a while)...")
        t, body = roundtrip(MessageType.LOAD_MODEL, {})
        assert t == MessageType.MODEL_LOADED, (t, body)
        print("MODEL_LOADED", body)

        # Synthetic greenscreen
        h, w = 64, 96
        rgba = np.zeros((h, w, 4), np.float32)
        rgba[..., 1] = 0.7
        rgba[..., 3] = 1.0
        rgba[16:48, 32:64, 0] = 0.8
        rgba[16:48, 32:64, 1] = 0.6
        rgba[16:48, 32:64, 2] = 0.55

        shm_in = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_INPUT)
        shm_out = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_OUTPUT)
        shm_in[:FRAME_HEADER_SIZE] = encode_frame_header(w, h, 4)
        shm_in[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + rgba.nbytes] = rgba.tobytes()

        print("PROCESS_FRAME...")
        t, body = roundtrip(
            MessageType.PROCESS_FRAME,
            {
                "width": w,
                "height": h,
                "mode": 0,
                "output_mode": 1,  # matte
                "despill_strength": 0.5,
                "has_alpha_hint": False,
                "frame_number": 0,
            },
        )
        assert t == MessageType.FRAME_DONE, (t, body)
        out = np.frombuffer(
            shm_out[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + rgba.nbytes], dtype=np.float32
        ).reshape(h, w, 4)
        corner = float(out[2, 2, 0])
        center = float(out[32, 48, 0])
        print(f"matte corner={corner:.3f} center={center:.3f} time_ms={body.get('processing_time_ms')}")
        assert center > corner + 0.2, "Expected subject center brighter than green corner on matte"

        win32file.WriteFile(handle, encode_message(MessageType.SHUTDOWN, {}))
        win32file.CloseHandle(handle)
        shm_in.close()
        shm_out.close()
        print("e2e_plugin_path: OK")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
