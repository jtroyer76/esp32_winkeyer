#include "keyer_engine.h"

#include <Arduino.h>
#include <soc/gpio_reg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "morse.h"
#include "pins.h"

// GPIO register helpers. Pins 0..31 live in the first bank, 32+ in the
// second; the bank is chosen at compile time so the S3 (G38/G39) and the
// classic ESP32 both work without run-time branching in the ISR.
namespace {

template <uint8_t PIN>
inline void IRAM_ATTR pin_write(bool level) {
  if constexpr (PIN < 32) {
    REG_WRITE(level ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, 1UL << PIN);
  } else {
    REG_WRITE(level ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG, 1UL << (PIN - 32));
  }
}

template <uint8_t PIN>
inline bool IRAM_ATTR pin_read() {
  if constexpr (PIN < 32) {
    return (REG_READ(GPIO_IN_REG) >> PIN) & 1;
  } else {
    return (REG_READ(GPIO_IN1_REG) >> (PIN - 32)) & 1;
  }
}

// ---- Lock-free single-producer / single-consumer ring -----------------------
// Writer and reader each own one index; the other side only reads it.

template <typename T, size_t N>
class Ring {
 public:
  bool IRAM_ATTR push(const T& v) {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t n = (h + 1) % N;
    if (n == tail_.load(std::memory_order_acquire)) return false;
    buf_[h] = v;
    head_.store(n, std::memory_order_release);
    return true;
  }
  // Producer-side removal of the last pushed item. Safe only while at least
  // two items are queued, so the consumer cannot be reading the one removed.
  bool unpush() {
    if (used() < 2) return false;
    size_t h = head_.load(std::memory_order_relaxed);
    head_.store((h + N - 1) % N, std::memory_order_release);
    return true;
  }
  bool IRAM_ATTR pop(T& out) {
    size_t t = tail_.load(std::memory_order_relaxed);
    if (t == head_.load(std::memory_order_acquire)) return false;
    out = buf_[t];
    tail_.store((t + 1) % N, std::memory_order_release);
    return true;
  }
  // Consumer-side look-ahead without removal.
  bool IRAM_ATTR peek(size_t i, T& out) const {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_acquire);
    if (((h + N - t) % N) <= i) return false;
    out = buf_[(t + i) % N];
    return true;
  }
  bool IRAM_ATTR empty() const {
    return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
  }
  size_t IRAM_ATTR used() const {
    size_t h = head_.load(std::memory_order_acquire);
    size_t t = tail_.load(std::memory_order_acquire);
    return (h + N - t) % N;
  }
  // Consumer-side flush. Only legal from the consumer (the ISR for the
  // input FIFO); the producer asks for it through a flag.
  void IRAM_ATTR flush() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  T buf_[N];
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

}  // namespace

