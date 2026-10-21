"""IPC protocol for CorridorKey AVX (named pipe + shared memory)."""

from __future__ import annotations

import json
import struct
from enum import IntEnum
from typing import Any, Optional

PIPE_NAME = r"\\.\pipe\CorridorKeyAVX"
SHM_NAME_INPUT = "CorridorKeyAVX_Input"
SHM_NAME_OUTPUT = "CorridorKeyAVX_Output"
SHM_NAME_ALPHA_HINT = "CorridorKeyAVX_AlphaHint"

MAX_FRAME_WIDTH = 4096
MAX_FRAME_HEIGHT = 4096
BYTES_PER_PIXEL = 16  # float32 RGBA
FRAME_HEADER_SIZE = 16
MAX_SHM_SIZE = MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * BYTES_PER_PIXEL + FRAME_HEADER_SIZE


class MessageType(IntEnum):
    PING = 0
    PROCESS_FRAME = 1
    SHUTDOWN = 2
    LOAD_MODEL = 3
    ANALYZE_CLIP = 4
    GET_CACHED_FRAME = 5
    PONG = 100
    FRAME_DONE = 101
    ERROR = 102
    MODEL_LOADED = 103
    STATUS = 104
    ANALYZE_DONE = 105


class OutputMode(IntEnum):
    PROCESSED = 0
    MATTE = 1
    FOREGROUND = 2
    CHECKER = 3


class AlphaHintMode(IntEnum):
    AUTO = 0
    EXTERNAL = 1


class InputColorspace(IntEnum):
    SRGB = 0
    LINEAR = 1


def encode_message(msg_type: MessageType, payload: Optional[dict[str, Any]] = None) -> bytes:
    if payload is None:
        payload = {}
    payload_bytes = json.dumps(payload).encode("utf-8")
    header = struct.pack("<II", int(msg_type), len(payload_bytes))
    return header + payload_bytes


def decode_message(data: bytes) -> tuple[MessageType, dict[str, Any]]:
    if len(data) < 8:
        raise ValueError(f"Message too short: {len(data)} bytes")
    msg_type_raw, payload_len = struct.unpack("<II", data[:8])
    msg_type = MessageType(msg_type_raw)
    if payload_len > 0:
        payload = json.loads(data[8 : 8 + payload_len].decode("utf-8"))
    else:
        payload = {}
    return msg_type, payload


def encode_frame_header(width: int, height: int, channels: int = 4) -> bytes:
    return struct.pack("<IIII", width, height, channels, 0)


def decode_frame_header(data: bytes) -> tuple[int, int, int]:
    width, height, channels, _ = struct.unpack("<IIII", data[:FRAME_HEADER_SIZE])
    return width, height, channels
