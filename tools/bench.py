#!/usr/bin/env python3
"""Automated bench measurements for the validation plan.

Each subcommand measures one thing and appends the result to
docs/validation/results.json; `report` renders them as a markdown table.

    bench.py clock --start          # begin the sample-rate measurement
    ... wait several hours ...
    bench.py clock --stop           # compute ppm error
    bench.py noise                  # input-referred noise (short A0 to GND)
    bench.py throughput -n 100      # capture rate + CRC integrity
    bench.py soak --minutes 60      # sustained acquisition, error counters
    bench.py report                 # write docs/validation/results.md

Nothing here invents numbers: every value comes from a device counter or from
samples the device sent, and the conditions are stored next to the result.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import statistics
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import scope  # noqa: E402
import scope_proto as sp  # noqa: E402

RESULTS_DIR = os.path.join(os.path.dirname(HERE), "docs", "validation")
RESULTS_JSON = os.path.join(RESULTS_DIR, "results.json")


# ----------------------------------------------------------------- storage --

def load_results() -> dict:
    try:
        with open(RESULTS_JSON) as fh:
            return json.load(fh)
    except FileNotFoundError:
        return {}


def save_result(key: str, value: dict) -> None:
    os.makedirs(RESULTS_DIR, exist_ok=True)
    data = load_results()
    value["measured_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    data[key] = value
    with open(RESULTS_JSON, "w") as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
    print(f"\nsaved to {os.path.relpath(RESULTS_JSON)} under '{key}'")


def device(args) -> scope.Device:
    return scope.Device(args.port or scope.find_port())


# ------------------------------------------------------- M1: clock accuracy --

SAMPLES_PER_DMA_EVENT = 8192     # half-transfer and full-transfer interrupts


def hold_sampling_free_running(dev) -> None:
    """NORMAL mode with an unreachable level: the timer never stops, so the
    DMA event count stays proportional to elapsed time."""
    dev.set_gen(0, 0)
    dev.set_timebase(0)
    dev.set_trigger(4095, 40, 0, sp.TRIG_MODES.index("NORMAL"), 50)
    dev.set_run(1)


def cmd_clock(dev, args):
    state_file = os.path.join(RESULTS_DIR, "clock_state.json")
    info = dev.info()
    tb = info.timebases[0]
    nominal = tb.sample_rate

    if args.stop:
        try:
            with open(state_file) as fh:
                start = json.load(fh)
        except FileNotFoundError:
            sys.exit("no measurement in progress - run 'bench.py clock --start' first")
        st = dev.status()
        t1 = time.time()
        dt = t1 - start["host_time"]
        d_events = st["dma_events"] - start["dma_events"]

        # Validity checks: a reset, a sleeping host or a changed timebase all
        # silently invalidate the result, so refuse rather than report garbage.
        if d_events <= 0:
            sys.exit("no DMA events counted - the device reset or stopped sampling")
        if st["uptime_ms"] < start["uptime_ms"]:
            sys.exit("device uptime went backwards: it reset during the window")
        device_elapsed = (st["uptime_ms"] - start["uptime_ms"]) / 1000.0
        if abs(device_elapsed - dt) > 0.02 * dt:
            sys.exit(f"device was powered for {device_elapsed/3600:.2f} h but "
                     f"{dt/3600:.2f} h passed on the host - it lost power "
                     f"(check that USB stays powered while the Mac sleeps)")
        if st["timebase"] != 0:
            sys.exit(f"timebase is {st['timebase']}, not 0 - settings changed mid-window")
        if st["mode"] != "NORMAL" or st["triggers"] != start.get("triggers", st["triggers"]):
            print("WARNING: trigger mode changed or edges were found - sampling may "
                  "have paused, which biases the result low")
        fs = d_events * SAMPLES_PER_DMA_EVENT / dt
        ppm = (fs / nominal - 1.0) * 1e6
        # a 1-second uncertainty in each timestamp bounds the measurement
        resolution_ppm = 2e6 / dt
        print(f"window          : {dt / 3600:.3f} h")
        print(f"DMA events      : {d_events}")
        print(f"samples         : {d_events * SAMPLES_PER_DMA_EVENT}")
        print(f"nominal rate    : {nominal:.4f} S/s")
        print(f"measured rate   : {fs:.4f} S/s")
        print(f"error           : {ppm:+.2f} ppm  (+-{resolution_ppm:.2f} ppm method floor)")
        if st["adc_overruns"] or st["records_discarded"]:
            print("WARNING: overruns or discarded records during the window")
        save_result("clock_accuracy", {
            "window_h": dt / 3600, "dma_events": d_events,
            "nominal_sps": nominal, "measured_sps": fs,
            "error_ppm": ppm, "method_floor_ppm": resolution_ppm,
            "adc_overruns": st["adc_overruns"],
            "records_discarded": st["records_discarded"],
            "uptime_ms": st["uptime_ms"],
            "note": "host clock reference; sampling held free-running in NORMAL mode",
        })
        os.remove(state_file)
        return

    hold_sampling_free_running(dev)
    time.sleep(0.5)
    st = dev.status()
    os.makedirs(RESULTS_DIR, exist_ok=True)
    with open(state_file, "w") as fh:
        json.dump({"host_time": time.time(), "dma_events": st["dma_events"],
                   "uptime_ms": st["uptime_ms"], "triggers": st["triggers"]}, fh)
    print("sample clock measurement started.")
    print("  - the device is now sampling continuously at 5.000 MS/s")
    print("  - do NOT reset or re-flash it")
    print(f"  - wait at least {args.min_hours} h, then run:  bench.py clock --stop")
    print("  - on macOS, keep the machine awake so USB stays powered:  caffeinate -s")


# ---------------------------------------------------------- M3: noise floor --

def code_stats(codes, vdda_mv):
    lsb = vdda_mv / 4095.0
    mean = statistics.fmean(codes)
    rms = statistics.pstdev(codes)
    return {
        "n": len(codes),
        "mean_code": mean,
        "rms_codes": rms,
        "rms_mv": rms * lsb,
        "pp_codes": max(codes) - min(codes),
        "pp_mv": (max(codes) - min(codes)) * lsb,
        "mean_mv": mean * lsb,
        # samples stuck at a rail: noise below 0 V (or above VDDA) cannot be
        # represented, so a clipped record under-reports the noise
        "clipped_pct": 100.0 * sum(1 for c in codes if c <= 0 or c >= 4095) / len(codes),
    }


def cmd_noise(dev, args):
    print(f"condition '{args.label}': {args.condition}")
    dev.set_gen(0, 0)
    rows = []
    for idx in args.timebases:
        dev.set_timebase(idx)
        dev.set_trigger(2048, 40, 0, sp.TRIG_MODES.index("AUTO"), 50)
        time.sleep(0.2)
        hdr, codes = dev.capture()
        s = code_stats(codes, hdr.vdda_mv)
        s["timebase_index"] = idx
        s["sample_rate"] = hdr.sample_rate
        rows.append(s)
        print(f"  tb {idx:2d}  {hdr.sample_rate/1e6:7.4f} MS/s  n={s['n']:5d}  "
              f"mean={s['mean_code']:7.2f} codes  noise={s['rms_codes']:5.2f} codes "
              f"= {s['rms_mv']:5.3f} mV RMS   p-p={s['pp_codes']} codes"
              + (f"   CLIPPED {s['clipped_pct']:.0f} %" if s["clipped_pct"] > 1 else ""))
    best = min(rows, key=lambda r: r["rms_mv"])
    worst = max(rows, key=lambda r: r["rms_mv"])
    print(f"\nbest  {best['rms_mv']:.3f} mV RMS at {best['sample_rate']/1e6:.4f} MS/s")
    print(f"worst {worst['rms_mv']:.3f} mV RMS at {worst['sample_rate']/1e6:.4f} MS/s")
    if any(r["clipped_pct"] > 1 for r in rows):
        print("\nWARNING: samples sit at code 0/4095, so the noise above is an "
              "underestimate.\nUse the mid-scale divider condition for the headline number.")
    save_result(f"noise_floor_{args.label}",
                {"rows": rows, "condition": args.condition, "label": args.label})


# ------------------------------------------------------- M4: DC accuracy ----

def cmd_dc(dev, args):
    """Interactive: you set a voltage, type what the DMM reads, repeat."""
    print("Feed A0 from a divider or pot. Type the DMM reading in volts for each")
    print("point, blank line to finish.\n")
    pts = []
    while True:
        try:
            raw = input(f"point {len(pts) + 1} - DMM reading [V]: ").strip()
        except EOFError:
            break
        if not raw:
            break
        try:
            v_dmm = float(raw)
        except ValueError:
            print("  not a number")
            continue
        m = dev.meas()
        if not m["valid"]:
            print("  no record yet, try again")
            continue
        v_dev = m["vavg_mv"] / 1000.0
        err_mv = (v_dev - v_dmm) * 1000.0
        pts.append({"dmm_v": v_dmm, "device_v": v_dev, "error_mv": err_mv})
        print(f"  device {v_dev:.4f} V   error {err_mv:+.2f} mV "
              f"({(v_dev / v_dmm - 1) * 100 if v_dmm else 0:+.3f} %)")

    if len(pts) < 2:
        sys.exit("need at least two points")

    # least squares fit  dmm = gain * (device - offset)
    xs = [p["device_v"] for p in pts]
    ys = [p["dmm_v"] for p in pts]
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    gain = sxy / sxx if sxx else 1.0
    offset_v = mx - my / gain if gain else 0.0
    residuals = [abs((x - offset_v) * gain - y) * 1000 for x, y in zip(xs, ys)]

    print(f"\npoints          : {n}")
    print(f"max error       : {max(abs(p['error_mv']) for p in pts):.2f} mV")
    print(f"suggested gain  : {gain:.6f}")
    print(f"suggested offset: {offset_v * 1000:.2f} mV")
    print(f"residual after fit: max {max(residuals):.2f} mV, rms "
          f"{math.sqrt(sum(r * r for r in residuals) / n):.2f} mV")
    print(f"\napply with:\n  scope.py cal set --range 0 --gain {gain:.6f} "
          f"--offset {offset_v * 1000:.2f}\n  scope.py cal save")
    save_result(f"dc_accuracy_{args.label}", {
        "points": pts, "fit_gain": gain, "fit_offset_mv": offset_v * 1000,
        "max_error_mv": max(abs(p["error_mv"]) for p in pts),
        "residual_max_mv": max(residuals), "label": args.label,
    })


# ------------------------------------------------- M2/M5: throughput + soak --

def cmd_throughput(dev, args):
    dev.set_timebase(args.timebase)
    dev.set_gen(sp.WAVES.index("sine"), 1)
    dev.set_trigger(2048, 40, 0, sp.TRIG_MODES.index("AUTO"), 50)
    time.sleep(0.2)
    before = dev.status()
    t0 = time.time()
    n_samples = 0
    for i in range(args.n):
        hdr, codes = dev.capture()      # verifies chunk + record CRC
        n_samples += len(codes)
        if (i + 1) % 10 == 0:
            print(f"  {i + 1}/{args.n} captures", end="\r", flush=True)
    dt = time.time() - t0
    after = dev.status()
    kbps = n_samples * 2 / dt / 1000
    print(f"\n{args.n} captures, {n_samples} samples, {dt:.2f} s")
    print(f"  {dt / args.n * 1000:.1f} ms per record of {hdr.n} samples")
    print(f"  {kbps:.1f} kB/s of sample payload")
    for k in ("rx_crc_errors", "rx_len_errors", "tx_dropped"):
        print(f"  {k}: {after[k] - before[k]} during the run")
    save_result("capture_throughput", {
        "captures": args.n, "record_len": hdr.n, "seconds": dt,
        "ms_per_record": dt / args.n * 1000, "kB_per_s": kbps,
        "crc_failures": 0,
        "rx_crc_errors": after["rx_crc_errors"] - before["rx_crc_errors"],
        "tx_dropped": after["tx_dropped"] - before["tx_dropped"],
    })


def cmd_soak(dev, args):
    dev.set_timebase(args.timebase)
    dev.set_gen(sp.WAVES.index("square"), 1)
    dev.set_trigger(2048, 40, 0, sp.TRIG_MODES.index("AUTO"), 50)
    dev.set_run(1)
    start = dev.status()
    t0 = time.time()
    print(f"soaking at timebase {args.timebase} for {args.minutes} min "
          f"(Ctrl-C stops early and still records)\n")
    last = start
    try:
        while time.time() - t0 < args.minutes * 60:
            time.sleep(min(60, args.minutes * 60 - (time.time() - t0)))
            st = dev.status()
            fps = (st["lcd_frames"] - last["lcd_frames"]) / 60.0
            print(f"  t+{(time.time() - t0) / 60:5.1f} min  records={st['records']:>9} "
                  f"overruns={st['adc_overruns']} discarded={st['records_discarded']} "
                  f"skips={st['search_skips']} loop_max={st['loop_max_us']} us "
                  f"fps={fps:.1f}")
            last = st
    except KeyboardInterrupt:
        print("\ninterrupted")
    end = dev.status()
    dt = time.time() - t0
    res = {
        "minutes": dt / 60,
        "timebase_index": args.timebase,
        "records": end["records"] - start["records"],
        "triggers": end["triggers"] - start["triggers"],
        "auto_triggers": end["auto_triggers"] - start["auto_triggers"],
        "adc_overruns": end["adc_overruns"] - start["adc_overruns"],
        "records_discarded": end["records_discarded"] - start["records_discarded"],
        "search_skips": end["search_skips"] - start["search_skips"],
        "max_stop_overshoot": end["max_stop_overshoot"],
        "loop_max_us": end["loop_max_us"],
        "display_fps": (end["lcd_frames"] - start["lcd_frames"]) / dt,
        "uptime_ms": end["uptime_ms"],
        "watchdog_reset": end["uptime_ms"] < start["uptime_ms"],
    }
    print(f"\n{res['records']} records in {res['minutes']:.1f} min")
    print(f"  overruns {res['adc_overruns']}, discarded {res['records_discarded']}, "
          f"skips {res['search_skips']}")
    print(f"  worst stop overshoot {res['max_stop_overshoot']} samples "
          f"(guard band is 1024)")
    print(f"  loop_max {res['loop_max_us']} us, display {res['display_fps']:.1f} FPS")
    print(f"  watchdog reset: {'YES - investigate' if res['watchdog_reset'] else 'no'}")
    save_result(f"soak_tb{args.timebase}", res)


# ------------------------------------------------------------------ report --

def cmd_report(dev, args):
    r = load_results()
    if not r:
        sys.exit("no results yet")
    out = ["# Measured results", "",
           "Produced by `tools/bench.py`. Raw values in `results.json`.", ""]

    c = r.get("clock_accuracy")
    if c:
        out += ["## Sample-rate accuracy", "",
                f"- window: {c['window_h']:.2f} h, host-clock reference",
                f"- nominal {c['nominal_sps']/1e6:.4f} MS/s, measured "
                f"{c['measured_sps']/1e6:.6f} MS/s",
                f"- **error {c['error_ppm']:+.2f} ppm** (method floor "
                f"±{c['method_floor_ppm']:.2f} ppm)",
                f"- overruns {c['adc_overruns']}, discarded {c['records_discarded']}",
                f"- measured {c['measured_at']}", ""]

    for key in sorted(k for k in r if k.startswith("noise_floor")):
        n = r[key]
        out += [f"## Input-referred noise ({n.get('label', 'gnd')})", "",
                f"Condition: {n.get('condition', 'input shorted to GND')}", "",
                "| timebase | sample rate | mean code | RMS codes | RMS mV | p-p codes | clipped |",
                "|---|---|---|---|---|---|---|"]
        for row in n["rows"]:
            out.append(f"| {row['timebase_index']} | {row['sample_rate']/1e6:.4f} MS/s "
                       f"| {row['mean_code']:.1f} | {row['rms_codes']:.2f} | "
                       f"{row['rms_mv']:.3f} | {row['pp_codes']} | "
                       f"{row.get('clipped_pct', 0):.0f} % |")
        out.append("")

    for key in sorted(k for k in r if k.startswith("dc_accuracy")):
        d = r[key]
        out += [f"## DC accuracy ({d['label']})", "",
                f"- {len(d['points'])} points against a multimeter",
                f"- max error **{d['max_error_mv']:.2f} mV**",
                f"- best-fit gain {d['fit_gain']:.6f}, offset "
                f"{d['fit_offset_mv']:.2f} mV, residual max "
                f"{d['residual_max_mv']:.2f} mV", "",
                "| DMM [V] | device [V] | error [mV] |", "|---|---|---|"]
        for p in d["points"]:
            out.append(f"| {p['dmm_v']:.4f} | {p['device_v']:.4f} | {p['error_mv']:+.2f} |")
        out.append("")

    m5 = r.get("timing_m5")
    if m5:
        out += ["## Firmware timing (GPIO probes on a bench oscilloscope)", "",
                f"- instrument: {m5['instrument']}",
                f"- probes: {m5['probes']}",
                f"- measured {m5['measured_at']}", "",
                "| quantity | result | derived from |", "|---|---|---|"]
        for row in m5["rows"]:
            out.append(f"| {row['quantity']} | **{row['result']}** | {row['from']} |")
        out += ["", m5.get("note", ""), ""]

    t = r.get("capture_throughput")
    if t:
        out += ["## Capture transfer", "",
                f"- {t['captures']} records of {t['record_len']} samples",
                f"- {t['ms_per_record']:.0f} ms each, {t['kB_per_s']:.1f} kB/s",
                f"- CRC failures **{t['crc_failures']}**, link CRC errors "
                f"{t['rx_crc_errors']}, dropped packets {t['tx_dropped']}", ""]

    for key in sorted(k for k in r if k.startswith("soak")):
        s = r[key]
        out += [f"## Sustained acquisition (timebase {s['timebase_index']})", "",
                f"- {s['minutes']:.1f} min, {s['records']} records",
                f"- ADC overruns **{s['adc_overruns']}**, discarded records "
                f"**{s['records_discarded']}**, search skips {s['search_skips']}",
                f"- worst stop overshoot {s['max_stop_overshoot']} samples of a "
                f"1024-sample guard band",
                f"- worst main-loop pass {s['loop_max_us']} us "
                f"(includes record processing, during which sampling is stopped)",
                f"- display {s['display_fps']:.1f} FPS",
                f"- watchdog reset: {'yes' if s['watchdog_reset'] else 'no'}", ""]

    path = os.path.join(RESULTS_DIR, "results.md")
    os.makedirs(RESULTS_DIR, exist_ok=True)
    with open(path, "w") as fh:
        fh.write("\n".join(out))
    print("\n".join(out))
    print(f"\nwritten to {os.path.relpath(path)}")


# -------------------------------------------------------------------- main --

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("clock", help="sample-rate accuracy vs the host clock")
    p.add_argument("--start", action="store_true")
    p.add_argument("--stop", action="store_true")
    p.add_argument("--min-hours", type=float, default=4.0)
    p.set_defaults(fn=cmd_clock)

    p = sub.add_parser("noise", help="input-referred noise (short A0 to GND first)")
    p.add_argument("--timebases", type=int, nargs="+", default=[0, 5, 7, 11, 13])
    p.add_argument("--label", default="gnd",
                   help="name of this condition, e.g. gnd / midscale / backlight_off")
    p.add_argument("--condition", default="A0 shorted to GND, display active",
                   help="free-text description stored with the result")
    p.set_defaults(fn=cmd_noise)

    p = sub.add_parser("dc", help="DC accuracy against a multimeter")
    p.add_argument("--label", default="uncalibrated", help="e.g. before / after")
    p.set_defaults(fn=cmd_dc)

    p = sub.add_parser("throughput", help="capture rate and CRC integrity")
    p.add_argument("-n", type=int, default=100)
    p.add_argument("--timebase", type=int, default=6)
    p.set_defaults(fn=cmd_throughput)

    p = sub.add_parser("soak", help="sustained acquisition and error counters")
    p.add_argument("--minutes", type=float, default=60)
    p.add_argument("--timebase", type=int, default=0)
    p.set_defaults(fn=cmd_soak)

    p = sub.add_parser("report", help="render results.md")
    p.set_defaults(fn=cmd_report)

    args = ap.parse_args()
    if args.cmd == "report":
        return cmd_report(None, args)
    if args.cmd == "clock" and not (args.start or args.stop):
        sys.exit("use --start or --stop")

    dev = device(args)
    try:
        args.fn(dev, args)
    finally:
        dev.close()


if __name__ == "__main__":
    main()
