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

// 0x14030e500 (LoggingApi slot 9): GiveTime - under the API mutex (+0x11D8)
// and with its own thread allocator (+0x11D0) active: BaseApi::GiveTime,
// the rate tracker (+0xE400), then queue flushing (0x14030d4b0 / 0x14030db40).
void LoggingApiGiveTime(uint8_t* api, int time) {
  void* mutex = api + 0x11D8;
  game::Call<void (*)(void*)>(0x14032f270)(mutex);  // lock
  void* previous = game::Call<void* (*)(void*)>(0x14032f8b0)(*reinterpret_cast<void**>(api + 0x11D0));  // SetThreadAllocator
  game::Call<void (*)(uint8_t*, int)>(0x1415f43a0)(api, time);
  if (void* tracker = *reinterpret_cast<void**>(api + 0xE400)) game::Call<void (*)(void*)>(0x140311f50)(tracker);
  game::Call<void (*)(uint8_t*)>(0x14030d4b0)(api);
  game::Call<void (*)(uint8_t*)>(0x14030db40)(api);
  game::Call<void* (*)(void*)>(0x14032f8b0)(previous);
  game::Call<void (*)(void*)>(0x14032f360)(mutex);  // unlock
}

// 0x14030f320 (LoggingApi slot 16): send the login packet
// {1, user\0, password\0, directory\0, int32 big-endian}. Each string is
// copied bounded by the 1 KB buffer but the next field starts at the stored
// length + 1, as in the original.
void LoggingApiSendLogin(uint8_t* api) {
  char buffer[0x400];
  char* const bufferEnd = buffer + 0x400;
  buffer[0] = 1;
  auto copyField = [&](char* start, const char* source) {
    char* limit = start == buffer + 1 ? buffer + 0x3FF : start - 1 + (bufferEnd - start);
    char* out = start;
    while (out != limit && *source) *out++ = *source++;
    *out = 0;
  };
  char* field = buffer + 1;
  copyField(field, *reinterpret_cast<const char**>(api + 0xD48));
  field += *reinterpret_cast<int*>(api + 0xD50) + 1;
  copyField(field, *reinterpret_cast<const char**>(api + 0xE68));
  field += *reinterpret_cast<int*>(api + 0xE70) + 1;
  copyField(field, *reinterpret_cast<const char**>(api + 0xF88));
  field += *reinterpret_cast<int*>(api + 0xF90) + 1;
  int value = *reinterpret_cast<int*>(api + 0x11C0);
  field[0] = static_cast<char>(value >> 24);
  field[3] = static_cast<char>(value);
  field[1] = static_cast<char>(value >> 16);
  field[2] = static_cast<char>(value >> 8);
  using SendFn = bool (*)(uint8_t*, const char*, int, bool);
  (*reinterpret_cast<SendFn**>(api))[0xA0 / 8](api, buffer, static_cast<int>(field - buffer) + 4, true);
}

}  // namespace rebuild::csc

REBUILD_FUNCTION(LoggingApi_OnDisconnected, 0x14030f480, rebuild::csc::LoggingApiOnDisconnected);
REBUILD_FUNCTION(LoggingApi_OnPacket, 0x14030f530, rebuild::csc::LoggingApiOnPacket);
REBUILD_FUNCTION(LoggingApi_GiveTime, 0x14030e500, rebuild::csc::LoggingApiGiveTime);
REBUILD_FUNCTION(LoggingApi_SendLogin, 0x14030f320, rebuild::csc::LoggingApiSendLogin);
