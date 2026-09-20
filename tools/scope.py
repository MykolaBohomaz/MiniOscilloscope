#!/usr/bin/env python3
"""Command-line utility for the STM32L432 scope (USART2 / ST-LINK VCP).

Examples
    scope.py info                          # firmware, timebase table, UID
    scope.py status                        # state + all runtime counters
    scope.py timebase 4                    # select 100 us/div
    scope.py trigger --level 1.2 --edge falling --mode normal --pre 20
    scope.py gen sine 1000                 # built-in DAC test signal on PA4
    scope.py capture -o cap.csv --plot     # next triggered record
    scope.py selftest --report docs/validation/loopback.md   # PA4 -> PA0 jumper

The port defaults to the first ST-LINK VCP found; override with --port or
SCOPE_PORT.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import os
import random
import statistics
import struct
import sys
import time

import scope_proto as sp

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install -r tools/requirements.txt")

BAUD = 921600


def find_port() -> str:
    env = os.environ.get("SCOPE_PORT")
    if env:
        return env
    for p in serial.tools.list_ports.comports():
        desc = f"{p.description} {p.manufacturer or ''}".lower()
        if "stlink" in desc or "st-link" in desc or p.vid == 0x0483:
            return p.device
    sys.exit("no ST-LINK VCP found; pass --port")


class Device:
    def __init__(self, port: str, verbose: bool = False):
        self.ser = serial.Serial(port, BAUD, timeout=0.05)
        self.parser = sp.Parser()
        self.seq = random.randrange(256)
        self.verbose = verbose
        self.pending: list[sp.Frame] = []
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    # -- transport -----------------------------------------------------------
    def _read_frames(self):
        data = self.ser.read(self.ser.in_waiting or 1)
        for f in self.parser.feed(data):
            if f.type == sp.LOG:
                print(f"[device] {f.payload.decode(errors='replace')}", file=sys.stderr)
            else:
                self.pending.append(f)

    def send(self, msg_type: int, payload: bytes = b"") -> int:
        self.seq = (self.seq + 1) & 0xFF
        self.ser.write(sp.encode(msg_type, self.seq, payload))
        return self.seq

    def wait(self, types, seq=None, timeout=1.0):
        types = (types,) if isinstance(types, int) else tuple(types)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for i, f in enumerate(self.pending):
                if f.type in types and (seq is None or f.seq == seq):
                    return self.pending.pop(i)
            self._read_frames()
        return None

    def request(self, msg_type, payload=b"", reply=sp.ACK, timeout=1.0):
        seq = self.send(msg_type, payload)
        f = self.wait((reply, sp.ACK), seq, timeout)
        if f is None:
            raise TimeoutError(f"no reply to 0x{msg_type:02X}")
        if f.type == sp.ACK and reply != sp.ACK:
            raise RuntimeError(f"command 0x{msg_type:02X} refused: {sp.ACK_TEXT.get(f.payload[1])}")
        return f

    def command(self, msg_type, payload=b""):
        f = self.request(msg_type, payload)
        if f.payload[1] != 0:
            raise RuntimeError(f"command 0x{msg_type:02X}: {sp.ACK_TEXT.get(f.payload[1])}")

    # -- API -----------------------------------------------------------------
    def info(self) -> sp.Info:
        return sp.parse_info(self.request(sp.GET_INFO, reply=sp.INFO).payload)

    def status(self) -> dict:
        return sp.parse_status(self.request(sp.GET_STATUS, reply=sp.STATUS).payload)

    def meas(self) -> dict:
        return sp.parse_meas(self.request(sp.GET_MEAS, reply=sp.MEAS).payload)

    def set_run(self, mode: int):
        self.command(sp.SET_RUN, bytes([mode]))

    def set_timebase(self, index: int):
        self.command(sp.SET_TIMEBASE, bytes([index]))

    def set_trigger(self, level, hyst, edge, mode, pre):
        self.command(sp.SET_TRIGGER, sp.trigger_payload(level, hyst, edge, mode, pre))

    def set_gen(self, wave: int, freq_index: int):
        self.command(sp.SET_SIGGEN, bytes([wave, freq_index]))

    def capture(self, current=False, timeout=5.0):
        """Return (header, samples) of one record, verifying chunk and record CRCs."""
        seq = self.send(sp.GET_CAPTURE, bytes([1 if current else 0]))
        f = self.wait((sp.CAPTURE_HDR, sp.ACK), seq, timeout)
        if f is None:
            raise TimeoutError("no capture (no trigger in NORMAL mode?)")
        if f.type == sp.ACK:
            raise RuntimeError(f"capture refused: {sp.ACK_TEXT.get(f.payload[1])}")
        hdr = sp.parse_capture_header(f.payload)
        samples = [None] * hdr.n
        got = 0
        while got < hdr.n:
            c = self.wait(sp.CAPTURE_DATA, seq, timeout=2.0)
            if c is None:
                raise TimeoutError(f"capture stalled at {got}/{hdr.n} samples")
            cid, off, chunk = sp.parse_capture_chunk(c.payload)
            if cid != hdr.id:
                continue
            samples[off:off + len(chunk)] = chunk
            got += len(chunk)
        raw = struct.pack(f"<{hdr.n}H", *samples)
        if sp.crc32(raw) != hdr.crc:
            raise RuntimeError("record CRC mismatch")
        return hdr, list(samples)


# ------------------------------------------------------------------- analysis

def estimate_frequency(samples, fs):
    """Mean-crossing frequency estimate with linear interpolation (host side,
    independent of the firmware's measure.c)."""
    mid = (max(samples) + min(samples)) / 2
    hyst = (max(samples) - min(samples)) * 0.1
    rises, low = [], samples[0] < mid
    for i in range(1, len(samples)):
        if low and samples[i] > mid + hyst:
            j = i
            while j > 0 and samples[j - 1] >= mid:
                j -= 1
            if j > 0:
                y0, y1 = samples[j - 1], samples[j]
                rises.append(j - 1 + (mid - y0) / (y1 - y0))
            low = False
        elif not low and samples[i] < mid - hyst:
            low = True
    if len(rises) < 2:
        return None
    return fs * (len(rises) - 1) / (rises[-1] - rises[0])


# ------------------------------------------------------------------- commands

def cmd_info(dev, args):
    i = dev.info()
    print(f"firmware {i.fw}  protocol v{i.protocol}  uid {i.uid}")
    print(f"timer clock {i.timer_clk / 1e6:.3f} MHz, buffer {i.buf_len} samples, "
          f"max record {i.record_max}, display {i.lcd[0]}x{i.lcd[1]}")
    print(f"{'idx':>3} {'time/div':>9} {'sample rate':>14} {'record':>7} {'sampling':>9}")
    for tb in i.timebases:
        print(f"{tb.index:>3} {tb.label:>9} {tb.sample_rate / 1e6:>10.4f} MS/s "
              f"{tb.record_len:>7} {sp.SMP_CYCLES[tb.smp]:>7} cyc")


def cmd_status(dev, args):
    for k, v in dev.status().items():
        print(f"{k:>20}: {v:.1f}" if isinstance(v, float) else f"{k:>20}: {v}")


def cmd_meas(dev, args):
    m = dev.meas()
    if not m["valid"]:
        print("no record yet")
        return
    print(f"Vpp {m['vpp_mv']:.1f} mV   Vavg {m['vavg_mv']:.1f} mV   Vrms {m['vrms_mv']:.1f} mV")
    if m["periodic"]:
        print(f"f {m['freq_hz']:.3f} Hz   duty {m['duty_pct']:.2f} %")
    if m["clipped"]:
        print("WARNING: record clipped at the ADC rails")


def cmd_run(dev, args):
    dev.set_run({"stop": 0, "run": 1, "single": 2}[args.cmd])


def cmd_timebase(dev, args):
    dev.set_timebase(args.index)


def cmd_trigger(dev, args):
    st = dev.status()
    vdda = st["vdda_mv"]
    level = st["trig_level"] if args.level is None else round(args.level * 1000 / vdda * 4095)
    dev.set_trigger(max(0, min(4095, level)), args.hyst, sp.EDGES.index(args.edge),
                    sp.TRIG_MODES.index(args.mode.upper()), args.pre)


def cmd_gen(dev, args):
    wave = sp.WAVES.index(args.wave)
    freq = sp.GEN_FREQS.index(args.freq) if wave else 0
    dev.set_gen(wave, freq)


def cmd_capture(dev, args):
    hdr, s = dev.capture(current=args.current)
    fs = hdr.sample_rate
    volts = sp.raw_to_volts(s, hdr)
    print(f"capture #{hdr.id}: {hdr.n} samples at {fs / 1e6:.4f} MS/s, trigger at "
          f"sample {hdr.trig_index}{' (auto)' if hdr.forced else ''}, VDDA {hdr.vdda_mv:.0f} mV")
    if args.output:
        with open(args.output, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["t_s", "code", "volts"])
            for i, (c, v) in enumerate(zip(s, volts)):
                w.writerow([f"{(i - hdr.trig_index) / fs:.9f}", c, f"{v:.5f}"])
        print(f"saved {args.output}")
    if args.plot:
        import matplotlib.pyplot as plt
        t = [(i - hdr.trig_index) / fs * 1e3 for i in range(hdr.n)]
        plt.plot(t, volts, lw=0.8)
        plt.axvline(0, color="grey", ls=":")
        plt.xlabel("time from trigger [ms]")
        plt.ylabel("input [V]")
        plt.title(f"{fs / 1e6:.3f} MS/s, {hdr.n} samples")
        plt.grid(alpha=0.3)
        plt.show()


def cmd_cal(dev, args):
    if args.action == "get":
        f = dev.request(sp.CAL_GET, bytes([args.range]), reply=sp.CAL)
        rng, gain, off = struct.unpack("<Bff", f.payload)
        print(f"range {rng}: gain {gain:.6f}  offset {off:.3f} mV")
    elif args.action == "set":
        dev.command(sp.CAL_SET, struct.pack("<Bff", args.range, args.gain, args.offset))
    else:
        dev.command(sp.CAL_SAVE)
        print("saved to flash")


def cmd_selftest(dev, args):
    """Automated end-to-end validation with the PA4 (DAC) -> PA0 (ADC) jumper."""
    info = dev.info()
    lines = ["# Loopback self-test", "",
             f"- date: {dt.datetime.now().isoformat(timespec='seconds')}",
             f"- firmware {info.fw}, board UID {info.uid}",
             "- setup: NUCLEO-L432KC, jumper PA4 (DAC1_OUT1) -> PA0 (ADC1_IN5), "
             "1 nF C0G from PA0 to GND", ""]
    failures = 0

    # 1. protocol robustness
    rtts = []
    for _ in range(args.pings):
        payload = os.urandom(random.randrange(0, 64))
        t0 = time.perf_counter()
        f = dev.request(sp.PING, payload, reply=sp.PONG)
        rtts.append((time.perf_counter() - t0) * 1e3)
        if f.payload != payload:
            failures += 1
    before = dev.status()["rx_crc_errors"]
    bad = bytearray(sp.encode(sp.GET_STATUS, 0))
    bad[-1] ^= 0xFF
    dev.ser.write(bytes(bad))
    time.sleep(0.05)
    after = dev.status()["rx_crc_errors"]
    crc_ok = after == before + 1
    failures += not crc_ok
    lines += ["## Protocol", "",
              f"- {args.pings} PING round trips with random payloads: "
              f"median {statistics.median(rtts):.2f} ms, max {max(rtts):.2f} ms",
              f"- corrupted frame rejected and counted: {'PASS' if crc_ok else 'FAIL'}", ""]

    # 2. generator loopback: frequency from the host's own estimator
    lines += ["## Generator loopback", "",
              "| wave | nominal | time/div | Fs | measured (host) | error | "
              "firmware f | Vpp | result |",
              "|---|---|---|---|---|---|---|---|---|"]
    dev.set_run(1)
    for wave in ("sine", "square", "triangle"):
        for fi, fnom in enumerate(sp.GEN_FREQS):
            # choose a timebase that shows ~4 periods
            want = 4.0 / fnom / 8.0 * 1e9
            tb = min(info.timebases, key=lambda t: abs(t.ns_per_div - want))
            dev.set_gen(sp.WAVES.index(wave), fi)
            dev.set_timebase(tb.index)
            dev.set_trigger(2048, 40, 0, 0, 50)
            time.sleep(0.1)
            hdr, s = dev.capture()
            f_host = estimate_frequency(s, hdr.sample_rate)
            m = dev.meas()
            vpp = (max(s) - min(s)) * hdr.vdda_mv / 4095
            err = (f_host / fnom - 1) * 100 if f_host else float("nan")
            ok = f_host is not None and abs(err) < args.tol_pct and not hdr.forced
            failures += not ok
            lines.append(f"| {wave} | {fnom} Hz | {tb.label} | {hdr.sample_rate / 1e6:.4f} MS/s | "
                         f"{f_host or float('nan'):.3f} Hz | {err:+.3f} % | "
                         f"{m['freq_hz']:.3f} Hz | {vpp:.0f} mV | {'PASS' if ok else 'FAIL'} |")
    dev.set_gen(0, 0)

    st = dev.status()
    lines += ["", "## Counters after the run", ""]
    lines += [f"- {k}: {st[k]}" for k in ("records", "triggers", "auto_triggers", "search_skips",
                                          "records_discarded", "max_stop_overshoot",
                                          "adc_overruns", "tx_dropped", "rx_crc_errors",
                                          "loop_max_us")]
    lines += ["", f"**{'PASS' if failures == 0 else f'FAIL ({failures})'}**", "",
              "Note: the DAC and ADC share the same 80 MHz clock, so the frequency check "
              "validates the trigger/measurement chain, not the absolute clock accuracy "
              "(see docs/validation-plan.md, test V1)."]
    text = "\n".join(lines) + "\n"
    print(text)
    if args.report:
        os.makedirs(os.path.dirname(args.report) or ".", exist_ok=True)
        with open(args.report, "w") as fh:
            fh.write(text)
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info").set_defaults(fn=cmd_info)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("meas").set_defaults(fn=cmd_meas)
    for name in ("run", "stop", "single"):
        sub.add_parser(name).set_defaults(fn=cmd_run)
    p = sub.add_parser("timebase"); p.add_argument("index", type=int); p.set_defaults(fn=cmd_timebase)
    p = sub.add_parser("trigger")
    p.add_argument("--level", type=float, help="volts at the ADC pin")
    p.add_argument("--hyst", type=int, default=40, help="ADC codes")
    p.add_argument("--edge", choices=sp.EDGES, default="rising")
    p.add_argument("--mode", choices=["auto", "normal", "single"], default="auto")
    p.add_argument("--pre", type=int, default=50, help="pre-trigger %% (10-90)")
    p.set_defaults(fn=cmd_trigger)
    p = sub.add_parser("gen")
    p.add_argument("wave", choices=sp.WAVES)
    p.add_argument("freq", type=int, nargs="?", default=1000, choices=sp.GEN_FREQS)
    p.set_defaults(fn=cmd_gen)
    p = sub.add_parser("capture")
    p.add_argument("-o", "--output")
    p.add_argument("--plot", action="store_true")
    p.add_argument("--current", action="store_true", help="send the frozen record (after SINGLE/STOP)")
    p.set_defaults(fn=cmd_capture)
    p = sub.add_parser("cal")
    p.add_argument("action", choices=["get", "set", "save"])
    p.add_argument("--range", type=int, default=0)
    p.add_argument("--gain", type=float, default=1.0)
    p.add_argument("--offset", type=float, default=0.0, help="mV")
    p.set_defaults(fn=cmd_cal)
    p = sub.add_parser("selftest")
    p.add_argument("--report")
    p.add_argument("--pings", type=int, default=200)
    p.add_argument("--tol-pct", type=float, default=0.5)
    p.set_defaults(fn=cmd_selftest)
    args = ap.parse_args()

    dev = Device(args.port or find_port())
    try:
        rc = args.fn(dev, args)
    finally:
        dev.close()
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