namespace keyer {

Params params;

namespace {

// ---- Shared state -----------------------------------------------------------

Ring<uint8_t, FIFO_SIZE + 1> fifo;        // characters and buffered commands from the host
Ring<Event, 64>              events;      // ISR -> caller
Ring<Edge, 512>              edges;       // ISR -> harness

std::atomic<bool> tune_request{false};
std::atomic<bool> flush_request{false};   // set by clear(): ISR flushes FIFO and drops current character
std::atomic<bool> speed_override{false};  // set by set_speed(): cancels a buffered speed change
std::atomic<bool> edge_log{false};
std::atomic<bool> engine_busy{false};
std::atomic<bool> paddle_busy{false};
std::atomic<bool> wait_busy{false};

// Sidetone state, handed from the ISR to a task because LEDC calls are not
// ISR-safe. Tens of microseconds of lag on the audio is inaudible and has
// no effect on the key line.
std::atomic<bool> sidetone_want{false};
TaskHandle_t      sidetone_task = nullptr;
constexpr uint8_t SIDETONE_LEDC_CH = 0;

hw_timer_t* timer = nullptr;

// ---- ISR-private state (touched only from the ISR) --------------------------

enum class State : uint8_t {
  Idle,
  Stall,      // a buffered command at the head of the FIFO is incomplete; retry every tick
  PttLead,
  KeyOn,
  ElemGap,    // 1 dit after an element
  CharGap,    // rest of the 3-dit letter space
  WordGap,    // rest of the word space (also used for the '|' half-dit pad)
  PttTail,
  Tune,
  Manual,     // bug / straight key: key follows the dah paddle
  KeyHold,    // buffered timed key-down
  Wait,       // buffered timed pause
};

enum class Source : uint8_t { None, Host, Paddle };

constexpr uint32_t DEBOUNCE_TICKS  = 1000 / TICK_US;   // 1 ms
constexpr uint32_t TICKS_PER_SEC   = 1000000UL / TICK_US;
constexpr uint8_t  WATCHDOG_ELEMS  = 128;

struct PaddleState {
  bool     dit = false, dah = false;     // debounced, true = pressed
  uint32_t dit_cnt = 0, dah_cnt = 0;     // debounce counters
  bool     dit_mem = false, dah_mem = false;
  bool     dit_mem_squeeze = false, dah_mem_squeeze = false;
  bool     last_dah = false;             // last element sent from the paddle
  bool     last_pressed_dah = false;     // most recently pressed paddle (Ultimatic)
  uint8_t  pat = 1;                      // pattern of the character in progress
  bool     no_echo = false;              // manual keying: nothing to decode
  uint8_t  wd_count = 0;                 // consecutive paddle elements
  bool     wd_tripped = false;
} p;

struct IsrState {
  State    state       = State::Idle;
  Source   src         = Source::None;
  uint32_t remaining   = 0;       // ticks left in the current state
  uint32_t elem_ticks  = 0;       // total ticks of the current KeyOn
  uint8_t  cur_char    = 0;
  uint8_t  merge_char  = 0;       // second character of a merged prosign, for echo
  uint8_t  cur_pat     = 0;
  uint8_t  elem_idx    = 0;
  uint8_t  elem_cnt    = 0;
  uint32_t pending_gap_us = 0;    // for '|' and word gaps loaded from the FIFO
  bool     pending_dah = false;   // paddle element waiting for PTT lead-in
  bool     abort_char  = false;   // drop the rest of the current host character
  bool     key         = false;
  bool     ptt         = false;
  bool     first_elem  = true;    // next element is the first after idle
  uint8_t  saved_wpm   = 0;       // speed before a buffered speed change, 0 = none
} s;

// ---- Timing -----------------------------------------------------------------

inline uint32_t IRAM_ATTR to_ticks(uint32_t us) {
  uint32_t t = (us + TICK_US / 2) / TICK_US;
  return t ? t : 1;
}

inline uint8_t IRAM_ATTR wpm_now() {
  uint8_t wpm = params.wpm.load(std::memory_order_relaxed);
  return wpm < 5 ? 5 : wpm;
}

// Spacing dit: the sending speed.
inline uint32_t IRAM_ATTR space_dit_us() { return 1200000UL / wpm_now(); }

// Element dit: the character speed, which is the Farnsworth speed when that
// is higher than the sending speed.
inline uint32_t IRAM_ATTR elem_dit_us() {
  uint8_t wpm = wpm_now();
  uint8_t f = params.farns_wpm.load(std::memory_order_relaxed);
  if (f > wpm) wpm = f;
  return 1200000UL / wpm;
}

// Weighting shifts time from the gap into the element (or back). Keying
// compensation adds a fixed time to every element and removes it from the
// following gap. Neither changes the overall speed.
inline int32_t IRAM_ATTR weight_adj_us(uint32_t dit) {
  int32_t w = params.weight.load(std::memory_order_relaxed);
  return (w - 50) * static_cast<int32_t>(dit) / 50;
}

inline uint32_t IRAM_ATTR element_on_us(bool dah) {
  uint32_t dit  = elem_dit_us();
  uint32_t base = dah ? dit * 3 * params.ratio.load(std::memory_order_relaxed) / 50 : dit;
  int32_t  us   = static_cast<int32_t>(base) + weight_adj_us(dit)
                + params.comp_ms.load(std::memory_order_relaxed) * 1000;
  if (s.first_elem) us += params.first_ext_ms.load(std::memory_order_relaxed) * 1000;
  return us < static_cast<int32_t>(TICK_US) ? TICK_US : static_cast<uint32_t>(us);
}

inline uint32_t IRAM_ATTR element_gap_us() {
  uint32_t dit = elem_dit_us();
  int32_t  us  = static_cast<int32_t>(dit) - weight_adj_us(dit)
               - params.comp_ms.load(std::memory_order_relaxed) * 1000;
  return us < static_cast<int32_t>(TICK_US) ? TICK_US : static_cast<uint32_t>(us);
}

// Letter space is 3 spacing dits in total; the element gap already elapsed.
inline uint32_t IRAM_ATTR char_gap_us() {
  uint32_t total = space_dit_us() * 3;
  uint32_t eg = element_gap_us();
  return total > eg + TICK_US ? total - eg : TICK_US;
}

// Word space is 7 spacing dits (6 with contest spacing); the letter space
// already elapsed.
inline uint32_t IRAM_ATTR word_gap_us() {
  uint32_t dits = params.contest_spacing.load(std::memory_order_relaxed) ? 3 : 4;
  return space_dit_us() * dits;
}

// ---- Outputs ----------------------------------------------------------------

inline void IRAM_ATTR log_edge(Line line, bool level) {
  if (edge_log.load(std::memory_order_relaxed)) {
    edges.push(Edge{static_cast<uint32_t>(micros()), line, static_cast<uint8_t>(level)});
  }
}

inline void IRAM_ATTR set_key(bool on) {
  if (s.key == on) return;
  s.key = on;
  // Paddle watchdog: keying is suppressed but sidetone still sounds.
  bool drive = !(p.wd_tripped && s.src == Source::Paddle);
  if (drive) {
    pin_write<PIN_KEY>(on);
    log_edge(Line::Key, on);
  }
  bool st = on && params.sidetone_enable.load(std::memory_order_relaxed) &&
            (!params.sidetone_paddle_only.load(std::memory_order_relaxed) || s.src == Source::Paddle);
  if (sidetone_want.load(std::memory_order_relaxed) != st) {
    sidetone_want.store(st, std::memory_order_relaxed);
    if (sidetone_task) {
      BaseType_t woken = pdFALSE;
      vTaskNotifyGiveFromISR(sidetone_task, &woken);
      if (woken) portYIELD_FROM_ISR();
    }
  }
}

inline void IRAM_ATTR set_ptt(bool on) {
  if (s.ptt == on) return;
  s.ptt = on;
  pin_write<PIN_PTT>(on);
  log_edge(Line::Ptt, on);
}

inline void IRAM_ATTR enter(State st, uint32_t us) {
  s.state     = st;
  s.remaining = to_ticks(us);
}

inline void IRAM_ATTR go_idle() {
  s.state = State::Idle;
  s.src = Source::None;
  s.first_elem = true;
  paddle_busy.store(false, std::memory_order_relaxed);
}

// Assert PTT (if enabled) and return true if a lead-in wait was started.
inline bool IRAM_ATTR ptt_lead_in() {
  if (!params.ptt_enable.load(std::memory_order_relaxed) || s.ptt) return false;
  set_ptt(true);
  uint32_t lead = params.ptt_lead.load(std::memory_order_relaxed) * 10000UL;
  if (!lead) return false;
  enter(State::PttLead, lead);
  return true;
}

// ---- Paddle -----------------------------------------------------------------

inline void IRAM_ATTR paddle_sample() {
  bool raw_dit = !pin_read<PIN_PADDLE_DIT>();   // closed to ground = pressed
  bool raw_dah = !pin_read<PIN_PADDLE_DAH>();
  if (params.swap_paddles.load(std::memory_order_relaxed)) {
    bool t = raw_dit; raw_dit = raw_dah; raw_dah = t;
  }
  uint8_t sw = params.sw_paddle.load(std::memory_order_relaxed);
  raw_dit |= sw & 1;
  raw_dah |= (sw >> 1) & 1;
  if (raw_dit != p.dit) { if (++p.dit_cnt >= DEBOUNCE_TICKS) { p.dit = raw_dit; p.dit_cnt = 0; if (p.dit) p.last_pressed_dah = false; } }
  else p.dit_cnt = 0;
  if (raw_dah != p.dah) { if (++p.dah_cnt >= DEBOUNCE_TICKS) { p.dah = raw_dah; p.dah_cnt = 0; if (p.dah) p.last_pressed_dah = true; } }
  else p.dah_cnt = 0;
}

// Latch element memory. During an element (past the switchpoint) or its
// following gap, a press of the opposite paddle is remembered so it is sent
// next even if released before the decision point. We also remember whether
// it was latched while both paddles were held, which is what distinguishes
// iambic A from B at release time. With autospace, presses during the
// letter space are latched too and start the next letter when it ends.
inline void IRAM_ATTR paddle_latch() {
  uint8_t sp = params.switchpoint.load(std::memory_order_relaxed);
  if (sp == 0) return;  // memory disabled
  bool in_letter_space = s.state == State::CharGap && s.src == Source::Paddle &&
                         params.autospace.load(std::memory_order_relaxed);
  if (s.state != State::KeyOn && s.state != State::ElemGap && !in_letter_space) return;
  if (s.state == State::KeyOn) {
    // Switchpoint is given in percent of a dit time.
    uint32_t sp_ticks = to_ticks(elem_dit_us() * sp / 100);
    if (s.elem_ticks - s.remaining < sp_ticks) return;
  }
  bool squeeze = p.dit && p.dah;
  bool bug = params.key_mode.load(std::memory_order_relaxed) == KeyMode::Bug;
  if (p.dit && (p.last_dah || in_letter_space))          { p.dit_mem = true; p.dit_mem_squeeze = squeeze; }
  if (p.dah && !bug && (!p.last_dah || in_letter_space)) { p.dah_mem = true; p.dah_mem_squeeze = squeeze; }
}

// Decide whether the paddle wants an element now. Returns true and sets
// `dah` if so; consumes the memories.
inline bool IRAM_ATTR paddle_wants(bool& dah) {
  KeyMode mode = params.key_mode.load(std::memory_order_relaxed);
  bool dit_req = p.dit || p.dit_mem;
  bool dah_req = mode == KeyMode::Bug ? false : (p.dah || p.dah_mem);
  // Iambic A: a memory latched by a squeeze is dropped once both paddles
  // are released. A memory latched by a single tap still inserts.
  if (mode == KeyMode::IambicA && !p.dit && !p.dah) {
    if (p.dit_mem_squeeze) dit_req = false;
    if (p.dah_mem_squeeze) dah_req = false;
  }
  if (dit_req && dah_req) {
    if (mode == KeyMode::Ultimatic) dah = p.last_pressed_dah;
    else dah = !p.last_dah;   // alternate
  } else if (dit_req) {
    dah = false;
  } else if (dah_req) {
    dah = true;
  } else {
    return false;
  }
  p.dit_mem = p.dah_mem = false;
  p.dit_mem_squeeze = p.dah_mem_squeeze = false;
  p.last_dah = dah;
  return true;
}

// Finish the current paddle character: emit the decoded echo.
inline void IRAM_ATTR paddle_char_done() {
  if (p.pat > 1 && !p.no_echo) events.push(Event{EventType::PaddleChar, morse::decode(p.pat)});
  p.pat = 1;
  p.no_echo = false;
  p.wd_count = 0;
  p.wd_tripped = false;
}

// Take over from host sending (break-in): the host's buffer is discarded.
inline void IRAM_ATTR paddle_take_over() {
  if (s.src == Source::Host) {
    fifo.flush();
    s.abort_char = false;
    s.elem_idx = s.elem_cnt = 0;
    s.saved_wpm = 0;
    events.push(Event{EventType::BreakIn, 0});
  }
  s.src = Source::Paddle;
  paddle_busy.store(true, std::memory_order_relaxed);
}

// ---- Element sequencing -----------------------------------------------------

inline void IRAM_ATTR key_element(bool dah) {
  set_key(true);
  enter(State::KeyOn, element_on_us(dah));
  s.elem_ticks = s.remaining;
  s.first_elem = false;
}

void IRAM_ATTR start_paddle_element(bool dah) {
  paddle_take_over();
  if (p.pat < 0x80) p.pat = static_cast<uint8_t>((p.pat << 1) | (dah ? 1 : 0));
  else p.pat = 0xFF;  // too long to decode
  if (params.paddle_watchdog.load(std::memory_order_relaxed) && ++p.wd_count > WATCHDOG_ELEMS) {
    p.wd_tripped = true;
    p.wd_count = WATCHDOG_ELEMS;
  }
  if (ptt_lead_in()) { s.pending_dah = dah; return; }
  key_element(dah);
}

void IRAM_ATTR start_manual_key() {
  paddle_take_over();
  p.no_echo = true;
  if (!params.ptt_enable.load(std::memory_order_relaxed) || s.ptt) { /* no lead-in */ }
  else set_ptt(true);   // operator controls timing; no lead-in wait for a hand key
  set_key(true);
  s.state = State::Manual;
  s.first_elem = false;
}

// Result of trying to take the next item from the FIFO.
enum class Load : uint8_t { None, Char, Gap, Stall };

// Parameter byte count for each buffered command code.
inline uint8_t IRAM_ATTR cmd_params(uint8_t c) {
  switch (c) {
    case CMD_PTT: case CMD_KEY_SECONDS: case CMD_WAIT: case CMD_SPEED: case CMD_HSCW: return 1;
    case CMD_MERGE: return 2;
    default: return 0;
  }
}

inline uint8_t IRAM_ATTR pattern_or_skip(uint8_t c) {
  return morse::pattern(c);
}

// Load the next host item: a character (elements ready), a gap (word space
// or '|' pad), a stall (incomplete command), or nothing. Buffered commands
// that need no engine time are executed here and skipped over.
Load IRAM_ATTR load_next() {
  if (params.paused.load(std::memory_order_relaxed)) return Load::None;
  uint8_t c;
  while (fifo.peek(0, c)) {
    if (c < 0x20) {
      uint8_t n = cmd_params(c);
      uint8_t a = 0, b = 0;
      if (n >= 1 && !fifo.peek(1, a)) return Load::Stall;
      if (n >= 2 && !fifo.peek(2, b)) return Load::Stall;
      // Consume command and parameters.
      fifo.pop(c);
      for (uint8_t i = 0; i < n; ++i) { uint8_t d; fifo.pop(d); }
      switch (c) {
        case CMD_PTT:
          if (!params.ptt_enable.load(std::memory_order_relaxed)) set_ptt(a != 0);
          break;
        case CMD_KEY_SECONDS:
          s.cur_char = 0;
          s.pending_gap_us = a;   // seconds, handled by caller
          return Load::Char;      // caller checks pending_gap_us with elem_cnt == 0 and cur_pat == 0xFE
        case CMD_WAIT:
          s.cur_pat = 0xFD;
          s.elem_cnt = 0;
          s.pending_gap_us = a;
          return Load::Gap;
        case CMD_MERGE: {
          uint8_t p1 = morse::pattern(a), p2 = morse::pattern(b);
          if (!p1 || !p2) break;
          uint8_t l2 = morse::length(p2);
          s.cur_pat = static_cast<uint8_t>((p1 << l2) | (p2 & ((1u << l2) - 1)));
          s.elem_cnt = morse::length(s.cur_pat);
          s.elem_idx = 0;
          s.cur_char = a;
          s.merge_char = b;
          s.abort_char = false;
          return Load::Char;
        }
        case CMD_SPEED:
          if (a >= 5 && a <= 99) {
            if (!s.saved_wpm) s.saved_wpm = params.wpm.load(std::memory_order_relaxed);
            params.wpm.store(a, std::memory_order_relaxed);
          }
          break;
        case CMD_SPEED_CANCEL:
          if (s.saved_wpm) { params.wpm.store(s.saved_wpm, std::memory_order_relaxed); s.saved_wpm = 0; }
          break;
        default:  // CMD_NULL, CMD_HSCW, CMD_NOP and anything unknown
          break;
      }
      continue;
    }
    fifo.pop(c);
    s.cur_char = c;
    s.merge_char = 0;
    s.abort_char = false;
    if (c == ' ') {
      events.push(Event{EventType::CharSent, c});
      s.cur_pat = 0;
      s.elem_cnt = 0;
      s.pending_gap_us = word_gap_us();
      return Load::Gap;
    }
    if (c == '|') {
      s.cur_pat = 0;
      s.elem_cnt = 0;
      s.pending_gap_us = space_dit_us() / 2;
      return Load::Gap;
    }
    s.cur_pat  = pattern_or_skip(c);
    s.elem_cnt = morse::length(s.cur_pat);
    s.elem_idx = 0;
    if (s.elem_cnt) return Load::Char;
    events.push(Event{EventType::CharSent, c});  // unknown char: echo, no keying
  }
  return Load::None;
}

// Start the next element of the current host character; false if none left.
bool IRAM_ATTR start_host_element() {
  if (s.abort_char) s.elem_idx = s.elem_cnt;
  if (s.elem_idx >= s.elem_cnt) return false;
  bool dah = morse::is_dah(s.cur_pat, s.elem_idx++);
  key_element(dah);
  return true;
}

// Act on a Load result for host sending. Returns false if nothing was started.
bool IRAM_ATTR dispatch_load(Load r) {
  switch (r) {
    case Load::None:
      return false;
    case Load::Stall:
      s.state = State::Stall;
      return true;
    case Load::Gap:
      s.src = Source::Host;
      if (s.cur_pat == 0xFD) {   // buffered wait, seconds
        s.cur_pat = 0;
        s.state = State::Wait;
        s.remaining = s.pending_gap_us * TICKS_PER_SEC;
        wait_busy.store(true, std::memory_order_relaxed);
        return true;
      }
      enter(State::WordGap, s.pending_gap_us);
      return true;
    case Load::Char:
      s.src = Source::Host;
      if (s.elem_cnt == 0) {     // buffered timed key-down, seconds in pending_gap_us
        if (ptt_lead_in()) { s.cur_pat = 0xFE; return true; }
        set_key(true);
        s.state = State::KeyHold;
        s.remaining = s.pending_gap_us * TICKS_PER_SEC;
        wait_busy.store(true, std::memory_order_relaxed);
        s.first_elem = false;
        return true;
      }
      if (ptt_lead_in()) return true;
      start_host_element();
      return true;
  }
  return false;
}

// Nothing left from either source: wind down PTT or go idle.
void IRAM_ATTR wind_down() {
  events.push(Event{EventType::Idle, 0});
  if (s.ptt && params.ptt_enable.load(std::memory_order_relaxed)) {
    uint32_t dit = space_dit_us();
    uint32_t hold = (s.src == Source::Paddle)
        ? dit * params.ptt_hang_dits.load(std::memory_order_relaxed)       // word space already elapsed
        : dit * 3 + params.ptt_tail.load(std::memory_order_relaxed) * 10000UL;
    enter(State::PttTail, hold);
  } else {
    go_idle();
  }
}

// After a character or gap: next host item, or wind down.
void IRAM_ATTR after_char() {
  if (!dispatch_load(load_next())) wind_down();
}

inline void IRAM_ATTR host_char_done() {
  events.push(Event{EventType::CharSent, s.cur_char});
  if (s.merge_char) { events.push(Event{EventType::CharSent, s.merge_char}); s.merge_char = 0; }
}

// ---- Tick -------------------------------------------------------------------

void IRAM_ATTR on_tick() {
  if (flush_request.exchange(false, std::memory_order_relaxed)) {
    fifo.flush();
    s.saved_wpm = 0;
    if (s.src == Source::Host) {
      s.abort_char = true;
      if (s.state == State::KeyHold || s.state == State::Wait || s.state == State::Stall) {
        set_key(false);
        wait_busy.store(false, std::memory_order_relaxed);
        enter(State::ElemGap, element_gap_us());
      }
    }
  }
  if (speed_override.exchange(false, std::memory_order_relaxed)) s.saved_wpm = 0;

  paddle_sample();
  paddle_latch();

  // Tune overrides everything.
  if (tune_request.load(std::memory_order_relaxed)) {
    if (s.state != State::Tune) {
      if (params.ptt_enable.load(std::memory_order_relaxed)) set_ptt(true);
      set_key(true);
      s.state = State::Tune;
    }
    // Paddles abort tune.
    if (p.dit || p.dah) tune_request.store(false, std::memory_order_relaxed);
    engine_busy.store(true, std::memory_order_relaxed);
    return;
  }
  if (s.state == State::Tune) {
    set_key(false);
    s.first_elem = true;
    if (s.ptt && params.ptt_enable.load(std::memory_order_relaxed)) {
      enter(State::PttTail, space_dit_us() * 3 + params.ptt_tail.load(std::memory_order_relaxed) * 10000UL);
    } else {
      go_idle();
    }
  }

  bool dah;
  bool bug = params.key_mode.load(std::memory_order_relaxed) == KeyMode::Bug;

  // Manual (bug/straight) keying follows the dah paddle directly.
  if (s.state == State::Manual) {
    if (!p.dah) {
      set_key(false);
      enter(State::ElemGap, element_gap_us());
    }
    engine_busy.store(true, std::memory_order_relaxed);
    return;
  }

  // Paddle may start immediately when the key is up and no mandatory gap is
  // running: idle, PTT tail, waits, and (for its own character) the paddle's
  // letter or word space, unless autospace holds it until the letter space
  // ends. During host sending it waits for the element boundary.
  bool paddle_free =
      s.state == State::Idle || s.state == State::PttTail || s.state == State::Stall ||
      s.state == State::Wait || s.state == State::KeyHold ||
      (s.src == Source::Paddle && s.state == State::WordGap) ||
      (s.src == Source::Paddle && s.state == State::CharGap && !params.autospace.load(std::memory_order_relaxed));
  if (paddle_free) {
    if (bug && p.dah) {
      if (s.state == State::KeyHold || s.state == State::Wait) { set_key(false); wait_busy.store(false, std::memory_order_relaxed); }
      start_manual_key();
      engine_busy.store(true, std::memory_order_relaxed);
      return;
    }
    if (paddle_wants(dah)) {
      if (s.state == State::KeyHold || s.state == State::Wait) { set_key(false); wait_busy.store(false, std::memory_order_relaxed); }
      start_paddle_element(dah);
      engine_busy.store(true, std::memory_order_relaxed);
      return;
    }
  }

  if (s.state == State::Idle || s.state == State::Stall) {
    if (s.state == State::Stall) s.state = State::Idle;
    if (!dispatch_load(load_next()) && s.ptt && params.ptt_enable.load(std::memory_order_relaxed) && s.src == Source::Host) {
      // Stall resolved to nothing (e.g. flush): drop PTT normally.
      wind_down();
    }
    engine_busy.store(s.state != State::Idle, std::memory_order_relaxed);
    return;
  }

  // A host item arriving during PTT tail resumes without dropping PTT.
  if (s.state == State::PttTail && !fifo.empty()) {
    if (dispatch_load(load_next())) return;
  }

  if (s.remaining > 1) { --s.remaining; return; }
  s.remaining = 0;

  switch (s.state) {
    case State::PttLead:
      if (s.src == Source::Paddle) key_element(s.pending_dah);
      else if (s.cur_pat == 0xFE) {       // timed key-down after lead-in
        s.cur_pat = 0;
        set_key(true);
        s.state = State::KeyHold;
        s.remaining = s.pending_gap_us * TICKS_PER_SEC;
        wait_busy.store(true, std::memory_order_relaxed);
      }
      else if (s.elem_cnt == 0) enter(State::WordGap, s.pending_gap_us);
      else start_host_element();
      break;

    case State::KeyOn:
      set_key(false);
      enter(State::ElemGap, element_gap_us());
      break;

    case State::ElemGap:
      // Element boundary: the paddle has priority over the host.
      if (bug && p.dah) { start_manual_key(); break; }
      if (paddle_wants(dah)) { start_paddle_element(dah); break; }
      if (s.src == Source::Host) {
        if (start_host_element()) break;
        host_char_done();
      }
      enter(State::CharGap, char_gap_us());
      break;

    case State::CharGap:
      if (s.src == Source::Paddle) {
        paddle_char_done();
        if (bug && p.dah) { start_manual_key(); break; }
        if (paddle_wants(dah)) { start_paddle_element(dah); break; }   // autospace: latched press
        enter(State::WordGap, word_gap_us());   // one word space before host data resumes
        break;
      }
      if (paddle_wants(dah)) { start_paddle_element(dah); break; }
      after_char();
      break;

    case State::WordGap:
      if (paddle_wants(dah)) { start_paddle_element(dah); break; }
      after_char();
      break;

    case State::KeyHold:
      set_key(false);
      wait_busy.store(false, std::memory_order_relaxed);
      enter(State::ElemGap, element_gap_us());
      break;

    case State::Wait:
      wait_busy.store(false, std::memory_order_relaxed);
      after_char();
      break;

    case State::PttTail:
      set_ptt(false);
      go_idle();
      break;

    default:
      go_idle();
      break;
  }
  engine_busy.store(s.state != State::Idle, std::memory_order_relaxed);
}

void sidetone_task_fn(void*) {
  ledcSetup(SIDETONE_LEDC_CH, params.sidetone_hz.load(), 8);
  ledcAttachPin(PIN_SIDETONE, SIDETONE_LEDC_CH);
  ledcWrite(SIDETONE_LEDC_CH, 0);
  bool on = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool want = sidetone_want.load(std::memory_order_relaxed);
    if (want == on) continue;
    on = want;
    if (on) ledcWriteTone(SIDETONE_LEDC_CH, params.sidetone_hz.load());
    else    ledcWrite(SIDETONE_LEDC_CH, 0);
  }
}

}  // namespace

