// Gateway::ExternalGatewayApi: the client side of the gateway (zone) server
// connection. Each opcode is unserialized into a Gateway::ExternalPackets
// struct and handed to a member handler, which forwards it to the game's
// gateway listener at +0x1058.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::game_net {
namespace {

constexpr size_t kListener = 0x1058;  // listener object (vtable callbacks below)
constexpr size_t kLog = 0xD40;
constexpr size_t kLogContext = 0x2D0;
constexpr uintptr_t kUnserializeFailedFormat = 0x1424be570;

// Gateway listener vtable slots.
enum ListenerSlot : size_t {
  kOnLoginReply = 0x20 / 8,
  kOnForcedLogout = 0x28 / 8,
  kOnChannelIsRoutable = 0x30 / 8,
  kOnConnectionIsNotRoutable = 0x38 / 8,
  kOnTunnelData = 0x40 / 8,
};

// Gateway::ExternalPackets::BasePacket: {vtable, opcode (low 5 bits), channel (high 3 bits)}.
struct GatewayPacket {
  void** vtable;
  int opcode;
  int channel;
};
static_assert(sizeof(GatewayPacket) == 0x10);

struct PacketLoginReply : GatewayPacket {
  bool loggedIn;
};
static_assert(offsetof(PacketLoginReply, loggedIn) == 0x10);

struct PacketChannelIsRoutable : GatewayPacket {
  bool isRoutable;
  bool flag;  // second bool, passed straight through to the listener
};
static_assert(offsetof(PacketChannelIsRoutable, isRoutable) == 0x10);
static_assert(offsetof(PacketChannelIsRoutable, flag) == 0x11);

constexpr uintptr_t kVtPacketLoginReply = 0x1424be238;
constexpr uintptr_t kVtPacketChannelIsRoutable = 0x1424be268;

// MSVC pointer-to-member passed by HandlePacket: {function, this adjustment}.
struct MemberFn {
  uintptr_t function;
  int thisAdjust;
  int padding;
};

// Bounds-checked byte reader matching the game's inline unserialize code:
// a short read yields 0 and pins the cursor at the end.
struct Reader {
  const uint8_t* cursor;
  const uint8_t* end;
  bool failed = false;
  uint8_t Byte() {
    if (cursor + 1 > end) {
      failed = true;
      cursor = end;
      return 0;
    }
    return *cursor++;
  }
};

void* Listener(uint8_t* api) { return game::Field<void*>(api, kListener); }

template <typename Fn>
Fn ListenerCall(void* listener, size_t slot) {
  return reinterpret_cast<Fn>((*static_cast<void***>(listener))[slot]);
}

void LogUnserializeFailed(uint8_t* api, int length) {
  using LogFn = void (*)(void*, void*, const char*, ...);
  game::Call<LogFn>(0x14165c7e0)(api + kLog, game::Field<void*>(game::Field<uint8_t*>(api, kLogContext), 0x250),
                                 reinterpret_cast<const char*>(kUnserializeFailedFormat), length);
}

void InvokeMember(uint8_t* api, const MemberFn* handler, GatewayPacket* packet) {
  reinterpret_cast<void (*)(uint8_t*, GatewayPacket*)>(handler->function)(api + handler->thisAdjust, packet);
}

void ReadHeader(Reader& in, GatewayPacket& packet) {
  uint8_t header = in.Byte();
  packet.channel = header >> 5;
  packet.opcode = header & 0x1F;
}

}  // namespace

// 0x14162bed0: unserialize PacketLoginReply and call the handler.
void GatewayDispatchLoginReply(uint8_t* api, const uint8_t* data, int length, const MemberFn* handler) {
  PacketLoginReply packet{};
  packet.vtable = reinterpret_cast<void**>(kVtPacketLoginReply);
  Reader in{data, data + length};
  ReadHeader(in, packet);
  packet.loggedIn = in.Byte() != 0;
  if (!in.failed) {
    InvokeMember(api, handler, &packet);
    return;
  }
  LogUnserializeFailed(api, length);
}

// 0x14162bca0: unserialize PacketChannelIsRoutable and call the handler.
void GatewayDispatchChannelIsRoutable(uint8_t* api, const uint8_t* data, int length, const MemberFn* handler) {
  PacketChannelIsRoutable packet{};
  packet.vtable = reinterpret_cast<void**>(kVtPacketChannelIsRoutable);
  Reader in{data, data + length};
  ReadHeader(in, packet);
  packet.isRoutable = in.Byte() != 0;
  packet.flag = in.Byte() != 0;
  if (!in.failed) {
    InvokeMember(api, handler, &packet);
    return;
  }
  LogUnserializeFailed(api, length);
}

// 0x14162d2c0: handler for PacketChannelIsRoutable.
void GatewayOnChannelIsRoutable(uint8_t* api, const PacketChannelIsRoutable* packet) {
  if (void* listener = Listener(api)) {
    using Fn = void (*)(void*, uint8_t*, int, bool, bool);
    ListenerCall<Fn>(listener, kOnChannelIsRoutable)(listener, api, packet->channel, packet->isRoutable,
                                                     packet->flag);
  }
}

// 0x14162d300: handler for opcode 8 (connection is not routable; header only).
void GatewayOnConnectionIsNotRoutable(uint8_t* api, const GatewayPacket* /*packet*/) {
  if (void* listener = Listener(api)) {
    ListenerCall<void (*)(void*, uint8_t*)>(listener, kOnConnectionIsNotRoutable)(listener, api);
  }
}

// 0x14162d320: handler for PacketForcedLogout (the reason string is unused).
void GatewayOnForcedLogout(uint8_t* api, const GatewayPacket* /*packet*/) {
  if (void* listener = Listener(api)) {
    ListenerCall<void (*)(void*, uint8_t*)>(listener, kOnForcedLogout)(listener, api);
  }
}

// 0x14162d460: opcode 5, a tunnelled application packet. The payload follows
// the header byte; the channel is the header's top 3 bits.
void GatewayOnTunnelPacket(uint8_t* api, const uint8_t* data, int length) {
  void* listener = Listener(api);
  if (!listener) return;
  const uint8_t* end = data + length;
  const uint8_t* payload = data + 1;
  uint8_t header;
  if (end < payload) {
    header = 0;
    payload = end;
  } else {
    header = data[0];
  }
  using Fn = void (*)(void*, uint8_t*, int, const uint8_t*, int);
  ListenerCall<Fn>(listener, kOnTunnelData)(listener, api, header >> 5, data + static_cast<int>(payload - data),
                                            static_cast<int>(end - payload));
}

REBUILD_FUNCTION(Gateway_DispatchLoginReply, 0x14162bed0, GatewayDispatchLoginReply);
REBUILD_FUNCTION(Gateway_DispatchChannelIsRoutable, 0x14162bca0, GatewayDispatchChannelIsRoutable);
REBUILD_FUNCTION(Gateway_OnChannelIsRoutable, 0x14162d2c0, GatewayOnChannelIsRoutable);
REBUILD_FUNCTION(Gateway_OnConnectionIsNotRoutable, 0x14162d300, GatewayOnConnectionIsNotRoutable);
REBUILD_FUNCTION(Gateway_OnForcedLogout, 0x14162d320, GatewayOnForcedLogout);
REBUILD_FUNCTION(Gateway_OnTunnelPacket, 0x14162d460, GatewayOnTunnelPacket);

}  // namespace rebuild::game_net
