#!/usr/bin/env python3
"""
Copyright (c) 2026 Nordic Semiconductor
SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

Feed images to the image_classification application.

Each image is shown in a window and sent as an RGB565 frame message over the
input UART (nRF9251 DK: VCOM1). The firmware logs the inference result on its
console UART (VCOM0); nothing is sent back on the input UART.

Frame message (little-endian):
  [4B magic 0x12345AA5] [1B version=1] [1B type=0x01] [2B width] [2B height]
  [4B payload_len] [4B frame_id] [payload: RGB565 big-endian] [4B crc32(payload)]

Image sources (combine as needed):
  * image files or directories (PNG/JPEG/BMP)
  * --axon-test-vectors HEADER   all int8 test inputs of a compiled Axon model header
  * --cifar-test-batch FILE      CIFAR-10 python test batch (shows the true label)

Keys in the window: space = send next image now, q = quit.

Dependencies: pyserial, opencv-python, numpy, pillow
"""
from __future__ import annotations

import argparse
import pickle
import random
import re
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial required: pip install pyserial") from exc

MAGIC = b"\xa5\x5a\x34\x12"
VERSION = 1
TYPE_FRAME = 0x01

# Square frame size sent by default for each firmware model (its input size).
MODEL_FRAME_SIZE = {"cifar10": 32, "vww": 96, "wildfire": 96}

CIFAR10_LABELS = [
    "airplane", "automobile", "bird", "cat", "deer",
    "dog", "frog", "horse", "ship", "truck",
]
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp"}


@dataclass
class Sample:
    name: str
    rgb: np.ndarray  # HxWx3 uint8
    truth: str | None = None


def load_image_files(paths: list[Path]) -> list[Sample]:
    try:
        from PIL import Image, ImageOps
    except ImportError as exc:
        raise SystemExit("Install Pillow: pip install pillow") from exc

    files: list[Path] = []
    for p in paths:
        if p.is_dir():
            files.extend(sorted(f for f in p.iterdir() if f.suffix.lower() in IMAGE_SUFFIXES))
        else:
            files.append(p)

    return [Sample(f.name, np.asarray(ImageOps.exif_transpose(Image.open(f)).convert("RGB")))
            for f in files]


def load_axon_test_vectors(header: Path) -> list[Sample]:
    text = header.read_text(encoding="utf-8")
    samples = []
    for m in re.finditer(
        r"(\w+)_test_input_vector_(\d+)\[3\]\[(\d+)\]\[(\d+)\]\s*=\s*\{(.*?)\};", text, re.DOTALL
    ):
        idx, h, w = int(m.group(2)), int(m.group(3)), int(m.group(4))
        vals = np.array([int(v) for v in re.findall(r"-?\d+", m.group(5))], dtype=np.int16)
        rgb = (vals.reshape(3, h, w).transpose(1, 2, 0) + 128).astype(np.uint8)
        samples.append(Sample(f"{m.group(1)} test vector {idx}", rgb))
    if not samples:
        raise SystemExit(f"no [3][H][W] test input vectors found in {header}")
    return samples


def load_cifar_test_batch(path: Path, count: int, indices: list[int] | None) -> list[Sample]:
    with path.open("rb") as f:
        batch = pickle.load(f, encoding="bytes")
    data = batch[b"data"].reshape(-1, 3, 32, 32).transpose(0, 2, 3, 1)
    labels = batch[b"labels"]
    if indices is None:
        indices = random.sample(range(len(labels)), min(count, len(labels)))
    return [Sample(f"CIFAR-10 test #{i}", data[i], CIFAR10_LABELS[labels[i]]) for i in indices]


def encode_frame(frame_id: int, width: int, height: int, rgb565_be: bytes) -> bytes:
    hdr = MAGIC + struct.pack("<BBHHII", VERSION, TYPE_FRAME, width, height, len(rgb565_be),
                              frame_id & 0xFFFFFFFF)
    return hdr + rgb565_be + struct.pack("<I", zlib.crc32(rgb565_be) & 0xFFFFFFFF)


def to_rgb565_be(rgb: np.ndarray) -> bytes:
    r = (rgb[:, :, 0].astype(np.uint16) >> 3) << 11
    g = (rgb[:, :, 1].astype(np.uint16) >> 2) << 5
    b = rgb[:, :, 2].astype(np.uint16) >> 3
    return (r | g | b).astype(">u2").tobytes()


def crop_square(rgb: np.ndarray) -> np.ndarray:
    h, w = rgb.shape[:2]
    side = min(h, w)
    top, left = (h - side) // 2, (w - side) // 2
    return rgb[top : top + side, left : left + side]


def resize(rgb: np.ndarray, size: int) -> np.ndarray:
    square = crop_square(rgb)
    if square.shape[0] == size:
        return square
    interp = cv2.INTER_AREA if square.shape[0] > size else cv2.INTER_NEAREST
    return cv2.resize(square, (size, size), interpolation=interp)


