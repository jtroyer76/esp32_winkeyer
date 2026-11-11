# Test log

## 2026-11-09, AtomS3 Lite, Windows 11 PC (COM13 → COM14 after re-plug)

Build: platform espressif32 6.12.0, Arduino core 2.0.x, PlatformIO 6.2.0.

### Timing (tools/check_timing.py, console build)

| WPM | weight | ratio | comp | worst error | notes |
|---|---|---|---|---|---|
| 20 | 50 | 50 | 0 | 0.01 % | 139 elements, all within 5 µs; stdev < 1 µs |
| 35 | 60 | 50 | 5 ms | 0.10 % | dits 46150 vs 46143 nominal, gaps 22450 vs 22429 (one 50 µs tick) |
| 12 | 50 | 60 | 0 | 0.01 % | dah/dit 3.600 vs 3.600 |
| 25 | 50 | 50 | 0 | 0.01 % | after paddle code added to ISR, no regression |

Raw 20 WPM log: `reference/edges-20wpm-2026-11-09.log`.

First run's earliest edge was at 27.7 s uptime, i.e. the host opening the
port did not reboot the board.

Parser note: the console echoes sent characters without a newline, so edge
lines can carry a leading character ("PE 1234 K 0"); the parser matches
from the right.

### Paddle

Jumper wire to GND. G8 (dit): echoed S, I, H, 5, E, `?` for long runs.
G39 (dah, moved from G38 for silkscreen legibility): T, M, O. Squeeze not
yet tested with a real paddle.

### Protocol (tools/wk_test.py)

```
Host Open -> revision 31
Echo test -> OK
STATUS 0xc0 [idle], POT 20
Sending 'CQ TEST DE N0CALL K' at 25 WPM
STATUS 0xc4 [BUSY] ... echo of each character ... STATUS 0xc0 [idle]
Echo matches text: yes
```
Tune 1.5 s: BUSY during, idle after. PTT-enabled send at 30 WPM: echo OK.

## 2026-11-10, AtomS3 Lite, N1MM+ on Windows 11 (keyer COM14, console on COM15 via FTDI)

Console RX was not wired at first (all six base GPIOs are used; RX is on
Grove G2), so host open/close, edges and a host byte trace were logged
unasked for this session. Afterwards edges and trace went back to opt-in
(`:e on`, `:d on`); host open/close stays.

**First attempt: runaway dah-dit.** Trace showed N1MM's message preamble is
`0A 16 00 16 02 00 1C <wpm>` then the text. The parser took `16 02` as
complete, so `00` began a bogus Admin command, `1C` was its code, and `14`
(Software Paddle) consumed `43` ("C") as "both paddles" — a permanent
squeeze that flushed the message (`> C6`) and keyed dah-dit forever.
Datasheet p. 14: pointer sub-commands 01/02/03 each carry one byte. Fixed;
`wk_test.py` now sends the same preamble.

**After fix:** F1 in N1MM → host open, status C4, echoes `CQ CQ CQ DE
AF7QD ...`, key-down lengths exactly 48 / 144 ms at 25 WPM, no break-in.

**Esc and speed:** Esc sends `0A`; keying stopped after the character in
progress (echo shows only "C"), status idle. Page Up ×3 sent `02 16`,
`02 18`, `02 1A`; the next F1 keyed at 46 / 138 ms = 26 WPM exactly, 85
elements. The datasheet says Clear Buffer ends a character "in midstream";
the firmware finished the character. Changed the same day to cut the
element in progress: five trials of `0A` fired inside a string of zeros at
20 WPM ended the last dah at 15, 94, 154 ms (two trials landed in a gap).
Timing regression after the change: 0.01 % at 20 WPM, 0.05 % at 35.

N1MM full connect sequence as traced (for reference):
`00 02` open, `00 17` minor rev, `0E 05` mode, `05 0A 2D FF` pot,
`01 88` sidetone, `09 07` pincfg, `04 00 00` ptt, `10 00`, `11 00`,
`03 32`, `07`, `00 13 00 00`, `09 07`, `02 wpm` (twice), then per message
`0A 16 00 16 02 00 1C wpm <text>`.

Also observed: closing the USB port with pyserial reboots the S3 (boot
banner after Host Close). Opening does not. N1MM holds the port open all
session, so this does not affect it.

## 2026-11-10: first radio keyed (AtomS3 Lite, PC817, mcHF)

Carrier board wired per `docs/hardware/carrier-board.md` as far as the key
side: 1206 470 Ω straddling strips I/J, PC817 at J/K, magnet wire on the
copper side, interim key lead from J1/K1 to the radio's KEY jack. Radio:
mcHF in CW, straight-key mode. N1MM+ F1 macro keyed the radio; by ear the
sending was clean. No instrument measurement yet; the RBN A/B in
`docs/todo.md` is the next step.

### Not yet tested

RBN A/B against DTR keying, iambic feel, Atom Lite,
XIAO S3, paddle watchdog, buffered wait / timed key / merge over the wire.
