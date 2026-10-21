"""Minimal echo worker for IPC bring-up (no CK2.0 model)."""

from __future__ import annotations

import struct
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
    import win32pipe

    shm_in = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_INPUT)
    shm_out = mmap.mmap(-1, MAX_SHM_SIZE, tagname=SHM_NAME_OUTPUT)

    handle = win32pipe.CreateNamedPipe(
        PIPE_NAME,
        win32pipe.PIPE_ACCESS_DUPLEX,
        win32pipe.PIPE_TYPE_MESSAGE | win32pipe.PIPE_READMODE_MESSAGE | win32pipe.PIPE_WAIT,
        1,
        65536,
        65536,
        0,
        None,
    )
    print("echo_server: waiting on", PIPE_NAME, flush=True)
    win32pipe.ConnectNamedPipe(handle, None)
    print("echo_server: connected", flush=True)

    while True:
        _, data = win32file.ReadFile(handle, 8)
        if len(data) < 8:
            break
        _t, payload_len = struct.unpack("<II", data)
        payload = b""
        if payload_len:
            _, payload = win32file.ReadFile(handle, payload_len)
        msg_type, body = decode_message(data + payload)
        if msg_type == MessageType.PING:
            win32file.WriteFile(handle, encode_message(MessageType.PONG, {"ok": True}))
        elif msg_type == MessageType.LOAD_MODEL:
            win32file.WriteFile(handle, encode_message(MessageType.MODEL_LOADED, {"echo": True}))
        elif msg_type == MessageType.PROCESS_FRAME:
            w = int(body.get("width", 0))
            h = int(body.get("height", 0))
            nbytes = w * h * 4 * 4
            arr = np.frombuffer(
                shm_in[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + nbytes], dtype=np.float32
            ).reshape(h, w, 4).copy()
            # Invert RGB as proof of processing
            out = arr.copy()
            out[..., :3] = 1.0 - out[..., :3]
            shm_out[:FRAME_HEADER_SIZE] = encode_frame_header(w, h, 4)
            shm_out[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + nbytes] = out.tobytes()
            win32file.WriteFile(
                handle,
                encode_message(
                    MessageType.FRAME_DONE,
                    {"width": w, "height": h, "channels": 4, "processing_time_ms": 1.0},
                ),
            )
        elif msg_type == MessageType.SHUTDOWN:
            win32file.WriteFile(handle, encode_message(MessageType.STATUS, {"bye": True}))
            break
        else:
            win32file.WriteFile(
                handle, encode_message(MessageType.ERROR, {"code": 1, "message": "unknown"})
            )
    win32file.CloseHandle(handle)
    shm_in.close()
    shm_out.close()
    print("echo_server: done", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 — server exits when client disconnects
        print("echo_server: exit", exc, flush=True)
        raise SystemExit(0)
