#include "morse.h"

namespace morse {

namespace {

// Build a pattern from a string of '.' and '-' at compile time.
constexpr uint8_t P(const char* s) {
  uint8_t v = 1;
  for (; *s; ++s) v = static_cast<uint8_t>((v << 1) | (*s == '-' ? 1 : 0));
  return v;
}

// Letters A-Z.
constexpr uint8_t kLetters[26] = {
    P(".-"),   P("-..."), P("-.-."), P("-.."),  P("."),    P("..-."), P("--."),
    P("...."), P(".."),   P(".---"), P("-.-"),  P(".-.."), P("--"),   P("-."),
    P("---"),  P(".--."), P("--.-"), P(".-."),  P("..."),  P("-"),    P("..-"),
    P("...-"), P(".--"),  P("-..-"), P("-.--"), P("--.."),
};

// Digits 0-9.
constexpr uint8_t kDigits[10] = {
    P("-----"), P(".----"), P("..---"), P("...--"), P("....-"),
    P("....."), P("-...."), P("--..."), P("---.."), P("----."),
};

struct Punct { uint8_t c; uint8_t pat; };

// Punctuation per the WinKeyer datasheet character set, plus the
// WinKeyer-specific prosign mappings. First match wins on decode, so the
// canonical character for a shared pattern comes first.
constexpr Punct kPunct[] = {
    {'/', P("-..-.")},  {'?', P("..--..")}, {',', P("--..--")}, {'.', P(".-.-.-")},
    {'=', P("-...-")},  {'+', P(".-.-.")},  {'-', P("-....-")}, {'(', P("-.--.")},
    {')', P("-.--.-")}, {':', P("---...")}, {';', P("-.-.-.")}, {'"', P(".-..-.")},
    {'\'', P(".----.")}, {'@', P(".--.-.")}, {'!', P("-.-.--")}, {'$', P("...-..-")},
    {'&', P(".-...")},  {'_', P("..--.-")},
    {'<', P(".-...")},  // AS (WinKeyer '<')
    {'>', P("...-.-")}, // SK
};

}  // namespace

uint8_t pattern(uint8_t c) {
  if (c >= 'a' && c <= 'z') c = static_cast<uint8_t>(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return kLetters[c - 'A'];
  if (c >= '0' && c <= '9') return kDigits[c - '0'];
  for (const auto& p : kPunct) if (p.c == c) return p.pat;
  return 0;  // includes '|' (half space, handled by the engine)
}

uint8_t decode(uint8_t pat) {
  if (pat <= 1) return 0;
  for (uint8_t i = 0; i < 26; ++i) if (kLetters[i] == pat) return static_cast<uint8_t>('A' + i);
  for (uint8_t i = 0; i < 10; ++i) if (kDigits[i] == pat) return static_cast<uint8_t>('0' + i);
  for (const auto& p : kPunct) if (p.pat == pat) return p.c;
  return 0;
}

}  // namespace morse
