#!/usr/bin/env python3
"""Exercise the WinKeyer protocol the way a logger does.

  python tools/wk_test.py COM14                 # open, probe, send a test message
  python tools/wk_test.py COM14 --text "CQ TEST DE N0CALL" --wpm 25
  python tools/wk_test.py COM14 --tune 2        # key down for 2 s

Sequence (mirrors what N1MM+ does on connect):
  Admin:Host Open  -> expect the firmware revision byte
  Admin:Echo Test  -> expect the byte back
  Admin:Set WK2 mode
  Set Mode (serial echo + paddle echo on)
  Set Speed, Request Status, Get Pot
  send text, collect echo and status bytes until idle
  Admin:Host Close
"""
import argparse
import sys
import time

import serial


def rx_all(ser, quiet=0.3, limit=30.0):
    """Read until `quiet` seconds pass with no data, or `limit` seconds total."""
    out = bytearray()
    t0 = time.time()
    last = t0
    while time.time() - t0 < limit:
        b = ser.read(64)
        if b:
            out += b
            last = time.time()
        elif time.time() - last > quiet:
            break
    return bytes(out)


def describe(b):
    if b & 0xC0 == 0xC0:
        flags = []
        if b & 0x10: flags.append("WAIT")
        if b & 0x08: flags.append("KEYDOWN/PB")
        if b & 0x04: flags.append("BUSY")
        if b & 0x02: flags.append("BREAKIN")
        if b & 0x01: flags.append("XOFF")
        return f"STATUS {b:#04x} [{' '.join(flags) or 'idle'}]"
    if b & 0xC0 == 0x80:
        return f"POT {b & 0x3F}"
    if 0x20 <= b < 0x7F:
        return f"ECHO {chr(b)!r}"
    return f"BYTE {b:#04x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--wpm", type=int, default=20)
    ap.add_argument("--text", default="VVV DE N0CALL K")
    ap.add_argument("--tune", type=float, default=0, help="key down for N seconds instead of sending text")
    ap.add_argument("--ptt", action="store_true", help="enable PTT with 20 ms lead-in")
    a = ap.parse_args()

    # 1200 baud, 8N2 is what WinKeyer specifies; on USB CDC it is nominal.
    with serial.Serial(a.port, 1200, bytesize=8, parity="N", stopbits=2, timeout=0.1) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()

        ser.write(bytes([0x00, 0x02]))                      # Host Open
        r = rx_all(ser, quiet=0.5)
        if not r:
            print("no response to Host Open: not a WinKeyer, or wrong port")
            return 1
        print(f"Host Open -> revision {r[0]} (WinKeyer {r[0] // 10}.{r[0] % 10} class)", "" if len(r) == 1 else f"extra: {r[1:].hex()}")

        ser.write(bytes([0x00, 0x04, ord('K')]))            # Echo test
        r = rx_all(ser)
        print("Echo test ->", "OK" if r == b"K" else f"FAIL {r!r}")

        ser.write(bytes([0x00, 0x0B]))                      # WK2 mode
        ser.write(bytes([0x0E, 0x44]))                      # mode: paddle echo + serial echo, iambic B
        ser.write(bytes([0x02, a.wpm]))                     # speed
        if a.ptt:
            ser.write(bytes([0x09, 0x03]))                  # PINCFG: PTT + sidetone enabled, key port 1
            ser.write(bytes([0x04, 2, 5]))                  # lead 20 ms, tail 50 ms + 3 dits
        else:
            ser.write(bytes([0x09, 0x02]))                  # sidetone only, no PTT
        ser.write(bytes([0x15]))                            # request status
        ser.write(bytes([0x07]))                            # get pot
        for b in rx_all(ser):
            print("  ", describe(b))

        if a.tune > 0:
            print(f"Tune on for {a.tune} s")
            ser.write(bytes([0x0B, 0x01]))
            t0 = time.time()
            while time.time() - t0 < a.tune:
                for b in ser.read(16):
                    print("  ", describe(b))
            ser.write(bytes([0x0B, 0x00]))
        else:
            print(f"Sending {a.text!r} at {a.wpm} WPM")
            # Exactly what N1MM+ sends before every message (traced 2026-11-10):
            # clear, pointer reset, pointer append at 0, buffered speed.
            ser.write(bytes([0x0A, 0x16, 0x00, 0x16, 0x02, 0x00, 0x1C, a.wpm]))
            ser.write(a.text.encode("ascii"))
            t0 = time.time()
            echoed = ""
            saw_idle = False
            while time.time() - t0 < 60 and not saw_idle:
                for b in ser.read(16):
                    print("  ", describe(b))
                    if 0x20 <= b < 0x7F:
                        echoed += chr(b)
                    # BUSY cleared after we saw it busy at least once
                    if b & 0xC0 == 0xC0 and not (b & 0x04) and echoed:
                        saw_idle = True
            print(f"Echoed back: {echoed!r}")
            print("Echo matches text:", "yes" if echoed == a.text else "NO")

        ser.write(bytes([0x00, 0x03]))                      # Host Close
        time.sleep(0.2)
        tail = ser.read(64)
        if tail:
            print("after close:", " ".join(describe(b) for b in tail))
    return 0


if __name__ == "__main__":
    sys.exit(main())
