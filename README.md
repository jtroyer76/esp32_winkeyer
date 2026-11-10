# esp32_winkeyer

WinKeyer-compatible CW keyer firmware for ESP32, with every element edge
timed by a hardware-timer interrupt rather than by code running in `loop()`.
Loggers (N1MM+, Not1MM, fldigi, ...) talk to it as a K1EL WinKeyer over USB
serial; it keys the radio through an optocoupler.

## Why another keyer

Keying a radio from a PC serial line (DTR/RTS) puts Morse timing at the
mercy of the OS and the USB-serial chip. A WinKeyer moves timing into
hardware: the PC sends characters, the keyer makes the dits.

Most open keyer firmware (the K3NG keyer being the best known) grew up on
8-bit AVRs and times elements with `delay()` or with `millis()` checks in
the main loop, so an edge can land late by however long the rest of the
loop took: serial parsing, display updates, debouncing. The K3NG NextGen
effort is converting that code to a non-blocking state machine after the
fact. This project starts from the other end: a 50 µs timer ISR owns the
key and PTT pins and every countdown; everything else (USB, protocol,
console, sidetone) runs outside it and only exchanges data through
lock-free rings. There is no `delay()` anywhere, by rule.

## Targets

| env           | board                | notes                                                   |
|---------------|----------------------|---------------------------------------------------------|
| `atoms3_lite` | M5Stack AtomS3 Lite  | primary. ESP32-S3, native USB CDC, no reset on port open |
| `xiao_s3`     | Seeed XIAO ESP32-S3  | same chip class; builds, not yet tested on hardware     |
| `atom_lite`   | M5Stack Atom Lite    | classic ESP32; USB-UART bridge resets the chip when the port opens; builds, not yet tested |

Pin maps are in `include/pins.h`, verified against the arduino-esp32
variant headers and M5's documentation. Any other ESP32-S3 or ESP32 board
needs only a pin block there and an env in `platformio.ini`.

## Hardware (per radio)

```
GPIO KEY  ──[470Ω]──► PC817 pin 1 (anode)   PC817 pin 4 ──► KEY jack tip
GPIO PTT  ──[470Ω]──► PC817 pin 1 (anode)   PC817 pin 4 ──► PTT jack tip
             pin 2 (cathode) ──► GND         PC817 pin 3 ──► jack sleeve
paddle tip  ──► DIT GPIO ──[10k]── 3V3, 100nF to GND
paddle ring ──► DAH GPIO ──[10k]── 3V3, 100nF to GND
paddle sleeve ──► GND
piezo ──[100Ω]── SIDETONE GPIO (optional)
```

Radio key input set to straight-key mode. Key and PTT are isolated from
the computer ground by the optocouplers. Internal pull-ups are enough on
the bench; the external 10 k / 100 nF is for RF-rich environments.

## Status

Done and measured on an AtomS3 Lite:

1. **Engine**: ISR state machine, 50 µs tick, weighting, dit/dah ratio,
   keying compensation, first extension, Farnsworth, contest spacing, PTT
   lead/tail/hang, sidetone, edge log for timing verification.
2. **Paddle**: sampled every tick, 1 ms debounce, iambic A/B, Ultimatic,
   bug, dit/dah memory with switchpoint, break-in with FIFO flush (paddle
   has priority, per the spec), paddle echo, software paddle, watchdog.
3. **WinKeyer protocol**: Admin open/close/echo and WK1/2/3 mode select,
   all immediate commands (speed, weight, PTT timing, pot range, pause,
   clear, key immediate, PINCFG, mode register, load defaults, extensions,
   compensation, switchpoint, status, ratio), buffered commands (PTT,
   timed key, wait, merge, speed change, HSCW, cancel), status and pot
   bytes, echo-back, X2MODE, sidetone frequency, Vcc readback.

Not yet: standalone mode (button, stored messages, NVS settings), pointer
commands (0x16), X1MODE, EEPROM dump/load with real contents. See
`docs/todo.md`.

## Using it with a logger

Plug in, select the COM port, choose "WinKey" (N1MM+: Configure Ports ->
the port -> CW/Other -> Set -> WinKey). The keyer answers Host Open with
revision 31. Speed, PTT timing, mode and echo all come from the logger.
Verified with N1MM+ 2026-11-10: F1 macros key with exact element timing.

## Test console

The console is on UART1 (TX = PIN_DEBUG_TX, 115200 8N1) in normal builds;
the `atoms3_lite_console` env puts it on USB instead (WinKeyer then on
UART1) for `tools/check_timing.py`. Type:

```
:s 20            speed 20 WPM
:e on            print every key/PTT edge as "E <micros> <K|P> <0|1>"
PARIS PARIS      send text
:?               show parameters
```

Full command list at the top of `src/main.cpp`. The console is useful with
TX only (boards with no spare RX pin): it prints `# host open` / `# host
closed`, every key/PTT edge, and a hex trace of host bytes (`< xx` in,
`> xx` out), which is how to see what a logger actually sends.

### Protocol check

```
pip install pyserial
python tools/wk_test.py COM14 --wpm 25 --text "CQ TEST DE N0CALL K"
python tools/wk_test.py COM14 --tune 2
python tools/wk_test.py COM14 --ptt --text "TU 599"
```

Runs the connect sequence a logger uses (Host Open, echo test, mode, speed,
status, pot), sends text, and checks that the echoed characters match.

### Timing check

```
python tools/check_timing.py COM7 --wpm 20 --text "PARIS PARIS PARIS PARIS PARIS"
```

Captures the edge log and reports measured dit, dah, and gap lengths
against nominal, with worst-case error. The engine's design bound is one
tick (50 µs); anything above 1 % is a bug.

Measured: worst element error 0.01 % at 12, 20 and 25 WPM; 0.10 % at
35 WPM with weighting and compensation (one tick of rounding on a 22 ms
gap). Details in `docs/test-log.md`.

## Build

PlatformIO: `pio run -e atoms3_lite -t upload`, then `pio device monitor`.

After flashing an S3 board over its native USB, Windows sometimes refuses to
open the port ("device not functioning") until the board is unplugged and
plugged back in; it may come back under a new COM number.

## License

MIT (see `LICENSE`). The K1EL datasheet in `docs/reference/` and the
WinKeyer protocol itself are K1EL's, under CC BY-NC-SA for personal,
non-commercial use.

## References

- K1EL WinKeyer3 IC datasheet v1.3 (`docs/reference/`). K1EL publishes the
  protocol under Creative Commons BY-NC-SA for personal, non-commercial
  use; this project follows those terms.
- K3NG CW keyer, NextGen development notes
  (`docs/reference/k3ng-nextgen-notes.md`): the blocking-to-non-blocking
  refactor this design avoids by starting non-blocking.
