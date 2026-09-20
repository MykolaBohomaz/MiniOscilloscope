"""Host-side implementation of the STM32L432 scope binary protocol.

Mirrors Core/App/protocol.h. Frame:

    A5 5A | type u8 | seq u8 | len u16le | payload[len] | crc32le

CRC-32 is zlib's, computed over type..payload. docs/protocol.md has the
payload layouts; the struct formats below are the executable version of it.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from typing import Iterator, List, Optional

SYNC = b"\xA5\x5A"
MAX_PAYLOAD = 1024

# message types
PING, GET_INFO, GET_STATUS = 0x01, 0x02, 0x03
SET_RUN, SET_TIMEBASE, SET_TRIGGER, SET_SIGGEN = 0x10, 0x11, 0x12, 0x14
GET_CAPTURE, GET_MEAS = 0x20, 0x21
CAL_GET, CAL_SET, CAL_SAVE, RESET_STATS = 0x30, 0x31, 0x32, 0x3F
ACK, INFO, STATUS, MEAS, CAL, PONG = 0x80, 0x81, 0x82, 0x83, 0x84, 0x8F
CAPTURE_HDR, CAPTURE_DATA, LOG = 0x90, 0x91, 0x9F

ACK_TEXT = {0: "ok", 1: "bad length", 2: "bad argument", 3: "unknown command",
            4: "busy", 5: "failed"}
ACQ_STATES = ["STOPPED", "ARMED", "POSTTRIG", "READY"]
TRIG_MODES = ["AUTO", "NORMAL", "SINGLE"]
EDGES = ["rising", "falling"]
WAVES = ["off", "sine", "square", "triangle"]
GEN_FREQS = [100, 1000, 10000]
SMP_CYCLES = [2.5, 6.5, 12.5, 24.5, 47.5, 92.5, 247.5, 640.5]


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode(msg_type: int, seq: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too large")
    body = struct.pack("<BBH", msg_type, seq & 0xFF, len(payload)) + payload
    return SYNC + body + struct.pack("<I", crc32(body))


@dataclass
class Frame:
    type: int
    seq: int
    payload: bytes


@dataclass
class Parser:
    """Byte-stream decoder with resynchronisation, same rules as the firmware."""
    frames_ok: int = 0
    crc_errors: int = 0
    len_errors: int = 0
    _buf: bytearray = field(default_factory=bytearray)

    def feed(self, data: bytes) -> Iterator[Frame]:
        self._buf += data
        while True:
            i = self._buf.find(SYNC)
            if i < 0:
                # keep a trailing A5 that may be the first half of a sync
                self._buf = self._buf[-1:] if self._buf[-1:] == b"\xA5" else bytearray()
                return
            del self._buf[:i]
            if len(self._buf) < 6:
                return
            msg_type, seq, n = struct.unpack_from("<BBH", self._buf, 2)
            if n > MAX_PAYLOAD:
                self.len_errors += 1
                del self._buf[:2]
                continue
            if len(self._buf) < 10 + n:
                return
            body = bytes(self._buf[2:6 + n])
            (crc,) = struct.unpack_from("<I", self._buf, 6 + n)
            if crc != crc32(body):
                self.crc_errors += 1
                del self._buf[:2]
                continue
            del self._buf[:10 + n]
            self.frames_ok += 1
            yield Frame(msg_type, seq, body[4:])


# ---------------------------------------------------------------- payloads --

@dataclass
class Timebase:
    index: int
    ns_per_div: int
    period_cycles: int
    record_len: int
    smp: int
    timer_clk: int

    @property
    def sample_rate(self) -> float:
        return self.timer_clk / self.period_cycles

    @property
    def label(self) -> str:
        ns = self.ns_per_div
        for div, unit in ((1_000_000_000, "s"), (1_000_000, "ms"), (1_000, "us")):
            if ns >= div and ns % div == 0:
                return f"{ns // div}{unit}"
        return f"{ns}ns"


@dataclass
class Info:
    fw: str
    protocol: int
    timer_clk: int
    buf_len: int
    record_max: int
    lcd: tuple
    n_ranges: int
    chunk: int
    timebases: List[Timebase]
    uid: str


def parse_info(p: bytes) -> Info:
    (ma, mi, pa, proto, clk, buf_len, rec_max, w, h, n_ranges, n_tb, chunk) = \
        struct.unpack_from("<BBBBIIIHHBBH", p, 0)
    off = 24
    tbs = []
    for i in range(n_tb):
        ns, per, rec, smp = struct.unpack_from("<IIHB", p, off)
        tbs.append(Timebase(i, ns, per, rec, smp, clk))
        off += 11
    uid = "".join(f"{w:08X}" for w in struct.unpack_from("<III", p, off))
    return Info(f"{ma}.{mi}.{pa}", proto, clk, buf_len, rec_max, (w, h), n_ranges,
                chunk, tbs, uid)


STATUS_FIELDS = ["uptime_ms", "records", "triggers", "auto_triggers", "search_skips",
                 "records_discarded", "max_stop_overshoot", "adc_overruns", "dma_events",
                 "lcd_frames", "tx_packets", "tx_dropped", "rx_frames_ok", "rx_crc_errors",
                 "rx_len_errors", "loop_max_us", "loop_count", "boot_faults",
                 "tx_queue_hwm", "adc_errors"]


def parse_status(p: bytes) -> dict:
    (state, mode, running, tb, level, hyst, edge, pre, rng, gen, vdda) = \
        struct.unpack_from("<BBBBHHBBBBf", p, 0)
    vals = struct.unpack_from("<20I", p, 16)
    d = dict(state=ACQ_STATES[state] if state < 4 else state, mode=TRIG_MODES[mode],
             running=bool(running), timebase=tb, trig_level=level, trig_hyst=hyst,
             edge=EDGES[edge], pretrig_pct=pre, range=rng, gen=WAVES[gen], vdda_mv=vdda)
    d.update(zip(STATUS_FIELDS, vals))
    return d


@dataclass
class CaptureHeader:
    id: int
    n: int
    trig_index: int
    trig_frac: float
    timer_clk: int
    period_cycles: int
    ns_per_div: int
    smp: int
    range: int
    flags: int
    mode: int
    trig_level: int
    chunk: int
    vdda_mv: float
    gain: float
    offset_mv: float
    crc: int

    @property
    def sample_rate(self) -> float:
        return self.timer_clk / self.period_cycles

    @property
    def forced(self) -> bool:
        return bool(self.flags & 1)


CAPTURE_HDR_FMT = "<HIIfIIIBBBBHHfffI"
assert struct.calcsize(CAPTURE_HDR_FMT) == 50


def parse_capture_header(p: bytes) -> CaptureHeader:
    return CaptureHeader(*struct.unpack(CAPTURE_HDR_FMT, p))


def parse_capture_chunk(p: bytes):
    cid, offset, count = struct.unpack_from("<HIH", p, 0)
    samples = struct.unpack_from(f"<{count}H", p, 8)
    return cid, offset, samples


def parse_meas(p: bytes) -> dict:
    vpp, vavg, vrms, f, duty, flags, _, mn, mx = struct.unpack("<fffffBBHH", p)
    return dict(valid=bool(flags & 1), periodic=bool(flags & 2), clipped=bool(flags & 4),
                vpp_mv=vpp, vavg_mv=vavg, vrms_mv=vrms, freq_hz=f, duty_pct=duty,
                min_code=mn, max_code=mx)


def trigger_payload(level: int, hyst: int, edge: int, mode: int, pre: int) -> bytes:
    return struct.pack("<HHBBB", level, hyst, edge, mode, pre)


def raw_to_volts(codes, hdr: CaptureHeader):
    """Codes -> input volts using the calibration sent with the capture."""
    k = hdr.vdda_mv / 4095.0
    return [hdr.gain * (c * k - hdr.offset_mv) / 1000.0 for c in codes]


def ack_ok(frame: Optional[Frame]) -> bool:
    return frame is not None and frame.type == ACK and frame.payload[1] == 0
