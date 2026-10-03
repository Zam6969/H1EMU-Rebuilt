// Login::ExternalLoginUdpApi unserializers: one per LoginUdp_11 reply.
// Each builds a Login::ExternalPackets struct on the stack, reads it with a
// bounds-checked cursor and, if nothing was short, calls the handler on the
// API's packet-handler subobject at +0xD40.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"

namespace rebuild::game_net {
namespace {

constexpr size_t kHandlerSubobject = 0xD40;

// Unserialize cursor passed to the per-packet Read functions.
struct LoginReader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(LoginReader, cursor) == 0x10);
static_assert(offsetof(LoginReader, end) == 0x18);
static_assert(offsetof(LoginReader, failed) == 0x20);

LoginReader MakeReader(const uint8_t* data, int length) { return {data, length, data, data + length, 0}; }

// SoeUtil::Array<unsigned char,0,1>: heap byte array.
struct ByteArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};
static_assert(sizeof(ByteArray) == 0x18);
constexpr uintptr_t kVtByteArray = 0x14204adc8;

void InitByteArray(ByteArray& array) {
  array.vtable = reinterpret_cast<void**>(kVtByteArray);
  array.data = nullptr;
  array.size = 0;
  array.capacity = 0;
}

// ~Array<unsigned char,0,1> (inlined).
void DestroyByteArray(ByteArray& array) {
  array.vtable = reinterpret_cast<void**>(kVtByteArray);
  array.size = 0;
  if (soeutil::ThreadAllocatorCount() == 0) {
    soeutil::FreeArray(array.data);
  } else {
    soeutil::MemoryFree(array.data, 1);
  }
}

// SoeUtil::List<T,-1>: {vtable, head, tail, count}.
struct PacketList {
  void** vtable;
  void* head;
  void* tail;
  int count;
  int padding;
};
static_assert(sizeof(PacketList) == 0x20);

using Handler = void (*)(uint8_t*, void*);

uint8_t* HandlerThis(uint8_t* api) { return api ? api + kHandlerSubobject : nullptr; }

// Invalid id sentinels (SoeUtil strong-typed 64-bit ids).
uint64_t InvalidCharacterId() { return *reinterpret_cast<uint64_t*>(0x143c78130); }
uint64_t InvalidServerId() { return *reinterpret_cast<uint64_t*>(0x143c78118); }

struct LoginPacket {
  void** vtable;
  int opcode;
  int padding;
};

struct PacketForcedDisconnect : LoginPacket {
  int reason;
};
static_assert(offsetof(PacketForcedDisconnect, reason) == 0x10);

struct PacketCharacterCreateReply : LoginPacket {
  int status;
  uint64_t characterId;
};
static_assert(offsetof(PacketCharacterCreateReply, characterId) == 0x18);

struct PacketCharacterLoginReply : LoginPacket {
  uint64_t characterId;
  uint64_t serverId;
  int status;
  ByteArray* payload;
  ByteArray payloadStorage;
};
static_assert(offsetof(PacketCharacterLoginReply, status) == 0x20);
static_assert(offsetof(PacketCharacterLoginReply, payload) == 0x28);
static_assert(offsetof(PacketCharacterLoginReply, payloadStorage) == 0x30);

struct PacketCharacterDeleteReply : LoginPacket {
  uint64_t characterId;
  int status;
  ByteArray* payload;
  ByteArray payloadStorage;
};
static_assert(offsetof(PacketCharacterDeleteReply, status) == 0x18);
static_assert(offsetof(PacketCharacterDeleteReply, payload) == 0x20);

struct PacketCharacterTransferReply : LoginPacket {
  uint64_t characterId;
  uint64_t serverId;
  ByteArray* payload;
  int status;
  ByteArray payloadStorage;
};
static_assert(offsetof(PacketCharacterTransferReply, payload) == 0x20);
static_assert(offsetof(PacketCharacterTransferReply, status) == 0x28);
static_assert(offsetof(PacketCharacterTransferReply, payloadStorage) == 0x30);

struct PacketTunnelAppPacketServerToClient : LoginPacket {
  uint64_t serverId;
  ByteArray* payload;
  ByteArray payloadStorage;
};
static_assert(offsetof(PacketTunnelAppPacketServerToClient, payload) == 0x18);

struct PacketCharacterSelectInfoReply : LoginPacket {
  int status;
  bool canBypassServerLock;
  PacketList* characters;
  ByteArray unusedArray;
  PacketList characterStorage;  // List<Login::EntityDetails>
};
static_assert(offsetof(PacketCharacterSelectInfoReply, characters) == 0x18);
static_assert(offsetof(PacketCharacterSelectInfoReply, unusedArray) == 0x20);
static_assert(offsetof(PacketCharacterSelectInfoReply, characterStorage) == 0x38);

