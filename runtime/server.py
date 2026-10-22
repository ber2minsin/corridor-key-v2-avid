#!/usr/bin/env python3
"""CorridorKey AVX out-of-process inference worker."""

from __future__ import annotations

import argparse
import logging
import struct
import sys
import threading
import traceback
from pathlib import Path

# Embeddable CPython uses a ._pth file that does not put the script directory on
# sys.path, so sibling imports (ipc_protocol, inference_session) fail unless we
# add it explicitly.
_RUNTIME_DIR = Path(__file__).resolve().parent
if str(_RUNTIME_DIR) not in sys.path:
    sys.path.insert(0, str(_RUNTIME_DIR))

import numpy as np

from ipc_protocol import (
    FRAME_HEADER_SIZE,
    MAX_SHM_SIZE,
    PIPE_NAME,
    SHM_NAME_ALPHA_HINT,
    SHM_NAME_INPUT,
    SHM_NAME_OUTPUT,
    MessageType,
    decode_frame_header,
    decode_message,
    encode_frame_header,
    encode_message,
)
from inference_session import FrameRequest, InferenceSession

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("corridorkey_avx.server")


class SharedMemory:
    def __init__(self, name: str, size: int):
        import mmap

        self.name = name
        self.size = size
        self._mmap = mmap.mmap(-1, size, tagname=name)

    def close(self) -> None:
        self._mmap.close()

    def read_frame(self) -> tuple[int, int, np.ndarray]:
        header = self._mmap[:FRAME_HEADER_SIZE]
        w, h, c = decode_frame_header(header)
        nbytes = w * h * c * 4
        buf = self._mmap[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + nbytes]
        arr = np.frombuffer(buf, dtype=np.float32).reshape(h, w, c).copy()
        return w, h, arr

    def write_frame(self, rgba: np.ndarray) -> None:
        h, w, c = rgba.shape
        self._mmap[:FRAME_HEADER_SIZE] = encode_frame_header(w, h, c)
        flat = np.ascontiguousarray(rgba, dtype=np.float32).reshape(-1)
        raw = flat.tobytes()
        self._mmap[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + len(raw)] = raw