def show(window: str, sample: Sample, frame_id: int, sent_rgb: np.ndarray, display: int) -> None:
    """Original (cropped) image at display size, plus a panel with the exact frame sent."""
    square = crop_square(sample.rgb)
    interp = cv2.INTER_AREA if square.shape[0] > display else cv2.INTER_CUBIC
    main = cv2.resize(square, (display, display), interpolation=interp)

    panel_w = display // 2
    panel = np.zeros((display, panel_w, 3), dtype=np.uint8)
    thumb = cv2.resize(sent_rgb, (panel_w - 16, panel_w - 16), interpolation=cv2.INTER_NEAREST)
    panel[30 : 30 + thumb.shape[0], 8 : 8 + thumb.shape[1]] = thumb

    img = cv2.cvtColor(np.hstack([main, panel]), cv2.COLOR_RGB2BGR)
    cv2.putText(img, f"sent {sent_rgb.shape[1]}x{sent_rgb.shape[0]}", (display + 8, 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1)

    bar = np.zeros((60, img.shape[1], 3), dtype=np.uint8)
    cv2.putText(bar, f"#{frame_id} {sample.name}", (8, 22),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1)
    if sample.truth is not None:
        cv2.putText(bar, f"truth: {sample.truth}", (8, 48),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 1)
    cv2.imshow(window, np.vstack([img, bar]))


def main() -> int:
    ap = argparse.ArgumentParser(description="Feed images to image_classification over UART")
    ap.add_argument("images", nargs="*", type=Path, help="Image files or directories")
    ap.add_argument("--port", default="/dev/ttyACM1", help="Input UART (nRF9251 DK: VCOM1)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rtscts", action=argparse.BooleanOptionalAction, default=False)
    ap.add_argument("--model", choices=MODEL_FRAME_SIZE, default="cifar10",
                    help="Model the firmware was built with; sets the default --size")
    ap.add_argument("--size", type=int,
                    help="Square frame size sent to the device, at least the model input "
                    "(default: the model input size)")
    ap.add_argument("--interval", type=float, default=2.0, help="Seconds between images")
    ap.add_argument("--loop", action="store_true", help="Repeat the image list forever")
    ap.add_argument("--shuffle", action="store_true")
    ap.add_argument("--axon-test-vectors", type=Path, metavar="HEADER")
    ap.add_argument("--cifar-test-batch", type=Path, metavar="FILE",
                    help="cifar-10-batches-py/test_batch")
    ap.add_argument("--count", type=int, default=20, help="Random CIFAR-10 samples to send")
    ap.add_argument("--indices", type=lambda s: [int(v) for v in s.split(",")],
                    help="Comma-separated CIFAR-10 test indices instead of random samples")
    ap.add_argument("--display", type=int, default=480, help="Displayed image size in pixels")
    args = ap.parse_args()
    if args.size is None:
        args.size = MODEL_FRAME_SIZE[args.model]

    samples: list[Sample] = []
    if args.images:
        samples += load_image_files(args.images)
    if args.axon_test_vectors:
        samples += load_axon_test_vectors(args.axon_test_vectors)
    if args.cifar_test_batch:
        samples += load_cifar_test_batch(args.cifar_test_batch, args.count, args.indices)
    if not samples:
        ap.error("no images: pass files/directories, --axon-test-vectors or --cifar-test-batch")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1, rtscts=args.rtscts)
    except serial.SerialException as exc:
        print(f"Cannot open {args.port}: {exc}", file=sys.stderr)
        return 1

    window = "Image fed to device"
    frame_id = 0
    print(f"Sending {len(samples)} image(s) as {args.size}x{args.size} RGB565 to {args.port} "
          f"@ {args.baud}. Watch the device console for results. Keys: space=next, q=quit.")

    try:
        while True:
            order = list(samples)
            if args.shuffle:
                random.shuffle(order)
            for sample in order:
                frame_id += 1
                rgb = resize(sample.rgb, args.size)
                show(window, sample, frame_id, rgb, args.display)
                cv2.waitKey(1)

                t0 = time.monotonic()
                ser.write(encode_frame(frame_id, args.size, args.size, to_rgb565_be(rgb)))
                ser.flush()
                truth = f" (truth: {sample.truth})" if sample.truth else ""
                print(f"#{frame_id} {sample.name}{truth} sent in {time.monotonic() - t0:.2f} s")

                deadline = t0 + args.interval
                while True:
                    key = cv2.waitKey(max(1, int((deadline - time.monotonic()) * 1000))) & 0xFF
                    if key == ord("q"):
                        return 0
                    if key == ord(" ") or time.monotonic() >= deadline:
                        break
            if not args.loop:
                break
        cv2.waitKey(0)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    sys.exit(main())
