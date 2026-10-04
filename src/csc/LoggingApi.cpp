// Logging::LoggingApi: the remote logging service connection (a BaseApi
// subclass, 0xF580 bytes). +0xD30 listener, +0xD38 "accepted" flag.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::csc {

// 0x14030f480 (LoggingApi slot 17): connection lost - clear the accepted
// flag and tell the listener (vfunc 0x10) the UdpConnection's disconnect
// reason (+0x1C0, read under the connection guard at +0x2E0).
void LoggingApiOnDisconnected(uint8_t* api) {
  api[0xD38] = 0;
  void* listener = *reinterpret_cast<void**>(api + 0xD30);
  if (!listener) return;
  uint8_t* connection = *reinterpret_cast<uint8_t**>(api + 0x2C0);
  game::Call<void (*)(void*)>(0x14034a560)(connection + 0x2E0);  // guard Enter
  int reason = *reinterpret_cast<int*>(connection + 0x1C0);
  game::Call<void (*)(void*)>(0x14034a8b0)(connection + 0x2E0);  // guard Leave
  listener = *reinterpret_cast<void**>(api + 0xD30);
  (*reinterpret_cast<void (***)(void*, uint8_t*, int)>(listener))[0x10 / 8](listener, api, reason);
}

// 0x14030f530 (LoggingApi slot 18): incoming packet. Opcode 1 = login reply
// {1, accepted}; when accepted, flush queued log lines (0x14030fe10), then
// tell the listener (vfunc 8).
void LoggingApiOnPacket(uint8_t* api, const uint8_t* data, int length) {
  if (length <= 0 || data[0] != 1) return;
  bool accepted = data[1] != 0;
  api[0xD38] = accepted;
  if (accepted) game::Call<void (*)(uint8_t*)>(0x14030fe10)(api);
  if (void* listener = *reinterpret_cast<void**>(api + 0xD30))
    (*reinterpret_cast<void (***)(void*, uint8_t*, bool)>(listener))[1](listener, api, api[0xD38] != 0);
}

}  // namespace rebuild::csc

REBUILD_FUNCTION(LoggingApi_OnDisconnected, 0x14030f480, rebuild::csc::LoggingApiOnDisconnected);
REBUILD_FUNCTION(LoggingApi_OnPacket, 0x14030f530, rebuild::csc::LoggingApiOnPacket);
