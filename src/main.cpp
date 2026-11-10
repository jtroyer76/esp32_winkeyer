// esp32_winkeyer
//
// USB serial: WinKeyer protocol (what N1MM, Not1MM, fldigi, ... talk to).
// UART1 (PIN_DEBUG_TX/RX, 115200): debug console, see console commands below.
// Build with -DUSB_CONSOLE to swap them (console on USB, WinKeyer on UART1),
// which is what tools/check_timing.py needs.
//
// The console is useful transmit-only (boards with no spare pin for RX, e.g.
// AtomS3 Lite where all six base GPIOs are taken): it reports host
// open/close and, by default, every key/PTT edge without being asked.
//
// Console commands (line based):
//   text          queue text to send
//   :s NN         speed, WPM
//   :w NN         weighting 10..90
//   :r NN         dit/dah ratio 33..66
//   :c NN         keying compensation, ms
//   :x NN         first extension, ms
//   :p L T        PTT lead / tail (x10 ms); ':p off' disables PTT
//   :f NNN        sidetone Hz (0 = off)
//   :e on|off     edge log on the console ("E <micros> <K|P> <0|1>"), default on
//   :t on|off     tune (key down)
//   :k            clear buffer
//   :m a|b|u|g    paddle mode: iambic A, iambic B, Ultimatic, buG/straight
//   :v on|off     swap paddles
//   :h NN         paddle switchpoint 0..90 %
//   :?            show parameters
//
// Paddle-sent characters are echoed in brackets, e.g. "[A]"; "[?]" when the
// pattern is not a known character. "!BREAKIN" marks a paddle interrupting
// host text.
#include <Arduino.h>

#include "keyer_engine.h"
#include "pins.h"
#include "wk_protocol.h"

#if defined(USB_CONSOLE)
#define CONSOLE Serial
#define HOST    Serial1
#else
#define CONSOLE Serial1
#define HOST    Serial
#endif

namespace {

String line;

void show_params() {
  using keyer::params;
  CONSOLE.printf("wpm=%u farns=%u weight=%u ratio=%u comp=%ums firstext=%ums ptt=%s lead=%ums tail=3dits+%ums sidetone=%uHz%s%s tick=%uus fifo=%u/%u\n",
                 params.wpm.load(), params.farns_wpm.load(), params.weight.load(), params.ratio.load(),
                 params.comp_ms.load(), params.first_ext_ms.load(),
                 params.ptt_enable.load() ? "on" : "off",
                 params.ptt_lead.load() * 10, params.ptt_tail.load() * 10,
                 params.sidetone_hz.load(), params.sidetone_enable.load() ? "" : " (muted)",
                 params.sidetone_paddle_only.load() ? " (paddle only)" : "",
                 keyer::TICK_US, keyer::fifo_used(), keyer::FIFO_SIZE);
  static const char* modes[] = {"iambic B", "iambic A", "ultimatic", "bug"};
  CONSOLE.printf("paddle: %s%s%s switchpoint=%u%% hang=+%u dits  host=%s\n",
                 modes[static_cast<uint8_t>(params.key_mode.load()) & 3],
                 params.swap_paddles.load() ? " swapped" : "",
                 params.autospace.load() ? " autospace" : "",
                 params.switchpoint.load(), params.ptt_hang_dits.load(),
                 wk::host_open() ? "open" : "closed");
}

bool on_off(const String& v) { return v == "on" || v == "1"; }

void handle_command(const String& cmd) {
  using keyer::params;
  char op = cmd.length() > 1 ? cmd[1] : '?';
  String arg = cmd.length() > 2 ? cmd.substring(2) : "";
  arg.trim();
  switch (op) {
    case 's': keyer::set_speed(arg.toInt()); break;
    case 'w': params.weight = constrain(arg.toInt(), 10, 90); break;
    case 'r': params.ratio = constrain(arg.toInt(), 33, 66); break;
    case 'c': params.comp_ms = constrain(arg.toInt(), 0, 250); break;
    case 'x': params.first_ext_ms = constrain(arg.toInt(), 0, 250); break;
    case 'p': {
      if (arg == "off") { params.ptt_enable = false; break; }
      int sp = arg.indexOf(' ');
      params.ptt_enable = true;
      params.ptt_lead = constrain(arg.toInt(), 0, 250);
      if (sp > 0) params.ptt_tail = constrain(arg.substring(sp + 1).toInt(), 0, 250);
      break;
    }
    case 'f': {
      int hz = arg.toInt();
      params.sidetone_enable = hz > 0;
      if (hz > 0) params.sidetone_hz = constrain(hz, 300, 4000);
      break;
    }
    case 'e': keyer::set_edge_log(on_off(arg)); break;
    case 't': keyer::set_tune(on_off(arg)); break;
    case 'k': keyer::clear(); break;
    case 'm':
      if (arg == "a") params.key_mode = keyer::KeyMode::IambicA;
      if (arg == "b") params.key_mode = keyer::KeyMode::IambicB;
      if (arg == "u") params.key_mode = keyer::KeyMode::Ultimatic;
      if (arg == "g") params.key_mode = keyer::KeyMode::Bug;
      break;
    case 'v': params.swap_paddles = on_off(arg); break;
    case 'h': params.switchpoint = constrain(arg.toInt(), 0, 90); break;
    default: break;
  }
  show_params();
}

void handle_line(const String& l) {
  if (l.length() == 0) return;
  if (l[0] == ':') { handle_command(l); return; }
  for (size_t i = 0; i < l.length(); ++i) {
    if (!keyer::enqueue(static_cast<uint8_t>(l[i]))) {
      CONSOLE.println("! fifo full");
      break;
    }
  }
}

}  // namespace

