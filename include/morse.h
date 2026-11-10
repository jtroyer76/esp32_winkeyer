// Morse code table.
//
// Each character is encoded as a byte: a leading 1 marker followed by the
// element bits, read MSB-first after the marker: 0 = dit, 1 = dah.
// Example: 'A' = .-  -> marker, 0, 1 -> 0b101 = 0x05.
// Zero means "no Morse for this character" (the caller skips it).
#pragma once
#include <stdint.h>

namespace morse {

// Returns the pattern for an ASCII character (case-insensitive), or 0.
uint8_t pattern(uint8_t c);

// Reverse lookup for paddle echo: the ASCII character for a pattern, or 0.
uint8_t decode(uint8_t pat);

// Number of elements in a pattern (0 for an empty pattern).
inline uint8_t length(uint8_t pat) {
  uint8_t n = 0;
  while (pat > 1) { pat >>= 1; ++n; }
  return n;
}

// Element i (0-based, from the start of the character). true = dah.
inline bool is_dah(uint8_t pat, uint8_t i) {
  uint8_t n = length(pat);
  return (pat >> (n - 1 - i)) & 1;
}

}  // namespace morse
