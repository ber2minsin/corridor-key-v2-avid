"""CorridorKey 2.0 inference session for the AVX worker."""

from __future__ import annotations

import logging
import os
import shutil
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import numpy as np

from ipc_protocol import AlphaHintMode, InputColorspace, OutputMode

log = logging.getLogger("corridorkey_avx.session")


@dataclass
class FrameRequest:
    width: int
    height: int
    mode: int = AlphaHintMode.AUTO
    input_colorspace: int = InputColorspace.SRGB
    despill_strength: float = 0.5
    hint_quality: float = 0.95
    carry_hint: bool = True
    output_mode: int = OutputMode.PROCESSED
    time_aware: int = 0
    temporal_window: int = 16  # hann_chunk when time-aware (1–80)
    has_alpha_hint: bool = False
    frame_number: int = 0


class InferenceSession:
    def __init__(self, ck_root: Path):
        self.ck_root = Path(ck_root).resolve()
        self.model = None
        self.device = "cuda"
        self._cache: dict[int, np.ndarray] = {}
        self._analyze_buffer: list[tuple[int, np.ndarray, Optional[np.ndarray]]] = []
        self._work = Path(tempfile.gettempdir()) / "CorridorKeyAVX"
        self._work.mkdir(parents=True, exist_ok=True)

    def _ensure_weights(self) -> None:
        """Fetch CorridorKey weights via the official upstream download script if missing."""
        marker = self.ck_root / "weights" / "corridorkey_v2.safetensors"
        if marker.is_file():
            return
        script = self.ck_root / "scripts" / "download_weights.py"
        if not script.is_file():
            raise RuntimeError(
                f"CorridorKey weights missing at {marker} and download script not found at {script}"
            )
        log.warning("Weights missing — running official download_weights.py (several GB)...")
        import subprocess
        import sys

        cmd = [sys.executable, str(script), "--package_root", str(self.ck_root)]
        proc = subprocess.run(cmd, check=False)
        if proc.returncode != 0 or not marker.is_file():
            raise RuntimeError(
                "Failed to download CorridorKey weights. "
                "Re-run packaging\\post_install.ps1 or scripts\\sync_corridorkey.ps1 with network access."
            )
        log.info("Weight download finished: %s", marker)

    def load(self) -> None:
        if self.model is not None:
            return
        import sys

        sys.path.insert(0, str(self.ck_root))
        import torch
        from corridorkey.runtime import RuntimePaths, load_model, prepare_runtime_environment

        if not torch.cuda.is_available():
            raise RuntimeError(
                "No NVIDIA GPU with CUDA was found. CorridorKey needs an NVIDIA graphics card."
            )

        self._ensure_weights()
        paths = RuntimePaths.from_package(self.ck_root)
        paths.validate()
        prepare_runtime_environment(paths)
        log.info("Loading CorridorKey 2.0 model from %s", paths.weights)
        t0 = time.time()
        self.model = load_model(paths, device=self.device)
        log.info("Model loaded in %.1fs", time.time() - t0)

    def process_frame(
        self,
        rgba: np.ndarray,
        hint_rgba: Optional[np.ndarray],
        req: FrameRequest,
    ) -> np.ndarray:
        self.load()

        if req.time_aware and req.frame_number in self._cache:
            return self._compose_output(self._cache[req.frame_number], req.output_mode)

        self._analyze_buffer.append(
            (req.frame_number, rgba.copy(), None if hint_rgba is None else hint_rgba.copy())
        )
        if len(self._analyze_buffer) > 300:
            self._analyze_buffer = self._analyze_buffer[-300:]

        result = self._run_single(rgba, hint_rgba, req)
        if req.time_aware:
            self._cache[req.frame_number] = result
        return self._compose_output(result, req.output_mode)

    def get_cached(self, frame_number: int, output_mode: int) -> Optional[np.ndarray]:
        if frame_number not in self._cache:
            return None
        return self._compose_output(self._cache[frame_number], output_mode)

    def analyze_buffered(self, req: FrameRequest) -> int:
        """Re-key buffered frames with temporal Hann settings into the cache."""
        self.load()
        if len(self._analyze_buffer) < 2:
            # Not enough frames; keep per-frame results
            count = 0
            for fnum, rgba, hint in self._analyze_buffer:
                self._cache[fnum] = self._run_single(
                    rgba, hint, FrameRequest(**{**req.__dict__, "frame_number": fnum, "time_aware": 0})
                )
                count += 1
            return count

        frames = sorted(self._analyze_buffer, key=lambda x: x[0])
        job = self._work / f"analyze_{int(time.time())}"
        inp = job / "input"
        hint_dir = job / "hint"
        out = job / "out"
        inp.mkdir(parents=True)
        out.mkdir(parents=True)
        has_hint = any(f[2] is not None for f in frames)
        if has_hint:
            hint_dir.mkdir(parents=True)

        try:
            from imageio.v2 import imwrite

            for idx, (_fnum, rgba, hint_rgba) in enumerate(frames):
                rgb_u8 = np.clip(rgba[..., :3] * 255.0, 0, 255).astype(np.uint8)
                imwrite(str(inp / f"{idx:06d}.png"), rgb_u8)
                if has_hint:
                    if hint_rgba is not None:
                        h = np.clip(hint_rgba[..., 0] * 255.0, 0, 255).astype(np.uint8)
                    else:
                        h = np.full(rgba.shape[:2], 128, dtype=np.uint8)
                    imwrite(str(hint_dir / f"{idx:06d}.png"), h)

            tw = int(req.temporal_window) if req.temporal_window else 16
            tw = max(2, min(80, tw, len(frames)))
            self._run_job(
                inp,
                out,
                req,
                num_frames=len(frames),
                hann_chunk=tw,
                initial_hint=None,
                hint_video=str(hint_dir) if has_hint else None,
            )

            # Map outputs back by index order
            alphas = sorted(out.rglob("*alpha*"))
            fgs = sorted(out.rglob("*fg*"))
            for idx, (fnum, rgba, _hint) in enumerate(frames):
                packed = np.zeros_like(rgba, dtype=np.float32)
                packed[..., :3] = rgba[..., :3]
                packed[..., 3] = 1.0
                if idx < len(fgs):
                    packed[..., :3] = self._load_rgb(fgs[idx])
                if idx < len(alphas):
                    a = self._load_gray(alphas[idx])
                    packed[..., 3] = a
                self._cache[fnum] = packed
            return len(frames)
        finally:
            shutil.rmtree(job, ignore_errors=True)

    def _run_single(
        self,
        rgba: np.ndarray,
        hint_rgba: Optional[np.ndarray],
        req: FrameRequest,
    ) -> np.ndarray:
        from imageio.v2 import imwrite

        job = self._work / f"frame_{os.getpid()}_{req.frame_number}_{int(time.time() * 1000)}"
        inp = job / "input"
        out = job / "out"
        inp.mkdir(parents=True)
        out.mkdir(parents=True)
        try:
            rgb_u8 = np.clip(rgba[..., :3] * 255.0, 0, 255).astype(np.uint8)
            imwrite(str(inp / "000000.png"), rgb_u8)
            hint_first = None
            if req.has_alpha_hint and hint_rgba is not None:
                h = np.clip(hint_rgba[..., 0] * 255.0, 0, 255).astype(np.uint8)
                hint_path = job / "hint.png"
                imwrite(str(hint_path), h)
                hint_first = str(hint_path)

            self._run_job(
                inp,
                out,
                req,
                num_frames=1,
                hann_chunk=1,
                initial_hint=hint_first,
                hint_video=None,
            )

            packed = np.zeros((rgba.shape[0], rgba.shape[1], 4), dtype=np.float32)
            packed[..., :3] = rgba[..., :3]
            packed[..., 3] = 1.0
            fg = self._find_rgb(out, ("fg", "foreground"))
            alpha = self._find_gray(out, ("alpha",))
            if fg is not None:
                packed[..., :3] = self._resize_to(fg, rgba.shape[0], rgba.shape[1])
            if alpha is not None:
                packed[..., 3] = self._resize_gray(alpha, rgba.shape[0], rgba.shape[1])
            log.info(
                "frame %s packed fg=%s alpha=%s in_mean=%.4f a_mean=%.4f a_max=%.4f",
                req.frame_number,
                fg is not None,
                alpha is not None,
                float(np.mean(rgba[..., :3])),
                float(np.mean(packed[..., 3])),
                float(np.max(packed[..., 3])),
            )
            return packed
        finally:
            shutil.rmtree(job, ignore_errors=True)

    def _run_job(
        self,
        inp: Path,
        out: Path,
        req: FrameRequest,
        *,
        num_frames: int,
        hann_chunk: int,
        initial_hint: Optional[str],
        hint_video: Optional[str],
    ) -> None:
        from corridorkey.runtime import InferenceConfig, WriterConfig, run_inference

        cfg = InferenceConfig(
            hann_chunk=hann_chunk,
            hann_stride=max(1, hann_chunk // 2),
            carry_hint=req.carry_hint,
            hint_quality=req.hint_quality,
            carry_hint_quality=req.hint_quality,
            despill_strength=float(req.despill_strength),
            frame_dir_linear=(req.input_colorspace == InputColorspace.LINEAR),
            outputs={"alpha", "fg"},
            # Single-frame AVX on modern GPUs: keep stages on CUDA. low_vram=True
            # spills batons to system RAM/CPU and looks like "CPU inference".
            low_vram=False,
            auto_vram=False,
            num_frames=num_frames,
            # Bundled opencv-python builds often lack OpenEXR — use mp4 writers.
            output_format="video",
        )
        writer_cfg = WriterConfig(ffmpeg_bin="ffmpeg", pix_fmt="yuv444p", crf=18, preset="veryfast")
        run_inference(
            self.model,
            str(inp),
            str(out),
            cfg,
            writer_cfg,
            initial_hint_path=initial_hint,
            hint_video_path=hint_video,
        )

    def _compose_output(self, packed: np.ndarray, output_mode: int) -> np.ndarray:
        """Compose a display frame for the Avid host.

        Media Composer composites effect alpha over black in the record monitor.
        If we leave keyed-out alpha at 0, the viewer goes fully black even when
        RGB is correct. Always return precomposed RGB with opaque alpha so the
        editor can see the result; true cutout-with-alpha can be added later.
        """
        out = np.zeros_like(packed)
        a = np.clip(packed[..., 3:4], 0.0, 1.0)
        fg = packed[..., :3]
        if output_mode == OutputMode.MATTE:
            out[..., :3] = a
        elif output_mode == OutputMode.FOREGROUND:
            out[..., :3] = fg
        elif output_mode == OutputMode.CHECKER:
            h, w = packed.shape[:2]
            yy, xx = np.mgrid[0:h, 0:w]
            check = (((xx // 32) + (yy // 32)) % 2).astype(np.float32) * 0.25 + 0.35
            out[..., :3] = fg * a + check[..., None] * (1.0 - a)
        else:
            # Processed: keyed foreground over black
            out[..., :3] = fg * a
        out[..., 3] = 1.0
        return out.astype(np.float32)

    @staticmethod
    def _resize_to(rgb: np.ndarray, h: int, w: int) -> np.ndarray:
        if rgb.shape[0] == h and rgb.shape[1] == w:
            return rgb[..., :3]
        try:
            import cv2

            return cv2.resize(rgb[..., :3], (w, h), interpolation=cv2.INTER_LINEAR)
        except Exception:
            return rgb[:h, :w, :3]

    @staticmethod
    def _resize_gray(a: np.ndarray, h: int, w: int) -> np.ndarray:
        if a.shape[0] == h and a.shape[1] == w:
            return a
        try:
            import cv2

            return cv2.resize(a, (w, h), interpolation=cv2.INTER_LINEAR)
        except Exception:
            return a[:h, :w]

    @staticmethod
    def _load_rgb(path: Path) -> np.ndarray:
        found = InferenceSession._find_rgb(path.parent, (path.stem,))
        return found if found is not None else np.zeros((1, 1, 3), np.float32)

    @staticmethod
    def _load_gray(path: Path) -> np.ndarray:
        found = InferenceSession._find_gray(path.parent, (path.stem,))
        return found if found is not None else np.zeros((1, 1), np.float32)

    @staticmethod
    def _find_rgb(root: Path, names: tuple[str, ...]) -> Optional[np.ndarray]:
        files = [root] if root.is_file() else list(root.rglob("*"))
        for p in files:
            if not p.is_file():
                continue
            low = p.stem.lower()
            if not any(n.lower() in low for n in names):
                continue
            if p.suffix.lower() in {".png", ".exr", ".tif", ".tiff", ".jpg", ".mp4", ".mov"}:
                return InferenceSession._read_float_rgb(p)
        return None

    @staticmethod
    def _find_gray(root: Path, names: tuple[str, ...]) -> Optional[np.ndarray]:
        files = [root] if root.is_file() else list(root.rglob("*"))
        for p in files:
            if not p.is_file():
                continue
            low = p.stem.lower()
            if not any(n.lower() in low for n in names):
                continue
            if p.suffix.lower() in {".png", ".exr", ".tif", ".tiff", ".jpg", ".mp4", ".mov"}:
                arr = InferenceSession._read_float_rgb(p)
                return arr[..., 0] if arr.ndim == 3 else arr
        return None

    @staticmethod
    def _read_float_rgb(path: Path) -> np.ndarray:
        """Load an RGB float image. CorridorKey writers store BGR via OpenCV."""
        import cv2

        suffix = path.suffix.lower()
        if suffix in {".mp4", ".mov", ".avi"}:
            from imageio.v2 import get_reader

            with get_reader(str(path)) as reader:
                img = np.asarray(reader.get_data(0))  # RGB
                if img.dtype == np.uint8:
                    img = img.astype(np.float32) / 255.0
                elif img.dtype == np.uint16:
                    img = img.astype(np.float32) / 65535.0
                else:
                    img = np.clip(img.astype(np.float32), 0.0, 1.0)
        else:
            raw = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
            if raw is None:
                from imageio.v2 import imread

                img = np.asarray(imread(path))
                if img.ndim == 2:
                    img = np.stack([img, img, img], axis=-1)
                return np.clip(img[..., :3].astype(np.float32), 0.0, 1.0)
            was_uint8 = raw.dtype == np.uint8
            was_uint16 = raw.dtype == np.uint16
            if raw.ndim == 2:
                img = cv2.cvtColor(raw, cv2.COLOR_GRAY2RGB)
            elif raw.shape[-1] >= 3:
                img = cv2.cvtColor(raw[..., :3], cv2.COLOR_BGR2RGB)
            else:
                img = raw
            img = img.astype(np.float32)
            if was_uint8:
                img /= 255.0
            elif was_uint16:
                img /= 65535.0
            else:
                # EXR / float — scene-linear; clamp for 8-bit Avid host
                img = np.clip(img, 0.0, 1.0)
        if img.ndim == 2:
            img = np.stack([img, img, img], axis=-1)
        elif img.ndim == 3 and img.shape[-1] == 1:
            img = np.repeat(img, 3, axis=-1)
        return np.clip(img[..., :3].astype(np.float32), 0.0, 1.0)