struct PacketServerListReply : LoginPacket {
  PacketList servers;  // List<Login::ClientGameServerData>
};
static_assert(offsetof(PacketServerListReply, servers) == 0x10);

template <typename Packet>
bool ReadWith(uintptr_t readFn, Packet* packet, LoginReader* reader) {
  game::Call<void (*)(Packet*, LoginReader*)>(readFn)(packet, reader);
  return static_cast<uint8_t>(reader->failed) == 0;
}

}  // namespace

// 0x141633b30: PacketForcedDisconnect {header, int32 reason}.
void LoginDispatchForcedDisconnect(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketForcedDisconnect packet;
  packet.vtable = reinterpret_cast<void**>(0x1424bfc98);
  const uint8_t* end = data + length;
  const uint8_t* cursor;
  if (end < data + 1) {
    packet.opcode = 0;
    cursor = end;
  } else {
    packet.opcode = static_cast<int8_t>(data[0]);
    cursor = data + 1;
  }
  if (cursor + 4 <= end) {
    packet.reason = *reinterpret_cast<const int*>(cursor);
    if (data + 1 <= end) handler(HandlerThis(api), &packet);
  }
}

// 0x141633080: PacketCharacterCreateReply {header, int32 status, u64 character id}.
void LoginDispatchCharacterCreateReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketCharacterCreateReply packet;
  packet.vtable = reinterpret_cast<void**>(0x1424bfcb8);
  packet.characterId = InvalidCharacterId();
  const uint8_t* end = data + length;
  const uint8_t* afterHeader;
  if (end < data + 1) {
    packet.opcode = 0;
    afterHeader = end;
  } else {
    packet.opcode = static_cast<int8_t>(data[0]);
    afterHeader = data + 1;
  }
  const uint8_t* afterStatus = afterHeader + 4;
  const uint8_t* cursor;
  if (end < afterStatus) {
    packet.status = 0;
    cursor = end;
  } else {
    packet.status = *reinterpret_cast<const int*>(afterHeader);
    cursor = afterStatus;
  }
  if (cursor + 8 <= end) {
    packet.characterId = *reinterpret_cast<const uint64_t*>(cursor);
    if (afterStatus <= end && data + 1 <= end) handler(HandlerThis(api), &packet);
  }
}

// 0x1416334b0: PacketCharacterLoginReply (read by 0x141636d50).
void LoginDispatchCharacterLoginReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketCharacterLoginReply packet;
  packet.opcode = 8;
  packet.vtable = reinterpret_cast<void**>(0x1424bfcd8);
  packet.characterId = InvalidCharacterId();
  packet.serverId = InvalidServerId();
  packet.status = 0;
  packet.payload = &packet.payloadStorage;
  InitByteArray(packet.payloadStorage);
  LoginReader reader = MakeReader(data, length);
  if (ReadWith(0x141636d50, &packet, &reader)) handler(HandlerThis(api), &packet);
  DestroyByteArray(packet.payloadStorage);
}

// 0x141633260: PacketCharacterDeleteReply (read by 0x141636c30).
void LoginDispatchCharacterDeleteReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketCharacterDeleteReply packet;
  packet.opcode = 10;
  packet.vtable = reinterpret_cast<void**>(0x1424bfcf8);
  packet.characterId = InvalidCharacterId();
  packet.status = 0;
  packet.payload = &packet.payloadStorage;
  InitByteArray(packet.payloadStorage);
  LoginReader reader = MakeReader(data, length);
  if (ReadWith(0x141636c30, &packet, &reader)) handler(HandlerThis(api), &packet);
  DestroyByteArray(packet.payloadStorage);
}

// 0x141633990: PacketCharacterTransferServerReply (read by 0x141636f40).
void LoginDispatchCharacterTransferReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketCharacterTransferReply packet;
  packet.opcode = 0x13;
  packet.vtable = reinterpret_cast<void**>(0x1424bfdd8);
  packet.characterId = InvalidCharacterId();
  packet.serverId = InvalidServerId();
  packet.payload = &packet.payloadStorage;
  packet.status = 0;
  InitByteArray(packet.payloadStorage);
  LoginReader reader = MakeReader(data, length);
  if (ReadWith(0x141636f40, &packet, &reader)) handler(HandlerThis(api), &packet);
  DestroyByteArray(packet.payloadStorage);
}

