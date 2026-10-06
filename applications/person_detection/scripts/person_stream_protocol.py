# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Binary UART/USB stream parser for person_detection host stream."""

from __future__ import annotations

import struct
import threading
import zlib
from collections import deque
from dataclasses import dataclass, field

MAGIC = b"\xa5\x5a\x34\x12"
VERSION = 1
TYPE_FRAME = 0x01
TYPE_DETECT = 0x02

FRAME_HDR_SIZE = 18
DETECT_HDR_SIZE = 19
BOX_SIZE = 21
MAX_FRAME_PAYLOAD = 128 * 128 * 2
MAX_DETECT_BOXES = 32
# One full frame (~33 KiB) plus detect message headroom
MAX_PARSER_BUF = 70000


@dataclass
class DetBox:
    x1: float
    y1: float
    x2: float
    y2: float
    score: float
    head: int


@dataclass
class FrameData:
    frame_id: int
    width: int
    height: int
    payload: bytes


@dataclass
class DetectionData:
    frame_id: int
    model_w: int
    model_h: int
    pad_left: int
    pad_top: int
    boxes: list[DetBox] = field(default_factory=list)


class StreamParser:
    def __init__(self) -> None:
        self._buf = bytearray()
        self._lock = threading.Lock()
        self._tail = bytearray()
        self.frames: deque[FrameData] = deque(maxlen=4)
        self.detections: deque[DetectionData] = deque(maxlen=8)
        self.stats = {
            "bytes_rx": 0,
            "magic_hits": 0,
            "frames_ok": 0,
            "frames_crc_err": 0,
            "frames_bad_len": 0,
            "dets_ok": 0,
            "dets_crc_err": 0,
            "resync": 0,
            "buf_trim": 0,
        }

    def feed(self, data: bytes) -> None:
        with self._lock:
            self.stats["bytes_rx"] += len(data)
            self._tail.extend(data)
            if len(self._tail) > 64:
                self._tail = self._tail[-64:]
            self._buf.extend(data)
            if len(self._buf) > MAX_PARSER_BUF:
                self.stats["buf_trim"] += 1
                self._buf.clear()
            self._parse()

    def flush(self) -> None:
        with self._lock:
            self._buf.clear()

    @property
    def tail_hex(self) -> str:
        with self._lock:
            return self._tail.hex()

    def pending_len(self) -> int:
        with self._lock:
            return len(self._buf)

    def _parse(self) -> None:
        while True:
            idx = self._buf.find(MAGIC)
            if idx < 0:
                if len(self._buf) > len(MAGIC):
                    self._buf = self._buf[-(len(MAGIC) - 1) :]
                return
            self.stats["magic_hits"] += 1
            if idx > 0:
                self.stats["resync"] += 1
                del self._buf[:idx]

            if len(self._buf) < 6:
                return

            ver = self._buf[4]
            msg_type = self._buf[5]

            if ver != VERSION:
                del self._buf[:1]
                continue

            if msg_type == TYPE_FRAME:
                if not self._try_parse_frame():
                    return
            elif msg_type == TYPE_DETECT:
                if not self._try_parse_detection():
                    return
            else:
                del self._buf[:1]

    def _try_parse_frame(self) -> bool:
        if len(self._buf) < FRAME_HDR_SIZE:
            return False

        _magic, ver, mtype, w, h, plen, fid = struct.unpack_from("<IBBHHII", self._buf, 0)
        expected_plen = w * h * 2
        if (
            plen > MAX_FRAME_PAYLOAD
            or w > 512
            or h > 512
            or w == 0
            or h == 0
            or plen != expected_plen
        ):
            self.stats["frames_bad_len"] += 1
            del self._buf[:1]
            return True

        total = FRAME_HDR_SIZE + plen + 4
        if len(self._buf) < total:
            return False

        payload = bytes(self._buf[FRAME_HDR_SIZE : FRAME_HDR_SIZE + plen])
        crc_expect = struct.unpack_from("<I", self._buf, FRAME_HDR_SIZE + plen)[0]
        crc_got = zlib.crc32(payload) & 0xFFFFFFFF

        if crc_got != crc_expect:
            self.stats["frames_crc_err"] += 1
            del self._buf[:total]
            return True

        self.frames.append(FrameData(frame_id=fid, width=w, height=h, payload=payload))
        self.stats["frames_ok"] += 1
        del self._buf[:total]
        return True

    def _try_parse_detection(self) -> bool:
        if len(self._buf) < DETECT_HDR_SIZE:
            return False

        _magic, ver, mtype, fid, mw, mh, pl, pt, cnt = struct.unpack_from(
            "<IBBIHHHHB", self._buf, 0
        )

        if cnt > MAX_DETECT_BOXES:
            del self._buf[:1]
            return True

        total = DETECT_HDR_SIZE + cnt * BOX_SIZE + 4
        if len(self._buf) < total:
            return False

        crc_payload = bytes(self._buf[4 : total - 4])
        crc_expect = struct.unpack_from("<I", self._buf, total - 4)[0]
        crc_got = zlib.crc32(crc_payload) & 0xFFFFFFFF

        if crc_got != crc_expect:
            self.stats["dets_crc_err"] += 1
            del self._buf[:total]
            return True

        boxes = []
        off = DETECT_HDR_SIZE
        for _ in range(cnt):
            x1, y1, x2, y2, score = struct.unpack_from("<fffff", self._buf, off)
            head = self._buf[off + 20]
            boxes.append(DetBox(x1=x1, y1=y1, x2=x2, y2=y2, score=score, head=head))
            off += BOX_SIZE

        self.detections.append(
            DetectionData(
                frame_id=fid, model_w=mw, model_h=mh, pad_left=pl, pad_top=pt, boxes=boxes
            )
        )
        self.stats["dets_ok"] += 1
        del self._buf[:total]
        return True