// ---- Public API -------------------------------------------------------------

void begin() {
  pinMode(PIN_KEY, OUTPUT);
  pinMode(PIN_PTT, OUTPUT);
  pin_write<PIN_KEY>(false);
  pin_write<PIN_PTT>(false);
  pinMode(PIN_PADDLE_DIT, INPUT_PULLUP);
  pinMode(PIN_PADDLE_DAH, INPUT_PULLUP);

  // Sidetone task: high priority, pinned to the app core so it reacts quickly
  // to the ISR notification but never competes with the timer itself.
  xTaskCreatePinnedToCore(sidetone_task_fn, "sidetone", 2048, nullptr,
                          configMAX_PRIORITIES - 2, &sidetone_task, 1);

  // 1 MHz timer base (80 MHz APB / 80), periodic alarm every TICK_US.
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &on_tick, false);
  timerAlarmWrite(timer, TICK_US, true);
  timerAlarmEnable(timer);
}

bool enqueue(uint8_t c) { return fifo.push(c); }
bool unenqueue() { return fifo.unpush(); }

void clear() {
  params.paused.store(false, std::memory_order_relaxed);
  tune_request.store(false, std::memory_order_relaxed);
  flush_request.store(true, std::memory_order_relaxed);
}

size_t fifo_used() { return fifo.used(); }
size_t fifo_free() { return FIFO_SIZE - fifo.used(); }

void set_tune(bool on) { tune_request.store(on, std::memory_order_relaxed); }
bool tuning() { return tune_request.load(std::memory_order_relaxed); }

void set_speed(uint8_t wpm) {
  if (wpm < 5) wpm = 5;
  if (wpm > 99) wpm = 99;
  params.wpm.store(wpm, std::memory_order_relaxed);
  speed_override.store(true, std::memory_order_relaxed);
}

bool busy() { return engine_busy.load(std::memory_order_relaxed) || !fifo.empty(); }
bool paddle_active() { return paddle_busy.load(std::memory_order_relaxed); }
bool waiting() { return wait_busy.load(std::memory_order_relaxed); }

bool pop_event(Event& out) { return events.pop(out); }

void set_edge_log(bool on) { edge_log.store(on, std::memory_order_relaxed); }
bool pop_edge(Edge& out) { return edges.pop(out); }

}  // namespace keyer
