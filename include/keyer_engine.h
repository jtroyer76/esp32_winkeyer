// Keyer timing engine.
//
// All element timing (key line, PTT, gaps, sidetone gating) runs inside a
// hardware-timer ISR with a fixed tick. Nothing in this module waits, and no
// code outside the ISR ever touches the key or PTT pin. The rest of the
// firmware talks to the engine only through:
//   - the input FIFO (enqueue / clear): characters and in-stream buffered
//     commands, byte-compatible with the WinKeyer buffered command set,
//   - atomic parameters (speed, weight, ...), read by the ISR at each
//     element boundary so changes land on the next element,
//   - the event queue (character sent, idle), drained by the caller,
//   - the edge log (timestamped key/PTT transitions) for the test harness.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <atomic>

namespace keyer {

// Timer tick. Element edges land on a tick boundary, so this bounds the
// timing error: 50 us is 0.125% of a dit at 30 WPM.
constexpr uint32_t TICK_US = 50;

constexpr size_t FIFO_SIZE = 160;   // WinKeyer input buffer size

// In-stream (buffered) command bytes, WinKeyer-compatible. Anything below
// 0x20 in the FIFO is one of these; the engine knows each one's parameter
// count and will not consume a command until all its bytes are present.
enum BufferedCmd : uint8_t {
  CMD_NULL        = 0x13,  // placeholder, ignored
  CMD_PTT         = 0x18,  // <nn> 1 = on, 0 = off (only when PTT disabled in config)
  CMD_KEY_SECONDS = 0x19,  // <nn> key down for nn seconds
  CMD_WAIT        = 0x1A,  // <nn> pause nn seconds
  CMD_MERGE       = 0x1B,  // next two characters sent as one prosign
  CMD_SPEED       = 0x1C,  // <nn> buffered speed change
  CMD_HSCW        = 0x1D,  // <nn> ignored
  CMD_SPEED_CANCEL= 0x1E,  // restore speed from before CMD_SPEED
  CMD_NOP         = 0x1F,
};

enum class KeyMode : uint8_t { IambicB = 0, IambicA = 1, Ultimatic = 2, Bug = 3 };

// Operating parameters. Units follow the WinKeyer datasheet.
struct Params {
  std::atomic<uint8_t>  wpm{20};          // 5..99
  std::atomic<uint8_t>  farns_wpm{0};     // Farnsworth: characters at this speed when > wpm; 0 = off
  std::atomic<uint8_t>  weight{50};       // 10..90, 50 = no change
  std::atomic<uint8_t>  ratio{50};        // 33..66, dah = 3 * ratio/50 dits
  std::atomic<uint8_t>  comp_ms{0};       // keying compensation, added to every element
  std::atomic<uint8_t>  first_ext_ms{0};  // added to the first element after idle
  std::atomic<uint8_t>  ptt_lead{0};      // x 10 ms
  std::atomic<uint8_t>  ptt_tail{0};      // x 10 ms, added to 3 dits
  std::atomic<bool>     ptt_enable{true};
  std::atomic<bool>     sidetone_enable{true};
  std::atomic<bool>     sidetone_paddle_only{false};
  std::atomic<uint16_t> sidetone_hz{700};
  std::atomic<bool>     paused{false};
  std::atomic<bool>     contest_spacing{false};  // 6-dit word space instead of 7

  // Paddle.
  std::atomic<KeyMode>  key_mode{KeyMode::IambicB};
  std::atomic<bool>     swap_paddles{false};
  std::atomic<bool>     autospace{false};
  std::atomic<uint8_t>  switchpoint{50};      // 10..90 % of a dit; 0 disables paddle memory
  std::atomic<uint8_t>  ptt_hang_dits{1};     // PTT hang after paddling = 1 word space + this many dits
  std::atomic<bool>     paddle_watchdog{true};// key off after 128 consecutive paddle elements
  std::atomic<uint8_t>  sw_paddle{0};         // host software paddle: bit0 dit, bit1 dah
};

enum class EventType : uint8_t {
  CharSent,     // one host character finished; `data` = the character
  PaddleChar,   // one paddle-sent character finished; `data` = decoded ASCII or 0
  BreakIn,      // paddle interrupted host sending; FIFO was flushed
  Idle,         // nothing left to send (PTT may still be in tail)
};

struct Event {
  EventType type;
  uint8_t   data;
};

enum class Line : uint8_t { Key = 0, Ptt = 1 };

struct Edge {
  uint32_t t_us;   // micros() at the transition
  Line     line;
  uint8_t  level;  // 1 = asserted
};

extern Params params;

void begin();

// Input FIFO.
bool   enqueue(uint8_t c);   // false if full
bool   unenqueue();          // remove the most recently queued byte (host backspace); false if not possible
void   clear();              // flush FIFO, cancel pause and tune; current character stops at its next element boundary
size_t fifo_used();
size_t fifo_free();

// Key immediate (tune). Overrides the FIFO while set.
void set_tune(bool on);
bool tuning();

// Immediate speed change from the host also cancels any buffered speed change.
void set_speed(uint8_t wpm);

// True while any element, gap, or PTT sequence is in progress.
bool busy();

// True from the first paddle element until a word space after the last.
bool paddle_active();

// True while a buffered wait or timed key-down is running (WinKeyer WAIT status bit).
bool waiting();

// Event queue (caller drains).
bool pop_event(Event& out);

// Edge log for the timing harness.
void set_edge_log(bool on);
bool pop_edge(Edge& out);

}  // namespace keyer
