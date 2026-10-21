"""IPC roundtrip without loading the heavy CK2.0 model.

Starts a minimal echo server using the same framing, or talks to a running worker
in echo mode via PROCESS_FRAME after patching session.

This test validates encode/decode and shared-memory layout locally.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "runtime"))

from ipc_protocol import (  # noqa: E402
    FRAME_HEADER_SIZE,
    MessageType,
    decode_frame_header,
    decode_message,
    encode_frame_header,
    encode_message,
)


def test_message_roundtrip():
    raw = encode_message(MessageType.PING, {"hello": "world"})
    t, body = decode_message(raw)
    assert t == MessageType.PING
    assert body["hello"] == "world"


def test_frame_header_roundtrip():
    hdr = encode_frame_header(1920, 1080, 4)
    w, h, c = decode_frame_header(hdr)
    assert (w, h, c) == (1920, 1080, 4)


def test_shm_layout_bytes():
    w, h = 64, 48
    rgba = np.linspace(0, 1, w * h * 4, dtype=np.float32).reshape(h, w, 4)
    buf = bytearray(FRAME_HEADER_SIZE + rgba.nbytes)
    buf[:FRAME_HEADER_SIZE] = encode_frame_header(w, h, 4)
    buf[FRAME_HEADER_SIZE:] = rgba.tobytes()
    w2, h2, c2 = decode_frame_header(bytes(buf[:FRAME_HEADER_SIZE]))
    assert (w2, h2, c2) == (w, h, 4)
    back = np.frombuffer(bytes(buf[FRAME_HEADER_SIZE:]), dtype=np.float32).reshape(h, w, 4)
    assert np.allclose(back, rgba)


if __name__ == "__main__":
    test_message_roundtrip()
    test_frame_header_roundtrip()
    test_shm_layout_bytes()
    print("ipc_roundtrip: OK")