class PipeServer:
    def __init__(self, session: InferenceSession):
        self.session = session
        self._stop = threading.Event()

    def serve_forever(self) -> None:
        import win32file
        import win32pipe
        import pywintypes

        log.info("Creating named pipe %s", PIPE_NAME)
        while not self._stop.is_set():
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
            try:
                log.info("Waiting for plugin connection...")
                win32pipe.ConnectNamedPipe(handle, None)
                log.info("Plugin connected")
                self._client_loop(handle)
            except pywintypes.error as e:
                log.warning("Pipe error: %s", e)
            finally:
                try:
                    win32file.CloseHandle(handle)
                except Exception:
                    pass

    def _client_loop(self, handle) -> None:
        import win32file
        import pywintypes

        shm_in = SharedMemory(SHM_NAME_INPUT, MAX_SHM_SIZE)
        shm_out = SharedMemory(SHM_NAME_OUTPUT, MAX_SHM_SIZE)
        shm_hint = SharedMemory(SHM_NAME_ALPHA_HINT, MAX_SHM_SIZE)
        try:
            while not self._stop.is_set():
                try:
                    # MESSAGE-mode: one ReadFile returns the entire framed message.
                    # Do not split header/payload across ReadFile calls.
                    try:
                        _, data = win32file.ReadFile(handle, 1024 * 1024)
                    except pywintypes.error as e:
                        # ERROR_MORE_DATA (234): message larger than buffer — treat as fatal
                        if getattr(e, "winerror", None) == 234:
                            log.error("Incoming pipe message larger than 1MB")
                        break
                    if not data or len(data) < 8:
                        break
                    msg_type, body = decode_message(data)

                    reply_type, reply_body = self._handle(msg_type, body, shm_in, shm_out, shm_hint)
                    raw = encode_message(reply_type, reply_body)
                    win32file.WriteFile(handle, raw)
                    if msg_type == MessageType.SHUTDOWN:
                        self._stop.set()
                        break
                except Exception as e:
                    log.error("Client loop error: %s\n%s", e, traceback.format_exc())
                    try:
                        raw = encode_message(MessageType.ERROR, {"code": 1, "message": str(e)})
                        win32file.WriteFile(handle, raw)
                    except Exception:
                        break
        finally:
            shm_in.close()
            shm_out.close()
            shm_hint.close()

    def _handle(self, msg_type, body, shm_in, shm_out, shm_hint):
        if msg_type == MessageType.PING:
            return MessageType.PONG, {"ok": True}

        if msg_type == MessageType.LOAD_MODEL:
            self.session.load()
            return MessageType.MODEL_LOADED, {"device": self.session.device}

        if msg_type == MessageType.SHUTDOWN:
            return MessageType.STATUS, {"shutting_down": True}

        if msg_type == MessageType.PROCESS_FRAME:
            w, h, rgba = shm_in.read_frame()
            hint = None
            if body.get("has_alpha_hint"):
                _, _, hint = shm_hint.read_frame()
            req = FrameRequest(
                width=int(body.get("width", w)),
                height=int(body.get("height", h)),
                mode=int(body.get("mode", 0)),
                input_colorspace=int(body.get("input_colorspace", 0)),
                despill_strength=float(body.get("despill_strength", 0.5)),
                hint_quality=float(body.get("hint_quality", 0.95)),
                carry_hint=bool(body.get("carry_hint", True)),
                output_mode=int(body.get("output_mode", 0)),
                time_aware=int(body.get("time_aware", 0)),
                temporal_window=int(body.get("temporal_window", 16)),
                has_alpha_hint=bool(body.get("has_alpha_hint", False)),
                frame_number=int(body.get("frame_number", 0)),
            )
            t0 = __import__("time").time()
            out = self.session.process_frame(rgba, hint, req)
            shm_out.write_frame(out)
            ms = (__import__("time").time() - t0) * 1000.0
            return MessageType.FRAME_DONE, {
                "width": out.shape[1],
                "height": out.shape[0],
                "channels": 4,
                "processing_time_ms": ms,
            }

        if msg_type == MessageType.GET_CACHED_FRAME:
            fnum = int(body.get("frame_number", 0))
            mode = int(body.get("output_mode", 0))
            cached = self.session.get_cached(fnum, mode)
            if cached is None:
                return MessageType.ERROR, {"code": 2, "message": f"No cached frame {fnum}"}
            shm_out.write_frame(cached)
            return MessageType.FRAME_DONE, {"width": cached.shape[1], "height": cached.shape[0], "channels": 4}

        if msg_type == MessageType.ANALYZE_CLIP:
            req = FrameRequest(
                width=int(body.get("width", 0)),
                height=int(body.get("height", 0)),
                mode=int(body.get("mode", 0)),
                input_colorspace=int(body.get("input_colorspace", 0)),
                despill_strength=float(body.get("despill_strength", 0.5)),
                hint_quality=float(body.get("hint_quality", 0.95)),
                carry_hint=bool(body.get("carry_hint", True)),
                output_mode=int(body.get("output_mode", 0)),
                time_aware=1,
                temporal_window=int(body.get("temporal_window", 16)),
            )
            n = self.session.analyze_buffered(req)
            return MessageType.ANALYZE_DONE, {"frames": n}

        return MessageType.ERROR, {"code": 3, "message": f"Unknown message {msg_type}"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--ck-root",
        default=str(Path(__file__).resolve().parents[1] / "CorridorKey2.0"),
        help="Path to CorridorKey2.0 package root",
    )
    args = ap.parse_args()

    ck_root = Path(args.ck_root)
    if not ck_root.exists():
        log.error("CorridorKey2.0 root not found: %s", ck_root)
        return 1

    # Prefer pywin32; fall back message for packaging
    try:
        import win32pipe  # noqa: F401
    except ImportError:
        log.error("pywin32 is required in the bundled runtime")
        return 1

    session = InferenceSession(ck_root)
    PipeServer(session).serve_forever()
    return 0


if __name__ == "__main__":
    sys.exit(main())
