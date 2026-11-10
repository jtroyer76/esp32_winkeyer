# Design notes

## Goal

A CW keyer that N1MM+ (and any other WinKeyer-aware logger) drives over USB,
with Morse timing that does not depend on the PC, the USB link, or anything
else the microcontroller is doing. Built for AF7QD's Winter Field Day 2027
go-boxes (Xiegu X6100 and Icom IC-7300, QRP, class 1M).

## Why this exists (decision record, 2026-11-09)

- N1MM DTR/RTS keying through a CH340 USB-serial dongle produced uneven
  elements. Reverse Beacon Network skimmers decoded it but their WPM
  estimates scattered 14-19 on a steady 15 WPM CQ, which is what timing
  jitter looks like. CH340 has no latency control; FTDI (1 ms latency) is
  better but still Windows-timed.
- RTS/CAT keying through the X6100's own USB port did not key at all.
- The hardware answer is a WinKeyer: the PC sends characters, the keyer
  makes the dits. Options weighed:
  - K1EL WKmini / WK3-IC ($10.75 chip + FTDI TTL cable + PC817): lowest
    risk, genuine firmware. **Rejected by Jess: wanted to build the firmware.**
  - K3NG keyer firmware on an Arduino/ESP32: mature, but its timing core is
    being converted from blocking code to a `micros()` state machine (see
    `reference/k3ng-nextgen-notes.md`), which still ties edge timing to how
    fast `loop()` spins. No ESP32-S3 or nRF52 support.
  - **Chosen:** own firmware on ESP32-S3, timing in a hardware-timer ISR.
    The WinKeyer protocol is published by K1EL under Creative Commons for
    personal, non-commercial use (datasheet p. 39).

## Architecture

```
 USB CDC (HWCDC)         UART1 (G38 TX)
      |                       |
  wk_protocol  <--events--  console (debug only)
      |  enqueue()               ^
      v                          | edge log
 +-----------------------------------------+
 |  keyer_engine: 50 us hardware timer ISR |
 |  FIFO -> character -> elements          |
 |  paddle sample/debounce/iambic          |
 |  key / PTT GPIO, sidetone gate          |
 +-----------------------------------------+
```

- **ISR owns all timing.** `on_tick()` runs every 50 µs from a GP timer.
  Every state (PTT lead, element, gap, letter space, word space, PTT tail,
  waits) is a tick countdown. No `delay()` anywhere in the project, by rule.
  Element edge error is bounded by one tick: measured 0.01 % at 12-25 WPM,
  0.10 % at 35 WPM with weighting (rounding on a 22 ms gap).
- **No code outside the ISR touches key or PTT pins.** GPIO is written with
  direct register access (`REG_WRITE(GPIO_OUT_W1TS_REG, ...)`), bank chosen
  at compile time for pins >= 32.
- **Lock-free rings** (single producer / single consumer) carry characters
  in, events out, and edge timestamps out. The FIFO is flushed only by the
  ISR (producer sets a flag) because a producer-side tail write races the
  consumer's pop.
- **Parameters are atomics** read by the ISR at each element boundary, so a
  speed change lands on the next element, as the WinKeyer spec requires.
- **Sidetone** uses LEDC, driven from a high-priority task woken by the ISR
  (LEDC calls are not ISR-safe). Audio lags the key edge by tens of µs,
  which does not matter and never touches the key line.
- **Buffered commands** (PTT, timed key, wait, merge, speed change) travel
  in the FIFO as WinKeyer byte codes with their parameters. The engine
  peeks and will not consume a command until all its bytes are present
  (`State::Stall`), so a host trickling bytes at 1200 baud cannot split one.
- **Paddle** is sampled every tick with a 1 ms debounce. Memory latches
  after the switchpoint; whether it latched during a squeeze is recorded so
  iambic A drops squeeze memory on release while B keeps it. Paddle has
  priority: break-in flushes the host FIFO, per spec.
- **Timing math** (datasheet): dit = 1200/WPM ms; weighting moves time from
  gap to element; compensation adds fixed ms to elements and removes it from
  the following gap; ratio: dah = 3 × nn/50 dits; first extension lengthens
  the first element after idle; Farnsworth sends characters at the higher
  speed with spacing at the sending speed; contest spacing = 6-dit word.

## Boards

| Board | MCU | USB | Verdict |
|---|---|---|---|
| M5Stack AtomS3 Lite | ESP32-S3 | native CDC (USB-Serial-JTAG) | **primary**. Port open does not reset the chip (verified: edges logged at 27 s uptime right after the host opened the port). Has a button (G41?) and RGB LED (G35?) for standalone mode; those two GPIOs are from memory, verify on the schematic image before use. |
| Seeed XIAO ESP32-S3 | ESP32-S3 | native CDC | same class, builds, untested on hardware |
| M5Stack Atom Lite | ESP32 (PICO-D4) | CH9102/CH552 bridge | builds, untested. The bridge resets the chip when a program opens the port; boot must be fast enough for the host's open to land. Unknown until tested. |
| XIAO nRF52840 | nRF52 | native | not supported, not planned |

After flashing an S3 over native USB, Windows may refuse to open the port
until the board is re-plugged; it can come back on a new COM number.

## Hardware interface

See README for the circuit. Key points:
- PC817 optocoupler per output: GPIO → 470 Ω → LED; transistor across the
  radio's key (or PTT) jack. Isolates the radio from USB ground. 3.3 V GPIO
  through 470 Ω gives ~4.5 mA LED current; use 330 Ω if a radio's key line
  needs more sink current (X6100/IC-7300 key lines are low-current pull-ups,
  a couple of mA).
- Paddle inputs: internal pull-ups work on the bench; add 10 k external
  pull-ups and 100 nF to ground in the van (RF environment), as the WK3serial
  guide does.
- Radio set to straight-key mode on its KEY jack.

## Testing

- `tools/check_timing.py`: drives the console build, captures ISR edge
  timestamps, compares every element and gap to nominal. Run after any
  engine change. Baseline logs in `docs/reference/`.
- `tools/wk_test.py`: a host simulator (N1MM-style connect sequence, echo,
  status, tune, PTT). Run after any protocol change.
- The ISR timestamps measure the firmware's own view; a scope on G5 would
  add the GPIO path, which is sub-µs.

## Open items

See `docs/todo.md`.
