#include "wk_protocol.h"

#include "keyer_engine.h"

namespace wk {

namespace {

Stream* io = nullptr;

// Host interface state.
bool    opened = false;
uint8_t wk_mode = 1;            // 1, 2 or 3, selected by Admin commands
bool    serial_echo = false;
bool    paddle_echo = false;
uint8_t pot_min = 5;            // Setup Speed Pot: MIN and RANGE
uint8_t pot_range = 30;
uint8_t last_status = 0;
uint32_t tune_started_ms = 0;
constexpr uint32_t TUNE_WATCHDOG_MS = 100000;   // 100 s, per the datasheet

// Command parser.
uint8_t  cmd = 0;               // command byte being collected, 0xFF = none
uint8_t  needed = 0;
uint8_t  got = 0;
uint8_t  buf[16];
uint16_t sink = 0;              // bytes to discard (Admin:LoadEEPROM)

constexpr uint8_t NO_CMD = 0xFF;

inline void send(uint8_t b) { io->write(b); }

// Parameter byte counts for immediate commands (index = command byte).
// Admin (0x00) takes the admin code first; extra parameters depend on it.
constexpr uint8_t kImmediateParams[0x20] = {
    /*00 Admin*/ 1, /*01 Sidetone*/ 1, /*02 Speed*/ 1, /*03 Weight*/ 1,
    /*04 PTT lead/tail*/ 2, /*05 Speed pot*/ 3, /*06 Pause*/ 1, /*07 Get pot*/ 0,
    /*08 Backspace*/ 0, /*09 PinConfig*/ 1, /*0A Clear*/ 0, /*0B Key immediate*/ 1,
    /*0C HSCW*/ 1, /*0D Farnsworth*/ 1, /*0E Mode*/ 1, /*0F Load defaults*/ 15,
    /*10 1st extension*/ 1, /*11 Key comp*/ 1, /*12 Switchpoint*/ 1, /*13 Null*/ 0,
    /*14 Software paddle*/ 1, /*15 Get status*/ 0, /*16 Pointer*/ 1, /*17 Ratio*/ 1,
    /*18 PTT buffered*/ 1, /*19 Key buffered*/ 1, /*1A Wait*/ 1, /*1B Merge*/ 0,
    /*1C Speed buffered*/ 1, /*1D HSCW buffered*/ 1, /*1E Cancel speed*/ 0, /*1F NOP*/ 0,
};

uint8_t admin_extra_params(uint8_t code) {
  switch (code) {
    case 4:  return 1;    // Echo test
    case 14: return 1;    // Send message
    case 15: return 1;    // Load X1MODE
    case 19: return 2;    // RTTY registers
    case 22: return 1;    // Load X2MODE
    case 25: return 1;    // Sidetone volume
    default: return 0;
  }
}

// ---- Status -----------------------------------------------------------------

uint8_t status_byte() {
  uint8_t st = 0xC0;
  if (keyer::waiting()) st |= 0x10;
  if (wk_mode == 1 && keyer::tuning()) st |= 0x08;   // WK1: KEYDOWN; WK2/3: bit 3 = 0 marks a status byte
  if (keyer::busy()) st |= 0x04;
  if (keyer::paddle_active()) st |= 0x02;
  if (keyer::fifo_used() > (keyer::FIFO_SIZE * 2) / 3) st |= 0x01;
  return st;
}

void send_status() {
  last_status = status_byte();
  send(last_status);
}

uint8_t pot_byte() {
  int v = static_cast<int>(keyer::params.wpm.load()) - pot_min;
  if (v < 0) v = 0;
  if (v > 31) v = 31;
  return 0x80 | static_cast<uint8_t>(v);
}

// ---- Setters shared by individual commands and Load Defaults ----------------

void set_sidetone(uint8_t nn) {
  using keyer::params;
  if (wk_mode == 3) {
    // Continuously adjustable: value = 62500 / frequency.
    if (nn == 0) return;
    unsigned hz = 62500u / nn;
    if (hz < 300) hz = 300;
    if (hz > 4000) hz = 4000;
    params.sidetone_hz = static_cast<uint16_t>(hz);
  } else {
    params.sidetone_paddle_only = (nn & 0x80) != 0;
    uint8_t n = nn & 0x0F;
    if (n >= 1 && n <= 10) params.sidetone_hz = static_cast<uint16_t>(4000 / n);
  }
}

void set_mode(uint8_t m) {
  using keyer::params;
  params.paddle_watchdog = !(m & 0x80);
  paddle_echo = (m & 0x40) != 0;
  params.key_mode = static_cast<keyer::KeyMode>((m >> 4) & 0x03);
  params.swap_paddles = (m & 0x08) != 0;
  serial_echo = (m & 0x04) != 0;
  params.autospace = (m & 0x02) != 0;
  params.contest_spacing = (m & 0x01) != 0;
}

void set_pincfg(uint8_t c) {
  using keyer::params;
  params.ptt_enable = (c & 0x01) != 0;
  params.sidetone_enable = (c & 0x02) != 0;
  // Bits 2-3 select key port 1/2: single port here. Bits 6-7: Ultimatic priority, not implemented.
  static const uint8_t hang[4] = {1, 2, 4, 8};
  params.ptt_hang_dits = hang[(c >> 4) & 0x03];
}

void set_x2mode(uint8_t x) {
  keyer::params.sidetone_paddle_only = (x & 0x08) != 0;
  // Bit 7 paddle status, 6 fast command response, 5/4 cut numbers, 2 SO2R, 1 paddle mute: not implemented.
}

void set_weight(uint8_t w)      { keyer::params.weight = constrain(w, 10, 90); }
void set_ratio(uint8_t r)       { keyer::params.ratio = constrain(r, 33, 66); }
void set_switchpoint(uint8_t s) { keyer::params.switchpoint = s > 90 ? 90 : s; }   // 0 disables memory

// ---- Command execution ------------------------------------------------------

void exec_admin(uint8_t code, const uint8_t* a) {
  using keyer::params;
  switch (code) {
    case 0: break;                                  // Calibrate (WK1): ignored
    case 1:                                         // Reset
      opened = false; wk_mode = 1; serial_echo = paddle_echo = false;
      keyer::clear();
      break;
    case 2: {                                       // Host Open
      keyer::Event stale;
      while (keyer::pop_event(stale)) {}            // drop events from before the host was listening
      opened = true; wk_mode = 1;
      send(FW_MAJOR);
      last_status = status_byte();
      break;
    }
    case 3:                                         // Host Close
      opened = false;
      break;
    case 4: send(a[0]); break;                      // Echo test
    case 5: case 6: case 7: send(0); break;         // Historical A2D / Get Values
    case 8: break;                                  // K1EL debug
    case 9: send(FW_MAJOR); break;                  // Get FW major rev
    case 10: wk_mode = 1; break;                    // Set WK1 mode
    case 11: wk_mode = 2; break;                    // Set WK2 mode
    case 12: for (int i = 0; i < 256; ++i) send(0xFF); break;   // Dump EEPROM: no standalone store yet
    case 13: sink = 256; break;                     // Load EEPROM: accept and discard
    case 14: break;                                 // Send message: standalone messages not implemented yet
    case 15: break;                                 // Load X1MODE: letterspace adjustment not implemented
    case 16: break;                                 // Firmware update: not supported
    case 17: case 18: break;                        // Baud rate: meaningless over USB CDC
    case 19: break;                                 // RTTY: not implemented
    case 20: wk_mode = 3; break;                    // Set WK3 mode
    case 21: send(52); break;                       // Vcc readback: 26214/52 = 5.04 V
    case 22: set_x2mode(a[0]); break;
    case 23: send(FW_MINOR); break;
    case 24: send(0); break;                        // IC type: DIP
    case 25: break;                                 // Sidetone volume: fixed
    default: break;
  }
}

void exec(uint8_t c, const uint8_t* a) {
  using keyer::params;
  if (c == 0x00) { exec_admin(a[0], a + 1); return; }
  if (!opened) return;   // only Admin commands are accepted while closed
  switch (c) {
    case 0x01: set_sidetone(a[0]); break;
    case 0x02: keyer::set_speed(a[0]); break;
    case 0x03: set_weight(a[0]); break;
    case 0x04: params.ptt_lead = a[0]; params.ptt_tail = a[1]; break;
    case 0x05: pot_min = a[0]; pot_range = a[1]; break;
    case 0x06: params.paused = (a[0] != 0); break;
    case 0x07: send(pot_byte()); break;
    case 0x08: keyer::unenqueue(); break;
    case 0x09: set_pincfg(a[0]); break;
    case 0x0A: keyer::clear(); break;
    case 0x0B:
      keyer::set_tune(a[0] == 1);
      tune_started_ms = millis();
      break;
    case 0x0C: break;                               // HSCW: not implemented
    case 0x0D: params.farns_wpm = (a[0] >= 10 && a[0] <= 99) ? a[0] : 0; break;
    case 0x0E: set_mode(a[0]); break;
    case 0x0F:                                      // Load Defaults, 15 values in datasheet order
      set_mode(a[0]);
      keyer::set_speed(a[1]);
      set_sidetone(a[2]);
      set_weight(a[3]);
      params.ptt_lead = a[4];
      params.ptt_tail = a[5];
      pot_min = a[6];
      pot_range = a[7];
      set_x2mode(a[8]);
      params.comp_ms = a[9];
      params.farns_wpm = (a[10] >= 10 && a[10] <= 99) ? a[10] : 0;
      set_switchpoint(a[11]);
      set_ratio(a[12]);
      set_pincfg(a[13]);
      // a[14] = X1MODE: letterspace adjustment, not implemented
      break;
    case 0x10: params.first_ext_ms = a[0]; break;
    case 0x11: params.comp_ms = a[0]; break;
    case 0x12: set_switchpoint(a[0]); break;
    case 0x13: break;                               // Null
    case 0x14: params.sw_paddle = a[0] & 0x03; break;
    case 0x15: send_status(); break;
    case 0x16: break;                               // Pointer commands: see note in README
    case 0x17: set_ratio(a[0]); break;
    // Buffered commands go into the FIFO with their parameters; the engine
    // executes them in stream order.
    case 0x18: case 0x19: case 0x1A: case 0x1C: case 0x1D:
      keyer::enqueue(c); keyer::enqueue(a[0]);
      break;
    case 0x1B: case 0x1E: case 0x1F:
      keyer::enqueue(c);
      break;
    default: break;
  }
}

void on_byte(uint8_t b) {
  if (sink) { --sink; return; }

  if (cmd != NO_CMD) {
    buf[got++] = b;
    // Admin: the first parameter is the admin code, which may need more.
    if (cmd == 0x00 && got == 1) needed = static_cast<uint8_t>(1 + admin_extra_params(b));
    // Pointer command 03 carries one extra parameter.
    if (cmd == 0x16 && got == 1 && b == 3) needed = 2;
    if (got >= needed) {
      uint8_t c = cmd;
      cmd = NO_CMD;
      exec(c, buf);
    }
    return;
  }

  if (b < 0x20) {
    cmd = b;
    needed = kImmediateParams[b];
    got = 0;
    if (needed == 0) { cmd = NO_CMD; exec(b, buf); }
    return;
  }

  if (b >= 0x80) return;        // not text (e.g. the 0xFF that follows WK1 calibrate)
  if (!opened) return;
  if (b >= 'a' && b <= 'z') b = static_cast<uint8_t>(b - 'a' + 'A');
  keyer::enqueue(b);
}

}  // namespace

void begin(Stream& host) {
  io = &host;
  cmd = NO_CMD;
}

bool host_open() { return opened; }

void poll() {
  while (io->available() > 0) on_byte(static_cast<uint8_t>(io->read()));

  // Tune watchdog.
  if (keyer::tuning() && millis() - tune_started_ms > TUNE_WATCHDOG_MS) keyer::set_tune(false);

  // Events are consumed only while a host is open; otherwise they are left
  // for the debug console (USB_CONSOLE builds) or dropped at the next open.
  keyer::Event ev;
  while (opened && keyer::pop_event(ev)) {
    switch (ev.type) {
      case keyer::EventType::CharSent:
        if (serial_echo) send(ev.data);
        break;
      case keyer::EventType::PaddleChar:
        if (paddle_echo && ev.data) send(ev.data);
        break;
      default:
        break;
    }
  }

  // Unsolicited status on change.
  if (opened) {
    uint8_t st = status_byte();
    if (st != last_status) { last_status = st; send(st); }
  }
}

}  // namespace wk
