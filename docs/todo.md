# To do

## Blocked on parts (PC817 optocouplers arrive ~2026-11-10)

- [x] One PC817 on the key GPIO keyed an mcHF from N1MM, clean by ear
      (2026-11-10, on the carrier board).
- [ ] Fair A/B on RBN: same frequency, antenna and power, fixed 20 WPM,
      several minutes of CQs with the CH340 DTR keyer, then with the Atom.
      Compare WPM estimates and SNR per skimmer. (The 2026-11-09 DTR run
      scattered 14-19 WPM but speed was changed between 15 and 20 during
      it, so that run is not a clean baseline.)
- [ ] Second PC817 on the PTT GPIO; verify PTT lead-in on a radio with a
      PTT jack.

## Hardware tests with no parts needed

- [x] N1MM+ WinKey: connects, F1 sends the macro with correct timing, Esc
      aborts, Page Up/Down speed follows (2026-11-10).
- [ ] Record the exact N1MM menu path in README once re-checked.
- [ ] Optional WiFi/TCP log socket so the console needs no pins (off by
      default; the keyer should stay RF-quiet unless asked).
- [ ] Paddle feel: iambic B default, then A, Ultimatic, bug. Note anything
      that feels wrong.
- [ ] Edge-log test of squeeze behaviour (needs a two-button jig or a
      paddle): alternation, A vs B release, dit insertion during a dah,
      switchpoint effect.
- [ ] Atom Lite (classic ESP32): flash `atom_lite` env, run `wk_test.py`,
      see whether the bridge's reset on port open breaks Host Open.
- [ ] XIAO ESP32-S3: flash `xiao_s3`, run both tools.

## Firmware

- [ ] Standalone mode: command button (G41), stored messages in NVS,
      settings persistence, speed pot option. Datasheet pp. 36-38 for the
      standalone command set if we want paddle-command compatibility.
- [ ] Pointer commands (0x16): implement overwrite/append semantics (needs
      the K1EL application note; a few loggers use them for callsign
      correction).
- [ ] X1MODE letterspace adjustment (WK3 mode).
- [ ] Admin:Dump/Load EEPROM: back with real settings once NVS exists.
- [ ] Ultimatic priority bits (PINCFG 7:6).
- [ ] Status LED (XIAO) / RGB (Atom) showing host open, sending, break-in.
- [ ] Paddle watchdog: verify the 128-element trip and recovery.
- [ ] Consider a second key output (WinKeyer "port 2") for a second radio.

## Carrier board ("Atomic base" style)

Layout in `docs/hardware/carrier-board.md` (stripboard, 2026-11-10).

- [x] Built, key side (2026-11-10): cuts, 1206, wires, opto, interim key
      lead. Paddle wires and jacks still to do.
- [ ] Jacks (Adafruit 1699) on order; meter pin 2 vs 5 for tip before wiring.
- [ ] Case: pull `AtomS3-Lite.stl` (github.com/m5stack/M5_Hardware,
      Products/C124_AtomS3-Lite/Structures) for the opening; model a 24 mm
      tray per the case section of the layout doc.
- [ ] Later: second PC817 + jack for PTT, 10 k / 100 nF on the paddle lines.

## Docs

- [ ] Record N1MM and Not1MM/fldigi configuration steps once verified.
- [ ] Photos of the bench setup.