void setup() {
  HOST.begin(1200);     // WinKeyer default; irrelevant on USB CDC, real on UART1
#if defined(USB_CONSOLE)
  CONSOLE.begin(115200);
  HOST.begin(1200, SERIAL_8N2, PIN_DEBUG_RX, PIN_DEBUG_TX);
#else
  CONSOLE.begin(115200, SERIAL_8N1, PIN_DEBUG_RX, PIN_DEBUG_TX);
#endif
  keyer::begin();
  keyer::set_edge_log(true);
  wk::begin(HOST);
#if !defined(USB_CONSOLE)
  wk::set_trace(&CONSOLE);
#endif
  if (PIN_STATUS_LED >= 0) pinMode(PIN_STATUS_LED, OUTPUT);
  // Do not wait for a host: the keyer must be ready the instant it boots.
  CONSOLE.println("# esp32_winkeyer console. ':?' for parameters.");
}

void loop() {
  wk::poll();

  static bool was_open = false;
  bool open = wk::host_open();
  if (open != was_open) {
    was_open = open;
    CONSOLE.println(open ? "# host open" : "# host closed");
  }

  while (CONSOLE.available()) {
    char c = static_cast<char>(CONSOLE.read());
    if (c == '\r') continue;
    if (c == '\n') { handle_line(line); line = ""; }
    else if (line.length() < 200) line += c;
  }

#if defined(USB_CONSOLE)
  // With the console on USB the protocol layer is not draining events for a
  // host, so show them here.
  keyer::Event ev;
  while (keyer::pop_event(ev)) {
    switch (ev.type) {
      case keyer::EventType::CharSent:   CONSOLE.write(ev.data); break;
      case keyer::EventType::PaddleChar: CONSOLE.printf("[%c]", ev.data ? ev.data : '?'); break;
      case keyer::EventType::BreakIn:    CONSOLE.print("!BREAKIN"); break;
      case keyer::EventType::Idle:       CONSOLE.println(); break;
    }
  }
#endif

  keyer::Edge e;
  while (keyer::pop_edge(e)) {
    CONSOLE.printf("E %lu %c %u\n", static_cast<unsigned long>(e.t_us),
                   e.line == keyer::Line::Key ? 'K' : 'P', e.level);
  }

  if (PIN_STATUS_LED >= 0) digitalWrite(PIN_STATUS_LED, keyer::busy() ? LOW : HIGH);
}
