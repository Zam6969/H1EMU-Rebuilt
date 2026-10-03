// The game's zone client (vtable 0x1420dd6e0, 0x2B8 bytes): the gateway
// listener. Gateway::ExternalGatewayApi calls these slots:
//   0 dtor, 1 OnConnect, 2 OnDisconnect, 3 OnFailed, 4 OnLoginReply,
//   5 OnForcedLogout, 6 OnChannelIsRoutable, 7 OnConnectionIsNotRoutable,
//   8 OnTunnelData (zone packets -> game), 9 (unused).
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::game_net {
namespace {

// "NetInfo" log channel name (char* global).
const char* NetInfoLog() { return *reinterpret_cast<const char**>(0x142053fd8); }

template <typename... Args>
void Log(uintptr_t format, Args... args) {
  game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(NetInfoLog(), reinterpret_cast<const char*>(format),
                                                                   args...);
}

void* GameClient() { return *reinterpret_cast<void**>(0x142b19780); }

}  // namespace

// 0x14063dbb0 (slot 1): OnConnect - tell the game, log the character id.
void ZoneClientOnConnect(uint8_t* client, void* /*api*/) {
  game::Call<void (*)(void*)>(0x140430490)(GameClient());  // game: zone connected
  uint64_t characterId;
  uint64_t* id = game::Call<uint64_t* (*)(void*, uint64_t*)>(0x14063bc10)(game::Field<void*>(client, 8), &characterId);
  Log(0x1420dd730, *id);  // "RECEIVED=OnConnect %lld"
}

// 0x14063db80 (slot 6): OnChannelIsRoutable - logged only.
void ZoneClientOnChannelIsRoutable(uint8_t* /*client*/, void* /*api*/, int channel, bool isRoutable, bool flag) {
  Log(0x1420dd780, channel, static_cast<int>(isRoutable), flag);  // "RECEIVED=OnChannelIsRoutable channel=%d isRoutable=%d"
}

// 0x14063dc00 (slot 7): OnConnectionIsNotRoutable - logged only.
void ZoneClientOnConnectionIsNotRoutable(uint8_t* /*client*/, void* /*api*/) {
  Log(0x1420dd7b8);  // "RECEIVED=OnConnectionIsNotRoutable"
}

namespace {

// Bounds-checked read cursor used by the zone packet readers.
struct ZoneReader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(ZoneReader, failed) == 0x20);

// Channel-2 packet (opcode 0x79) built on the stack.
struct Channel2Packet {
  void** vtable;
  int opcode;
  int padding;
  uint8_t body[0x1A0];  // +0x10, ctor 0x1417f2640 / dtor 0x1417f2750
  uint8_t* bodyPtr;     // +0x1B0 (read by game client slot 73, 0x14040b750)
  int value;            // +0x1B8
};
static_assert(offsetof(Channel2Packet, bodyPtr) == 0x1B0);
static_assert(offsetof(Channel2Packet, value) == 0x1B8);

// Header-only view of an incoming zone packet passed to the game.
struct ZonePacketHeader {
  void** vtable;
  uint32_t opcode;
};

constexpr uint8_t kOpcodeF5 = 0xF5;
constexpr size_t kOpcodeF5Value = 0x2B0;  // float set by opcode 0xF5 (meaning not identified yet)

}  // namespace

// 0x14063de90 (slot 8): OnTunnelData - zone packets from the gateway.
// Channel 2 carries opcode 0x79 packets; opcode 0xF5 updates a float kept
// on the client; everything else goes to the game's packet handler (slot 42).
void ZoneClientOnTunnelData(uint8_t* client, void* /*api*/, int channel, const uint8_t* data, int length) {
  void* game = GameClient();
  if (channel == 2) {
    Channel2Packet packet;
    packet.opcode = 0x79;
    packet.vtable = reinterpret_cast<void**>(0x142064070);
    game::Call<void (*)(uint8_t*)>(0x1417f2640)(packet.body);
    packet.bodyPtr = packet.body;
    packet.value = *reinterpret_cast<int*>(0x142b319a8);
    packet.vtable = reinterpret_cast<void**>(0x1420dd668);
    ZoneReader reader{data, length, data, data + length, 0};
    game::Call<void (*)(ZoneReader*, int*)>(0x140357100)(&reader, &packet.value);
    game::Call<void (*)(uint8_t*, ZoneReader*)>(0x1403717d0)(packet.bodyPtr, &reader);
    using HandleFn = void (*)(void*, Channel2Packet*);
    reinterpret_cast<HandleFn>((*static_cast<void***>(game))[0x248 / 8])(game, &packet);
    game::Call<void (*)(uint8_t*)>(0x1417f2750)(packet.body);
    return;
  }
  ZonePacketHeader header{reinterpret_cast<void**>(0x1420633d0), 0};
  if (data) {
    const uint8_t* end = data + length;
    const uint8_t* cursor = data + 1;
    header.opcode = cursor > end ? 0 : data[0];
    if (header.opcode == kOpcodeF5) {
      bool failed = false;
      if (cursor > end) {
        failed = true;
        cursor = end;
      }
      if (cursor + 4 > end) return;
      float value = *reinterpret_cast<const float*>(cursor);
      if (failed) return;
      game::Field<float>(client, kOpcodeF5Value) = value;
      return;
    }
  }
  using HandleFn = void (*)(void*, int, ZonePacketHeader*, const uint8_t*, int);
  reinterpret_cast<HandleFn>((*static_cast<void***>(game))[0x150 / 8])(game, channel, &header, data, length);
}

// Slots 3, 4, 5, 9: `ret 0` - nothing to do.
void ZoneClientIgnore() {}

REBUILD_FUNCTION(ZoneClient_OnConnect, 0x14063dbb0, ZoneClientOnConnect);
REBUILD_FUNCTION(ZoneClient_OnChannelIsRoutable, 0x14063db80, ZoneClientOnChannelIsRoutable);
REBUILD_FUNCTION(ZoneClient_OnConnectionIsNotRoutable, 0x14063dc00, ZoneClientOnConnectionIsNotRoutable);
REBUILD_FUNCTION(ZoneClient_OnTunnelData, 0x14063de90, ZoneClientOnTunnelData);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnFailed, 0x14063de20, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnForcedLogout, 0x14063de40, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnLoginReply, 0x14063de60, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_Slot9, 0x14063de80, ZoneClientIgnore);

}  // namespace rebuild::game_net
