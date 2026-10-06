#!/usr/bin/env python3
# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Capture UART bytes and report whether person_detection stream frames parse."""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pip install pyserial") from exc

from person_stream_protocol import MAGIC, StreamParser


def main() -> int:
    ap = argparse.ArgumentParser(description="Diagnose person_detection UART binary stream")
    ap.add_argument("--port", default="/dev/ttyACM1")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument(
        "--rtscts",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Hardware flow control (default off for diagnosis)",
    )
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2, rtscts=args.rtscts)
        ser.reset_input_buffer()
    except serial.SerialException as exc:
        print(f"Cannot open {args.port}: {exc}", file=sys.stderr)
        return 1

    parser = StreamParser()
    print(f"Capturing {args.seconds}s on {args.port} @ {args.baud} (rtscts={args.rtscts})")
    print(f"Expected frame magic: {MAGIC.hex()} (0x12345AA5 LE)")
    print("Reset the DK now if you want boot-to-stream in this window.\n")

    t0 = time.monotonic()
    while time.monotonic() - t0 < args.seconds:
        chunk = ser.read(65536)
        if chunk:
            parser.feed(chunk)

    ser.close()
    s = parser.stats
    print("--- results ---")
    for k, v in s.items():
        print(f"  {k}: {v}")
    print(f"  pending: {parser.pending_len()}")
    print(f"  tail64: {parser.tail_hex()}")

    if s["magic_hits"] == 0 and s["bytes_rx"] > 0:
        print(
            "\nNo magic in capture — likely wrong port (try other ttyACM) or not binary stream UART.",
            file=sys.stderr,
        )
    elif s["frames_ok"] == 0 and s["frames_crc_err"] > 0:
        print(
            "\nMagic seen but CRC fails — baud mismatch (try 115200 and 1000000) or use --rtscts / --no-rtscts.",
            file=sys.stderr,
        )
    elif s["frames_ok"] > 0:
        print("\nFrames parse OK — use view_person_stream.py with the same --port and --baud.")

    return 0 if s["frames_ok"] > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
