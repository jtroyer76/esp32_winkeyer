#!/usr/bin/env python3
"""Measure keyer element timing from the firmware's edge log.

Usage:
  python tools/check_timing.py COM7 --wpm 20 --text "PARIS PARIS PARIS"
  python tools/check_timing.py --file edges.log --wpm 20

Opens the console, enables the edge log, sends the text, collects the
"E <micros> <K|P> <0|1>" lines until the engine goes idle, then reports the
measured dit/dah/gap lengths against the nominal values for the speed.
"""
import argparse
import re
import statistics
import sys
import time


EDGE_RE = re.compile(r"E (\d+) ([KP]) ([01])\s*$")


def parse_edges(lines):
    # The console echoes sent characters without a newline, so an edge line
    # may carry a leading character ("PE 123 K 0"). Match from the right.
    edges = []
    for ln in lines:
        m = EDGE_RE.search(ln)
        if m:
            edges.append((int(m.group(1)), m.group(2), int(m.group(3))))
    return edges


def capture(port, wpm, text, weight, ratio, comp, timeout):
    import serial  # pyserial

    with serial.Serial(port, 115200, timeout=0.2) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        for cmd in (f":s {wpm}", f":w {weight}", f":r {ratio}", f":c {comp}", ":p off", ":e on"):
            ser.write((cmd + "\n").encode())
            time.sleep(0.05)
        ser.reset_input_buffer()
        ser.write((text + "\n").encode())
        lines = []
        last = time.time()
        deadline = time.time() + timeout
        while time.time() < deadline:
            raw = ser.readline()
            if raw:
                lines.append(raw.decode(errors="replace"))
                last = time.time()
            elif lines and time.time() - last > 1.0:
                break
        ser.write(b":e off\n")
    return lines


def analyse(edges, wpm, weight, ratio, comp):
    key = [(t, lvl) for t, line, lvl in edges if line == "K"]
    if len(key) < 2:
        print("no key edges captured")
        return 1

    dit = 1_200_000 / wpm
    wadj = (weight - 50) * dit / 50
    comp_us = comp * 1000
    nom_dit = dit + wadj + comp_us
    nom_dah = dit * 3 * ratio / 50 + wadj + comp_us
    nom_gap = dit - wadj - comp_us
    nom_cgap = nom_gap + 2 * dit
    nom_wgap = nom_gap + 6 * dit

    ons, offs = [], []
    for (t0, l0), (t1, l1) in zip(key, key[1:]):
        d = t1 - t0
        (ons if l0 == 1 else offs).append(d)

    # Classify by nearest nominal value.
    def nearest(d, table):
        return min(table, key=lambda kv: abs(d - kv[1]))[0]

    buckets = {"dit": [], "dah": [], "gap": [], "cgap": [], "wgap": []}
    for d in ons:
        buckets[nearest(d, [("dit", nom_dit), ("dah", nom_dah)])].append(d)
    for d in offs:
        buckets[nearest(d, [("gap", nom_gap), ("cgap", nom_cgap), ("wgap", nom_wgap)])].append(d)

    nominal = {"dit": nom_dit, "dah": nom_dah, "gap": nom_gap, "cgap": nom_cgap, "wgap": nom_wgap}
    print(f"{wpm} WPM, weight {weight}, ratio {ratio}, comp {comp} ms  (dit = {dit:.0f} us)")
    print(f"{'element':8} {'n':>4} {'nominal':>9} {'mean':>9} {'min':>9} {'max':>9} {'stdev':>7} {'err%':>6}")
    worst = 0.0
    for name, vals in buckets.items():
        if not vals:
            continue
        mean = statistics.fmean(vals)
        sd = statistics.pstdev(vals) if len(vals) > 1 else 0.0
        err = (mean - nominal[name]) / nominal[name] * 100
        worst = max(worst, abs(err), max(abs(v - nominal[name]) for v in vals) / nominal[name] * 100)
        print(f"{name:8} {len(vals):4d} {nominal[name]:9.0f} {mean:9.1f} {min(vals):9d} {max(vals):9d} {sd:7.1f} {err:6.2f}")
    if buckets["dit"] and buckets["dah"]:
        print(f"dah/dit ratio: {statistics.fmean(buckets['dah']) / statistics.fmean(buckets['dit']):.3f} "
              f"(nominal {nom_dah / nom_dit:.3f})")
    print(f"worst single-element error: {worst:.2f}%")
    return 0 if worst < 1.0 else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", help="serial port (omit with --file)")
    ap.add_argument("--file", help="read edge lines from a file instead of the port")
    ap.add_argument("--wpm", type=int, default=20)
    ap.add_argument("--weight", type=int, default=50)
    ap.add_argument("--ratio", type=int, default=50)
    ap.add_argument("--comp", type=int, default=0)
    ap.add_argument("--text", default="PARIS PARIS PARIS PARIS PARIS")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--save", help="write captured lines to this file")
    a = ap.parse_args()

    if a.file:
        with open(a.file) as f:
            lines = f.readlines()
    elif a.port:
        lines = capture(a.port, a.wpm, a.text, a.weight, a.ratio, a.comp, a.timeout)
        if a.save:
            with open(a.save, "w") as f:
                f.writelines(lines)
    else:
        ap.error("give a serial port or --file")

    return analyse(parse_edges(lines), a.wpm, a.weight, a.ratio, a.comp)


if __name__ == "__main__":
    sys.exit(main())
