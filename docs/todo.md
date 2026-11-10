# To do

## Blocked on parts (PC817 optocouplers arrive ~2026-11-10)

- [ ] Breadboard one PC817 on the key GPIO, key a radio, listen on a
      second receiver.
- [ ] Fair A/B on RBN: same frequency, antenna and power, fixed 20 WPM,
      several minutes of CQs with the CH340 DTR keyer, then with the Atom.
      Compare WPM estimates and SNR per skimmer. (The 2026-11-09 DTR run
      scattered 14-19 WPM but speed was changed between 15 and 20 during
      it, so that run is not a clean baseline.)
- [ ] Second PC817 on the PTT GPIO; verify PTT lead-in on a radio with a
      PTT jack.

## Hardware tests with no parts needed

- [ ] Plug the AtomS3 Lite into the logging PC, configure N1MM+ WinKey on
      its COM port, confirm N1MM finds it, F1 runs a macro, ESC aborts,
      speed up/down follows. Record exact N1MM menu path in README.
- [ ] Paddle feel: iambic B default, then A, Ultimatic, bug. Note anything
      that feels wrong.
- [ ] Edge-log test of squeeze behaviour (needs a two-button jig or a
      paddle): alternation, A vs B release, dit insertion during a dah,
      switchpoint effect.
- [ ] Atom Lite (classic ESP32): flash `atom_lite` env, run `wk_test.py`,
      see whether the bridge's reset on port open breaks Host Open.
- [ ] XIAO ESP32-S3: flash `xiao_s3`, run both tools.

## Firmware

- [ ] Standalone mode: command button (G41? verify), stored messages in NVS,
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
- [ ] Add a LICENSE file before the repo goes public (note the WinKeyer
      protocol's own CC BY-NC-SA terms in it).

## Carrier board ("Atomic base" style)

- [ ] Get header geometry from `AtomS3-Lite.stl`
      (github.com/m5stack/M5_Hardware, Products/C124_AtomS3-Lite/Structures).
      Board 24 × 24 mm footprint; 2×4 female header on the Atom, so the
      carrier takes 2×4 male pins.
- [ ] Parts: 2 × PC817, 2 × 470 Ω, 2 × 10 k, 2 × 100 nF, 3 × 3.5 mm jacks
      (paddle TRS, key, PTT), optional piezo + 100 Ω, optional 10 k pot.
- [ ] Decide: protoboard first, then PCB.

## Docs

- [ ] Record N1MM and Not1MM/fldigi configuration steps once verified.
- [ ] Photos of the bench setup.
