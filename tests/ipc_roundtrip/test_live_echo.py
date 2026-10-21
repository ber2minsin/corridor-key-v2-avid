"""Live IPC test against echo_server.py (spawned as sibling process)."""

from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
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


def main() -> int:
    import mmap
    import win32file

    server = subprocess.Popen(
        [sys.executable, str(Path(__file__).with_name("echo_server.py"))],
        cwd=str(ROOT),
    )
    try:
        time.sleep(1.0)
        # Connect
        deadline = time.time() + 10
        handle = None
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
                time.sleep(0.2)
        if handle is None:
            raise RuntimeError("Could not connect to echo pipe")

        win32file.WriteFile(handle, encode_message(MessageType.PING, {}))
        _, resp = win32file.ReadFile(handle, 65536)
        t, body = decode_message(resp)
        assert t == MessageType.PONG, (t, body)

        shm_in = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_INPUT)
        shm_out = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_OUTPUT)
        w, h = 32, 16
        rgba = np.full((h, w, 4), 0.25, dtype=np.float32)
        rgba[..., 3] = 1.0
        shm_in[:FRAME_HEADER_SIZE] = encode_frame_header(w, h, 4)
        shm_in[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + rgba.nbytes] = rgba.tobytes()

        win32file.WriteFile(
            handle,
            encode_message(
                MessageType.PROCESS_FRAME,
                {"width": w, "height": h, "has_alpha_hint": False, "output_mode": 0},
            ),
        )
        _, resp = win32file.ReadFile(handle, 65536)
        t, body = decode_message(resp)
        assert t == MessageType.FRAME_DONE, (t, body)
        out = np.frombuffer(
            shm_out[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + rgba.nbytes], dtype=np.float32
        ).reshape(h, w, 4)
        assert np.allclose(out[..., :3], 0.75), out[..., :3].mean()
        assert np.allclose(out[..., 3], 1.0)

        win32file.WriteFile(handle, encode_message(MessageType.SHUTDOWN, {}))
        win32file.CloseHandle(handle)
        shm_in.close()
        shm_out.close()
        print("live_echo: OK")
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except Exception:
            server.kill()


if __name__ == "__main__":
    raise SystemExit(main())
