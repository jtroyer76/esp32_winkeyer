# esp32_winkeyer

WinKeyer-compatible CW keyer firmware for ESP32, built for AF7QD's Winter
Field Day go-boxes. Loggers (N1MM+, Not1MM, fldigi, ...) talk to it as a
K1EL WinKeyer over USB serial; it keys the radio through an optocoupler.

## Why another keyer

Keying a radio from a PC serial line (DTR/RTS) puts Morse timing at the
mercy of the OS and the USB-serial chip. A WinKeyer moves timing into
hardware: the PC sends characters, the keyer makes the dits. This project
does that on an ESP32 with the timing engine in a hardware-timer ISR, so
element edges do not depend on `loop()`, USB traffic, or anything else the
chip is doing. No `delay()` anywhere, by rule.

## Targets

| env           | board                | notes                                                   |
|---------------|----------------------|---------------------------------------------------------|
| `atoms3_lite` | M5Stack AtomS3 Lite  | primary. ESP32-S3, native USB CDC, no reset on port open |
| `xiao_s3`     | Seeed XIAO ESP32-S3  | same chip class, same behaviour                         |
| `atom_lite`   | M5Stack Atom Lite    | classic ESP32; USB-UART bridge resets the chip when the port opens |

Pin maps are in `include/pins.h`, verified against the arduino-esp32
variant headers and M5's documentation.

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
the computer ground by the optocouplers.

## Status

Milestone 1 (this build): timing engine and a test console. No WinKeyer
protocol yet.

Planned:

1. **Engine** (done): ISR state machine, 50 µs tick, weighting, dit/dah ratio,
   keying compensation, first extension, PTT lead/tail, sidetone, edge log.
2. **Paddle**: 1 kHz sampling, debounce, iambic A/B, Ultimatic, dit/dah
   memory, break-in with FIFO flush (paddle has priority, per the spec).
3. **WinKeyer 2 protocol**: Admin open/close/echo, speed, weight, PTT,
   pause, clear, key immediate, mode register, load defaults, status and
   speed-pot bytes, echo-back, pointer and buffered commands.
4. **WinKeyer 3 extensions**: X1/X2 mode registers, continuous sidetone
   frequency, Vcc readback, firmware revision.
5. **Standalone**: command button, stored messages, settings in NVS.

## Using it with a logger

Plug in, select the COM port, choose "WinKey" (N1MM+: Configure Ports ->
the port -> CW/Other -> Set -> WinKey). The keyer answers Host Open with
revision 31. Speed, PTT timing, mode and echo all come from the logger.

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

Full command list at the top of `src/main.cpp`.

### Protocol check

```
python tools/wk_test.py COM14 --wpm 25 --text "CQ WFD DE AF7QD K"
python tools/wk_test.py COM14 --tune 2
python tools/wk_test.py COM14 --ptt --text "TU 1M WWA"
```

### Timing check

```
pip install pyserial
python tools/check_timing.py COM7 --wpm 20 --text "PARIS PARIS PARIS PARIS PARIS"
```

Captures the edge log and reports measured dit, dah, and gap lengths
against nominal, with worst-case error. The engine's design bound is one
tick (50 µs); anything above 1 % is a bug.

## Build

PlatformIO: `pio run -e atoms3_lite -t upload`, then `pio device monitor`.

After flashing an S3 board over its native USB, Windows sometimes refuses to
open the port ("device not functioning") until the board is unplugged and
plugged back in; it may come back under a new COM number.

Measured on an AtomS3 Lite, 2026-11-09 (milestone 1): worst element error
0.01 % at 12 and 20 WPM, 0.10 % at 35 WPM with weighting and compensation
(one tick of rounding). Opening the USB port does not reset the board.

## References

- K1EL WinKeyer3 IC datasheet v1.3 (protocol spec). K1EL publishes the
  protocol under Creative Commons for personal, non-commercial use.
- K3NG CW keyer, NextGen development notes (the blocking-to-non-blocking
  refactor this design skips by starting non-blocking).
