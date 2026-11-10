# esp32_winkeyer — notes for AI-assisted sessions

Read `README.md`, then `docs/design.md`, `docs/todo.md`, `docs/test-log.md`
before changing anything. The project's one non-negotiable is that Morse
timing never depends on code running in `loop()`.

## Ground rules

- **No `delay()`, no busy-waits, anywhere.** All timing lives in the 50 µs
  timer ISR in `src/keyer_engine.cpp`. If something needs time, it becomes
  an ISR state with a tick countdown.
- **Only the ISR touches key/PTT pins** and only through `set_key()` /
  `set_ptt()`.
- **ISR code**: `IRAM_ATTR`, no heap, no Arduino `Serial`, no LEDC calls,
  no blocking FreeRTOS calls. Hand work to a task via a notification.
- **Rings are SPSC.** Producer writes head, consumer writes tail. The FIFO
  is flushed only from the ISR (`flush_request`).
- **Protocol behaviour comes from the K1EL datasheet**
  (`docs/reference/WK3_Datasheet_v1.3.pdf`), not from memory. Cite the page
  in a comment when implementing a command.
- **Pins come from the variant headers / vendor docs** and are recorded with
  their source in `include/pins.h`. Anything unverified is marked so.
- After any engine change run `tools/check_timing.py` (console build);
  after any protocol change run `tools/wk_test.py`. Record results in
  `docs/test-log.md` with the date and board.
- When hardware is involved, give the person at the bench short, direct
  steps rather than doing it for them. Verify facts before claiming them;
  flag guesses.

## Build / flash

```
pio run -e atoms3_lite -t upload              # WinKeyer on USB (normal)
pio run -e atoms3_lite_console -t upload      # console on USB (timing tests)
pio device list                               # AtomS3 = VID 303A:0009
python tools/check_timing.py COMx --wpm 20
python tools/wk_test.py COMx --wpm 25 --text "CQ TEST"
```

After flashing an S3 over native USB, re-plug if the port refuses to open.

## Layout

- `include/pins.h` — per-board pin maps (AtomS3 Lite primary).
- `include/morse.h`, `src/morse.cpp` — pattern table + reverse lookup.
- `include/keyer_engine.h`, `src/keyer_engine.cpp` — the ISR engine.
- `include/wk_protocol.h`, `src/wk_protocol.cpp` — WinKeyer byte protocol.
- `src/main.cpp` — wiring: USB ↔ protocol, UART1 ↔ console (swap with
  `-DUSB_CONSOLE`).
- `tools/` — host-side test scripts (pyserial).
- `docs/` — design, todo, test log, reference material.
