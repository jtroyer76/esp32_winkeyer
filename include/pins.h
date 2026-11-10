// Pin assignments. Verified against the arduino-esp32 variant headers
// (variants/XIAO_ESP32S3/pins_arduino.h, variants/m5stack_atom/pins_arduino.h).
#pragma once
#include <stdint.h>

#if defined(BOARD_ATOMS3_LITE)
// M5Stack AtomS3 Lite. Header IO: G5 G6 G7 G8 G38 G39 (+3V3, 5V, GND); Grove: G1 G2.
// Verified against docs.m5stack.com/en/core/AtomS3%20Lite, 2026-11-09.
constexpr uint8_t PIN_KEY        = 5;   // -> 470R -> PC817 LED (key out, high = key down)
constexpr uint8_t PIN_PTT        = 6;   // -> 470R -> PC817 LED (PTT out, high = PTT on)
constexpr uint8_t PIN_SIDETONE   = 7;   // -> 100R -> piezo
constexpr uint8_t PIN_PADDLE_DIT = 8;   // 10k pull-up to 3V3, 100nF to GND, closes to GND
constexpr uint8_t PIN_PADDLE_DAH = 39;  // same (G39 is easier to read on the silkscreen than G38)
constexpr uint8_t PIN_DEBUG_TX   = 38;  // UART1 TX for the timing harness
constexpr uint8_t PIN_DEBUG_RX   = 2;   // Grove, unused
constexpr int8_t  PIN_STATUS_LED = -1;  // RGB WS2812 (G35) and button (G41): from memory, verify before use
#elif defined(BOARD_XIAO_S3)
// Seeed XIAO ESP32-S3. Dn labels are the silkscreen names.
constexpr uint8_t PIN_KEY        = 2;   // D1 -> 470R -> PC817 LED (key out, high = key down)
constexpr uint8_t PIN_PTT        = 3;   // D2 -> 470R -> PC817 LED (PTT out, high = PTT on)
constexpr uint8_t PIN_SIDETONE   = 4;   // D3 -> 100R -> piezo
constexpr uint8_t PIN_PADDLE_DIT = 5;   // D4, 10k pull-up to 3V3, 100nF to GND, closes to GND
constexpr uint8_t PIN_PADDLE_DAH = 6;   // D5, same
constexpr uint8_t PIN_DEBUG_TX   = 43;  // D6, UART1 TX for the timing harness
constexpr uint8_t PIN_DEBUG_RX   = 44;  // D7
constexpr int8_t  PIN_STATUS_LED = 21;  // on-board LED, active low
#elif defined(BOARD_ATOM_LITE)
// M5Stack Atom Lite, 2x4 header pins only (none are strapping pins).
constexpr uint8_t PIN_KEY        = 25;
constexpr uint8_t PIN_PTT        = 21;
constexpr uint8_t PIN_SIDETONE   = 22;
constexpr uint8_t PIN_PADDLE_DIT = 19;
constexpr uint8_t PIN_PADDLE_DAH = 23;
constexpr uint8_t PIN_DEBUG_TX   = 33;
constexpr uint8_t PIN_DEBUG_RX   = 32;  // Grove, unused
constexpr int8_t  PIN_STATUS_LED = -1;  // Atom has an RGB LED on G27, not a plain LED
#else
#error "Define BOARD_ATOMS3_LITE, BOARD_XIAO_S3 or BOARD_ATOM_LITE"
#endif
