// WinKeyer host protocol (K1EL WinKeyer 2/3 command set) on a Stream.
//
// Implemented from the K1EL "WinKeyer 3.1 IC Interface & Operation Manual"
// Rev 1.3. The host sends command bytes 0x00..0x1F (some with parameters)
// and ASCII text; we return echo bytes, status bytes (0xC0 | flags) and
// speed-pot bytes (0x80 | value).
#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace wk {

// Firmware revision reported on Host Open and Admin:GetFWRev.
// 31 = "WinKeyer 3.1" class; hosts enable WK3 features at >= 30.
constexpr uint8_t FW_MAJOR = 31;
constexpr uint8_t FW_MINOR = 3;

void begin(Stream& host);

// Call often from loop(). Parses incoming bytes and emits echo/status.
void poll();

bool host_open();
// Hex-trace every host byte ("< xx" received, "> xx" sent) to a Print; nullptr off.
void set_trace(Print* p);

}  // namespace wk
