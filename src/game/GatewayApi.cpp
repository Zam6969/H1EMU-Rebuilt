// Gateway::ExternalGatewayApi: the client side of the gateway (zone) server
// connection. Each opcode is unserialized into a Gateway::ExternalPackets
// struct and handed to a member handler, which forwards it to the game's
// gateway listener at +0x1058.
#include <cstddef>
#include <cstdint>
#include <intrin.h>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"

namespace rebuild::game_net {
namespace {

constexpr size_t kListener = 0x1058;  // listener object (vtable callbacks below)
constexpr size_t kLog = 0xD40;
constexpr size_t kLogContext = 0x2D0;
constexpr uintptr_t kUnserializeFailedFormat = 0x1424be570;

// Gateway listener vtable slots.
enum ListenerSlot : size_t {
  kOnConnect = 0x08 / 8,
  kOnDisconnect = 0x10 / 8,
  kOnFailed = 0x18 / 8,
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
constexpr uintptr_t kVtPacketLogout = 0x1424be248;
constexpr uintptr_t kVtPacketForcedLogout = 0x1424be258;
constexpr uintptr_t kVtStringFixed64 = 0x142049d00;
constexpr uintptr_t kVtPacketLoginRequest = 0x1424be228;
constexpr uintptr_t kVtGatewayBasePacket = 0x1424be218;
constexpr uintptr_t kVtStringFixed32 = 0x14204a378;
constexpr uintptr_t kVtIStringFixed32 = 0x14204a358;
constexpr int kOpcodeLoginRequest = 1;

struct PacketLoginRequest : GatewayPacket {
  soeutil::StringFixed<32> ticket;      // +0x10
  soeutil::StringFixed<32> clientProtocol;  // +0x50
  soeutil::StringFixed<32> clientBuild;     // +0x90
  uint64_t characterId;                 // +0xD0
};
static_assert(offsetof(PacketLoginRequest, clientProtocol) == 0x50);
static_assert(offsetof(PacketLoginRequest, clientBuild) == 0x90);
static_assert(offsetof(PacketLoginRequest, characterId) == 0xD0);

// Login info stored by Init: character id and three strings.
constexpr size_t kCharacterId = 0xFC0;
constexpr size_t kTicket = 0xFC8;
constexpr size_t kClientProtocol = 0xFF8;
constexpr size_t kClientBuild = 0x1028;
constexpr uintptr_t kVtIStringFixed64 = 0x142049ce0;

struct PacketForcedLogout : GatewayPacket {
  soeutil::StringFixed<64> reason;
};
static_assert(offsetof(PacketForcedLogout, reason) == 0x10);
constexpr int kOpcodeLogout = 3;

// ClientServerCore::BaseApi vtable slots used here.
constexpr size_t kApiIsConnected = 0x50 / 8;
constexpr size_t kApiDisconnect = 0x60 / 8;  // (timeoutMs, forceOnTimeout)
constexpr size_t kLoginPending = 0x1061;     // bool: send login request on connect

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

template <typename Fn>
Fn ApiSlot(uint8_t* api, size_t slot) {
  return reinterpret_cast<Fn>((*reinterpret_cast<void***>(api))[slot]);
}

// The API's logger at +0xD40, tagged with the connection's log context.
void* LogContext(uint8_t* api) { return game::Field<void*>(game::Field<uint8_t*>(api, kLogContext), 0x250); }

template <typename... Args>
void LogInfo(uint8_t* api, const char* format, Args... args) {
  game::Call<void (*)(void*, void*, const char*, ...)>(0x14165c9a0)(api + kLog, LogContext(api), format, args...);
}

template <typename... Args>
void LogError(uint8_t* api, const char* format, Args... args) {
  game::Call<void (*)(void*, void*, const char*, ...)>(0x14165c7e0)(api + kLog, LogContext(api), format, args...);
}

void LogUnserializeFailed(uint8_t* api, int length) {
  LogError(api, reinterpret_cast<const char*>(kUnserializeFailedFormat), length);
}

void InvokeMember(uint8_t* api, const MemberFn* handler, GatewayPacket* packet) {
  reinterpret_cast<void (*)(uint8_t*, GatewayPacket*)>(handler->function)(api + handler->thisAdjust, packet);
}

constexpr size_t kPendingQueue = 0xD68;     // SoeUtil::DataQueue
constexpr size_t kGatewayLoggedIn = 0x1060;  // bool
constexpr size_t kLogRawPackets = 0x1062;    // bool: dump to RawPacketsOutgoing.txt
constexpr size_t kApiSendSlot = 0xA0 / 8;    // BaseApi::Send(data, length, reliable)

// A DataQueue entry: a ref-counted byte buffer. The ref-counted base sits at
// +0x18 and its {strong, weak} count block at +0x20.
struct QueuedPacket {
  void* unknown0;
  const uint8_t* data;
  int length;
  int padding;
  void** refCountedVtable;
  int* counts;
};
static_assert(offsetof(QueuedPacket, data) == 0x08);
static_assert(offsetof(QueuedPacket, length) == 0x10);
static_assert(offsetof(QueuedPacket, refCountedVtable) == 0x18);
static_assert(offsetof(QueuedPacket, counts) == 0x20);

// 0x14165ae90: SoeUtil::DataQueue::Pop (memory or disk backed).
QueuedPacket* DataQueuePop(uint8_t* queue) {
  return game::Call<QueuedPacket* (*)(uint8_t*)>(0x14165ae90)(queue);
}

void ReleaseQueuedPacket(QueuedPacket* entry) {
  int* counts = entry->counts;
  bool lastStrong = _InterlockedDecrement(reinterpret_cast<volatile long*>(&counts[0])) == 0;
  long weakBefore = _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&counts[1]), -1);
  if (weakBefore == 1 && counts) soeutil::Free(counts, 0x10);
  if (lastStrong) {
    void* base = &entry->refCountedVtable;
    reinterpret_cast<void (*)(void*)>(entry->refCountedVtable[1])(base);
  }
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

// 0x14162d340: handler for PacketLoginReply. Packets the game sent before
// the gateway login completed sit in a SoeUtil DataQueue at +0xD68; on
// login they are flushed (reliably) to the server, otherwise put back.
void GatewayOnLoginReply(uint8_t* api, const PacketLoginReply* packet) {
  game::Field<bool>(api, kGatewayLoggedIn) = packet->loggedIn;
  uint8_t* queue = api + kPendingQueue;
  for (QueuedPacket* entry = DataQueuePop(queue); entry; entry = DataQueuePop(queue)) {
    int length = entry->length;
    const uint8_t* data = length ? entry->data : nullptr;
    if (game::Field<bool>(api, kLogRawPackets)) {
      game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x14162d4e0)(api, data, length);
    }
    if (!game::Field<bool>(api, kGatewayLoggedIn)) {
      game::Call<bool (*)(uint8_t*, const uint8_t*, int)>(0x14165aff0)(queue, data, length);
    } else {
      using SendFn = void (*)(uint8_t*, const uint8_t*, int, bool);
      ApiSlot<SendFn>(api, kApiSendSlot)(api, data, length, true);
    }
    ReleaseQueuedPacket(entry);
  }
  if (void* listener = Listener(api)) {
    using Fn = void (*)(void*, uint8_t*, bool);
    ListenerCall<Fn>(listener, kOnLoginReply)(listener, api, game::Field<bool>(api, kGatewayLoggedIn));
  }
}

// 0x14162bd80: unserialize PacketForcedLogout (int32 length + reason text).
void GatewayDispatchForcedLogout(uint8_t* api, const uint8_t* data, int length, const MemberFn* handler) {
  PacketForcedLogout packet;
  packet.vtable = reinterpret_cast<void**>(kVtPacketForcedLogout);
  packet.reason.vtable = reinterpret_cast<void**>(kVtStringFixed64);
  packet.reason.data = soeutil::EmptyStringData();
  packet.reason.length = 0;
  packet.reason.capacity = 0;
  Reader in{data, data + length};
  ReadHeader(in, packet);
  int textLength = 0;
  bool ok;
  if (in.cursor + 4 > in.end) {
    in.failed = true;
    in.cursor = in.end;
    ok = true;  // a zero-length string is still read below
  } else {
    textLength = *reinterpret_cast<const int*>(in.cursor);
    in.cursor += 4;
    ok = textLength >= 0;
  }
  bool dispatched = false;
  if (ok && textLength <= static_cast<int>(in.end - in.cursor)) {
    soeutil::StringAssignN(&packet.reason, reinterpret_cast<const char*>(in.cursor), textLength);
    if (!in.failed) {
      reinterpret_cast<void (*)(uint8_t*, GatewayPacket*)>(handler->function)(api + handler->thisAdjust, &packet);
      dispatched = true;
    }
  }
  if (!dispatched) LogUnserializeFailed(api, length);
  // ~StringFixed<64> (inlined): back to the IStringFixed vtable, drop the buffer.
  packet.reason.vtable = reinterpret_cast<void**>(kVtIStringFixed64);
  soeutil::StringRelease(&packet.reason);
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

// 0x140467e80: IString::Write (int32 length + chars).
void StreamWriteString(soeutil::ByteStream* stream, const soeutil::IString* string) {
  int length = string->length;
  soeutil::StreamPut(stream, &length, 4);
  soeutil::StreamPut(stream, string->data, length);
}

// 0x14162c1e0: PacketLoginRequest::Serialize
void SerializeLoginRequest(const PacketLoginRequest* packet, soeutil::ByteStream** stream) {
  uint8_t header = static_cast<uint8_t>((packet->opcode & 0x1F) | (packet->channel << 5));
  soeutil::StreamPut(*stream, &header, 1);
  uint64_t characterId = packet->characterId;
  soeutil::StreamPut(*stream, &characterId, 8);
  StreamWriteString(*stream, &packet->ticket);
  StreamWriteString(*stream, &packet->clientProtocol);
  StreamWriteString(*stream, &packet->clientBuild);
}

void DestroyStringFixed32(soeutil::StringFixed<32>* string) {
  string->vtable = reinterpret_cast<void**>(kVtIStringFixed32);
  soeutil::StringRelease(string);
  string->data = soeutil::EmptyStringData();
  string->length = 0;
  string->capacity = 0;
  string->vtable = soeutil::IStringVtable();
}

// 0x14162cdd0: ~PacketLoginRequest
void DestroyLoginRequest(PacketLoginRequest* packet) {
  DestroyStringFixed32(&packet->clientBuild);
  DestroyStringFixed32(&packet->clientProtocol);
  DestroyStringFixed32(&packet->ticket);
  packet->vtable = reinterpret_cast<void**>(kVtGatewayBasePacket);
}

void InitStringFixed32(soeutil::StringFixed<32>* string) {
  string->vtable = reinterpret_cast<void**>(kVtStringFixed32);
  string->data = soeutil::EmptyStringData();
  string->length = 0;
  string->capacity = 0;
}

// 0x14162d5d0: send the gateway login request now if connected, otherwise
// remember to send it from OnConnect.
void GatewaySendLoginRequest(uint8_t* api) {
  if (!ApiSlot<bool (*)(uint8_t*)>(api, kApiIsConnected)(api)) {
    game::Field<bool>(api, kLoginPending) = true;
    return;
  }
  LogInfo(api, reinterpret_cast<const char*>(0x1424be498));  // "OnConnect, sending login packet."
  PacketLoginRequest packet;
  packet.opcode = kOpcodeLoginRequest;
  packet.channel = 0;
  packet.vtable = reinterpret_cast<void**>(kVtPacketLoginRequest);
  InitStringFixed32(&packet.ticket);
  InitStringFixed32(&packet.clientProtocol);
  InitStringFixed32(&packet.clientBuild);
  packet.characterId = game::Field<uint64_t>(api, kCharacterId);
  soeutil::StringAssignString(&packet.ticket, reinterpret_cast<soeutil::IString*>(api + kTicket));
  soeutil::StringAssignString(&packet.clientProtocol, reinterpret_cast<soeutil::IString*>(api + kClientProtocol));
  soeutil::StringAssignString(&packet.clientBuild, reinterpret_cast<soeutil::IString*>(api + kClientBuild));
  {
    soeutil::ScopedByteStream stream;
    SerializeLoginRequest(&packet, &stream.active);
    using SendFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
    ApiSlot<SendFn>(api, kApiSendSlot)(api, stream.Data(), stream.Size(), true);
    if (game::Field<bool>(api, 0x1063) && game::Field<void*>(api, 0xD28)) game::Field<bool>(api, 0xD38) = true;
  }
  DestroyLoginRequest(&packet);
}

// 0x14162c020: serialize a header-only packet and send it reliably, or queue
// it until the gateway login completes.
bool GatewaySendPacket(uint8_t* api, const GatewayPacket* packet) {
  soeutil::ScopedByteStream stream;
  uint8_t header = static_cast<uint8_t>((packet->opcode & 0x1F) | (packet->channel << 5));
  stream.Put(&header, 1);
  const uint8_t* data = stream.Data();
  int length = stream.Size();
  if (game::Field<bool>(api, kLogRawPackets)) {
    game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x14162d4e0)(api, data, length);
  }
  if (!game::Field<bool>(api, kGatewayLoggedIn)) {
    return game::Call<bool (*)(uint8_t*, const uint8_t*, int)>(0x14165aff0)(api + kPendingQueue, data, length);
  }
  using SendFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
  return ApiSlot<SendFn>(api, kApiSendSlot)(api, data, length, true);
}

// 0x14162d7d0: Logout(disconnectTimeoutMs, forceDisconnectOnTimeout).
void GatewayLogout(uint8_t* api, int disconnectTimeoutMs, bool forceDisconnectOnTimeout) {
  if (game::Field<bool>(api, kGatewayLoggedIn) && ApiSlot<bool (*)(uint8_t*)>(api, kApiIsConnected)(api)) {
    GatewayPacket packet{reinterpret_cast<void**>(kVtPacketLogout), kOpcodeLogout, 0};
    GatewaySendPacket(api, &packet);
    LogInfo(api, reinterpret_cast<const char*>(0x1424be4c0), disconnectTimeoutMs,
            static_cast<int>(forceDisconnectOnTimeout));  // "Logout sent to the server. ..."
    ApiSlot<void (*)(uint8_t*, int, bool)>(api, kApiDisconnect)(api, disconnectTimeoutMs, forceDisconnectOnTimeout);
    return;
  }
  LogInfo(api, reinterpret_cast<const char*>(0x1424be518));  // "Logout called but not connected, doing nothing."
}

// 0x14162d8a0 (vtable slot 16): OnConnect. Sends the deferred login request.
void GatewayOnConnect(uint8_t* api) {
  game::Field<bool>(api, kGatewayLoggedIn) = false;
  if (game::Field<bool>(api, kLoginPending)) {
    GatewaySendLoginRequest(api);
  }
  if (void* listener = Listener(api)) {
    ListenerCall<void (*)(void*, uint8_t*)>(listener, kOnConnect)(listener, api);
  }
}

// 0x14162d8e0 (vtable slot 17): OnDisconnect.
void GatewayOnDisconnect(uint8_t* api) {
  game::Field<bool>(api, kGatewayLoggedIn) = false;
  LogInfo(api, reinterpret_cast<const char*>(0x1424be488));  // "OnDisconnect."
  if (void* listener = Listener(api)) {
    ListenerCall<void (*)(void*, uint8_t*)>(listener, kOnDisconnect)(listener, api);
  }
}

// 0x14162d940 (vtable slot 15): OnFailed.
void GatewayOnFailed(uint8_t* api) {
  game::Field<bool>(api, kGatewayLoggedIn) = false;
  LogError(api, reinterpret_cast<const char*>(0x1424be478));  // "OnFailed."
  if (void* listener = Listener(api)) {
    ListenerCall<void (*)(void*, uint8_t*)>(listener, kOnFailed)(listener, api);
  }
}

REBUILD_FUNCTION(SoeUtil_WriteString, 0x140467e80, StreamWriteString);
REBUILD_FUNCTION(Gateway_SerializeLoginRequest, 0x14162c1e0, SerializeLoginRequest);
REBUILD_FUNCTION(Gateway_DestroyLoginRequest, 0x14162cdd0, DestroyLoginRequest);
REBUILD_FUNCTION(Gateway_SendLoginRequest, 0x14162d5d0, GatewaySendLoginRequest);
REBUILD_FUNCTION(Gateway_SendPacket, 0x14162c020, GatewaySendPacket);
REBUILD_FUNCTION(Gateway_Logout, 0x14162d7d0, GatewayLogout);
REBUILD_FUNCTION(Gateway_OnConnect, 0x14162d8a0, GatewayOnConnect);
REBUILD_FUNCTION(Gateway_OnDisconnect, 0x14162d8e0, GatewayOnDisconnect);
REBUILD_FUNCTION(Gateway_OnFailed, 0x14162d940, GatewayOnFailed);
REBUILD_FUNCTION(Gateway_OnLoginReply, 0x14162d340, GatewayOnLoginReply);
REBUILD_FUNCTION(Gateway_DispatchLoginReply, 0x14162bed0, GatewayDispatchLoginReply);
REBUILD_FUNCTION(Gateway_DispatchForcedLogout, 0x14162bd80, GatewayDispatchForcedLogout);
REBUILD_FUNCTION(Gateway_DispatchChannelIsRoutable, 0x14162bca0, GatewayDispatchChannelIsRoutable);
REBUILD_FUNCTION(Gateway_OnChannelIsRoutable, 0x14162d2c0, GatewayOnChannelIsRoutable);
REBUILD_FUNCTION(Gateway_OnConnectionIsNotRoutable, 0x14162d300, GatewayOnConnectionIsNotRoutable);
REBUILD_FUNCTION(Gateway_OnForcedLogout, 0x14162d320, GatewayOnForcedLogout);
REBUILD_FUNCTION(Gateway_OnTunnelPacket, 0x14162d460, GatewayOnTunnelPacket);

}  // namespace rebuild::game_net