// 0x141634280: PacketTunnelAppPacketServerToClient (read by 0x141637280).
void LoginDispatchTunnelAppPacket(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketTunnelAppPacketServerToClient packet;
  packet.opcode = 0x11;
  packet.vtable = reinterpret_cast<void**>(0x1424bfd60);
  packet.serverId = InvalidServerId();
  packet.payload = &packet.payloadStorage;
  InitByteArray(packet.payloadStorage);
  LoginReader reader = MakeReader(data, length);
  if (ReadWith(0x141637280, &packet, &reader)) handler(HandlerThis(api), &packet);
  DestroyByteArray(packet.payloadStorage);
}

// 0x141633c40: PacketLoginReply (0x1E0 bytes; ctor 0x14163b9b0, read
// 0x141637090, dtor 0x14163c2f0).
void LoginDispatchLoginReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  alignas(8) uint8_t packet[0x1E0];
  game::Call<void (*)(void*)>(0x14163b9b0)(packet);
  LoginReader reader = MakeReader(data, length);
  game::Call<void (*)(void*, LoginReader*)>(0x141637090)(packet, &reader);
  if (static_cast<uint8_t>(reader.failed) == 0) handler(HandlerThis(api), packet);
  game::Call<void (*)(void*)>(0x14163c2f0)(packet);
}

// 0x141633720: PacketCharacterSelectInfoReply (read by 0x14163ab60, which
// returns true on failure).
void LoginDispatchCharacterSelectInfoReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketCharacterSelectInfoReply packet;
  packet.opcode = 0xC;
  packet.vtable = reinterpret_cast<void**>(0x1424bfd40);
  packet.status = 0;
  packet.canBypassServerLock = false;
  packet.characters = &packet.characterStorage;
  InitByteArray(packet.unusedArray);
  packet.characterStorage = {reinterpret_cast<void**>(0x1424bfd18), nullptr, nullptr, 0, 0};
  bool failed = game::Call<bool (*)(const uint8_t*, int, void*)>(0x14163ab60)(data, length, &packet);
  if (!failed) handler(HandlerThis(api), &packet);
  packet.characterStorage.vtable = reinterpret_cast<void**>(0x1424bfd18);
  while (packet.characterStorage.head) {
    game::Call<void (*)(PacketList*, void*)>(0x14163f100)(&packet.characterStorage, packet.characterStorage.head);
  }
  DestroyByteArray(packet.unusedArray);
}

// 0x141633de0: PacketServerListReply. The header byte is read inline, the
// server list by 0x140adbaf0.
void LoginDispatchServerListReply(uint8_t* api, const uint8_t* data, int length, Handler handler) {
  PacketServerListReply packet;
  packet.vtable = reinterpret_cast<void**>(0x1424bfda8);
  packet.servers = {reinterpret_cast<void**>(0x1424bfd80), nullptr, nullptr, 0, 0};
  LoginReader reader{data, length, data + 1, data + length, 0};
  bool headerOk = reader.cursor <= reader.end;
  if (headerOk) {
    packet.opcode = static_cast<int8_t>(data[0]);
  } else {
    packet.opcode = 0;
    reader.cursor = reader.end;
  }
  reader.failed = headerOk ? 0 : 1;
  game::Call<void (*)(LoginReader*, PacketList*)>(0x140adbaf0)(&reader, &packet.servers);
  if (static_cast<uint8_t>(reader.failed) == 0) handler(HandlerThis(api), &packet);
  packet.servers.vtable = reinterpret_cast<void**>(0x1424bfd80);
  while (packet.servers.head) {
    game::Call<void (*)(PacketList*, void*)>(0x140adcbf0)(&packet.servers, packet.servers.head);
  }
}

REBUILD_FUNCTION(Login_DispatchForcedDisconnect, 0x141633b30, LoginDispatchForcedDisconnect);
REBUILD_FUNCTION(Login_DispatchCharacterCreateReply, 0x141633080, LoginDispatchCharacterCreateReply);
REBUILD_FUNCTION(Login_DispatchCharacterLoginReply, 0x1416334b0, LoginDispatchCharacterLoginReply);
REBUILD_FUNCTION(Login_DispatchCharacterDeleteReply, 0x141633260, LoginDispatchCharacterDeleteReply);
REBUILD_FUNCTION(Login_DispatchCharacterTransferReply, 0x141633990, LoginDispatchCharacterTransferReply);
REBUILD_FUNCTION(Login_DispatchTunnelAppPacket, 0x141634280, LoginDispatchTunnelAppPacket);
REBUILD_FUNCTION(Login_DispatchLoginReply, 0x141633c40, LoginDispatchLoginReply);
REBUILD_FUNCTION(Login_DispatchCharacterSelectInfoReply, 0x141633720, LoginDispatchCharacterSelectInfoReply);
REBUILD_FUNCTION(Login_DispatchServerListReply, 0x141633de0, LoginDispatchServerListReply);

}  // namespace rebuild::game_net
