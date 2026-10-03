// Login::ExternalLoginUdpApi packet handlers. HandlePacket unserializes each
// LoginUdp_11 reply into a Login::ExternalPackets struct and calls one of
// these, which forward the fields to the game's login listener at +0x10.
// Status codes follow the SOE convention: 1 = success.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"

namespace rebuild::game_net {
namespace {

constexpr size_t kLoggedIn = 0x0C;  // bool
constexpr size_t kListener = 0x10;  // login listener object

// Login listener vtable slots.
enum LoginListenerSlot : size_t {
  kOnLoginReply = 0x20 / 8,
  kOnForceDisconnect = 0x30 / 8,
  kOnCharacterCreateReply = 0x38 / 8,
  kOnCharacterLoginReply = 0x40 / 8,
  kOnCharacterDeleteReply = 0x48 / 8,
  kOnCharacterSelectInfoReply = 0x50 / 8,
  kOnTunnelAppPacket = 0x58 / 8,
  kOnServerListReply = 0x60 / 8,
  kOnServerUpdate = 0x68 / 8,
  kOnCharacterTransferReply = 0x70 / 8,
};

// The API's own vtable slot 5, called when a login reply says "not logged in".
constexpr size_t kApiOnLoginFailed = 0x28 / 8;

constexpr int kStatusSuccess = 1;

void* Listener(uint8_t* api) { return game::Field<void*>(api, kListener); }

template <typename Fn>
Fn Slot(void* object, size_t slot) {
  return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot]);
}

template <typename T>
T& At(const uint8_t* packet, size_t offset) {
  return *reinterpret_cast<T*>(const_cast<uint8_t*>(packet) + offset);
}

}  // namespace

// 0x14163dd30: LoginReply. Layout after the 0x10-byte packet header:
// +0x10 loggedIn, +0x14 status, +0x18 resultCode, +0x1C isMember,
// +0x1D isInternal, then string/array members whose data fields the
// listener receives (+0x28, +0x48, +0x50, +0x58, +0x68).
void LoginOnLoginReply(uint8_t* api, const uint8_t* packet) {
  bool loggedIn = At<bool>(packet, 0x10);
  game::Field<bool>(api, kLoggedIn) = loggedIn;
  void* listener = Listener(api);
  if (!listener) return;
  using Fn = void (*)(void*, uint8_t*, bool, int, bool, bool, uint64_t, uint64_t, uint64_t, int, uint64_t,
                      uint64_t);
  Slot<Fn>(listener, kOnLoginReply)(listener, api, loggedIn, At<int>(packet, 0x14), At<bool>(packet, 0x1C),
                                    At<bool>(packet, 0x1D), At<uint64_t>(packet, 0x28), At<uint64_t>(packet, 0x48),
                                    At<uint64_t>(packet, 0x50), At<int>(packet, 0x18), At<uint64_t>(packet, 0x58),
                                    At<uint64_t>(packet, 0x68));
  if (!game::Field<bool>(api, kLoggedIn)) {
    Slot<void (*)(uint8_t*)>(api, kApiOnLoginFailed)(api);
  }
}

// 0x14163dd00: ForceDisconnect (+0x10 reason).
void LoginOnForceDisconnect(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  Slot<void (*)(void*, uint8_t*, int)>(listener, kOnForceDisconnect)(listener, api, At<int>(packet, 0x10));
  game::Field<bool>(api, kLoggedIn) = false;
}

// 0x14163dba0: CharacterCreateReply (+0x10 status, +0x18 character id).
void LoginOnCharacterCreateReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  int status = At<int>(packet, 0x10);
  using Fn = void (*)(void*, uint8_t*, bool, int, const void*);
  Slot<Fn>(listener, kOnCharacterCreateReply)(listener, api, status == kStatusSuccess, status, packet + 0x18);
}

// 0x14163dc20: CharacterLoginReply (+0x10 character id, +0x20 status, +0x28 payload).
void LoginOnCharacterLoginReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  int status = At<int>(packet, 0x20);
  using Fn = void (*)(void*, uint8_t*, const void*, bool, int, uint64_t);
  Slot<Fn>(listener, kOnCharacterLoginReply)(listener, api, packet + 0x10, status == kStatusSuccess, status,
                                             At<uint64_t>(packet, 0x28));
}

// 0x14163dbe0: CharacterDeleteReply (+0x10 character id, +0x18 status, +0x20 payload).
void LoginOnCharacterDeleteReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  int status = At<int>(packet, 0x18);
  using Fn = void (*)(void*, uint8_t*, const void*, bool, int, uint64_t);
  Slot<Fn>(listener, kOnCharacterDeleteReply)(listener, api, packet + 0x10, status == kStatusSuccess, status,
                                              At<uint64_t>(packet, 0x20));
}

// 0x14163dc60: CharacterSelectInfoReply (+0x10 status, +0x14 canBypassServerLock, +0x18 characters).
void LoginOnCharacterSelectInfoReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  int status = At<int>(packet, 0x10);
  using Fn = void (*)(void*, uint8_t*, bool, uint64_t, int, bool);
  Slot<Fn>(listener, kOnCharacterSelectInfoReply)(listener, api, status == kStatusSuccess,
                                                  At<uint64_t>(packet, 0x18), status, At<bool>(packet, 0x14));
}

// 0x14163ddc0: ServerListReply (+0x10 server list).
void LoginOnServerListReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  Slot<void (*)(void*, uint8_t*, const void*)>(listener, kOnServerListReply)(listener, api, packet + 0x10);
}

// 0x14163dde0: ServerUpdate (+0x10 server entry).
void LoginOnServerUpdate(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  Slot<void (*)(void*, uint8_t*, const void*)>(listener, kOnServerUpdate)(listener, api, packet + 0x10);
}

// 0x14163de00: TunnelAppPacketServerToClient (+0x10 server id, +0x18 payload).
// The 8-byte id is copied and passed by address.
void LoginOnTunnelAppPacket(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  uint64_t serverId = At<uint64_t>(packet, 0x10);
  using Fn = void (*)(void*, uint8_t*, const uint64_t*, uint64_t);
  Slot<Fn>(listener, kOnTunnelAppPacket)(listener, api, &serverId, At<uint64_t>(packet, 0x18));
}

// 0x14163dca0: CharacterTransferReply (+0x10 character id, +0x18 server id,
// +0x20 payload, +0x28 status).
void LoginOnCharacterTransferReply(uint8_t* api, const uint8_t* packet) {
  void* listener = Listener(api);
  if (!listener) return;
  int status = At<int>(packet, 0x28);
  uint64_t serverId = At<uint64_t>(packet, 0x18);
  using Fn = void (*)(void*, uint8_t*, const void*, bool, const uint64_t*, int, uint64_t);
  Slot<Fn>(listener, kOnCharacterTransferReply)(listener, api, packet + 0x10, status == kStatusSuccess, &serverId,
                                                status, At<uint64_t>(packet, 0x20));
}

// ---- Outgoing requests. These run on the API's request subobject (+0xD40),
// whose +8 selects the transport: 0 = UDP (send through the BaseApi at -0xD40),
// 1 = the alternate transport at -0xAE0 (also given the request's name).

namespace {

struct RequestByteArray {  // SoeUtil::Array<unsigned char,0,1>
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};
constexpr uintptr_t kVtRequestByteArray = 0x14204adc8;

void DestroyRequestArray(RequestByteArray& array) {
  array.vtable = reinterpret_cast<void**>(kVtRequestByteArray);
  array.size = 0;
  if (soeutil::ThreadAllocatorCount() == 0) {
    soeutil::FreeArray(array.data);
  } else {
    soeutil::MemoryFree(array.data, 1);
  }
}

struct RequestHeader {
  void** vtable;
  int opcode;
  int padding;
};

int Transport(uint8_t* requests) { return game::Field<int>(requests, 8); }

template <typename Packet>
void SendRequest(uint8_t* requests, Packet* packet, uintptr_t udpSend, uintptr_t altSend, uintptr_t namePointer) {
  int transport = Transport(requests);
  if (transport == 0) {
    game::Call<void (*)(uint8_t*, Packet*)>(udpSend)(requests - 0xD40, packet);
  } else if (transport == 1) {
    const char* name = *reinterpret_cast<const char**>(namePointer);
    game::Call<void (*)(uint8_t*, Packet*, const char*)>(altSend)(requests - 0xAE0, packet, name);
  }
}

}  // namespace

// 0x14163d900: CharacterDeleteRequest(characterId)
void LoginRequestCharacterDelete(uint8_t* requests, const uint64_t* characterId) {
  struct : RequestHeader {
    uint64_t characterId;
  } packet;
  packet.opcode = 9;
  packet.vtable = reinterpret_cast<void**>(0x1424bfce8);
  packet.characterId = *characterId;
  SendRequest(requests, &packet, 0x141635270, 0x1416344f0, 0x142ab6930);  // "CharacterDeleteRequest"
}

// 0x14163db00: CharacterSelectInfoRequest()
void LoginRequestCharacterSelectInfo(uint8_t* requests) {
  RequestHeader packet;
  packet.opcode = 0xB;
  packet.vtable = reinterpret_cast<void**>(0x1424bfd08);
  SendRequest(requests, &packet, 0x1416355b0, 0x1416347f0, 0x142ab6940);  // "CharacterSelectInfoRequest"
}

// 0x14163e310: CharacterLoginRequest(characterId, payload, serverId)
void LoginRequestCharacterLogin(uint8_t* requests, const uint64_t* characterId, RequestByteArray* payload,
                                const uint64_t* serverId) {
  struct : RequestHeader {
    uint64_t characterId;
    uint64_t serverId;
    RequestByteArray* payload;
    RequestByteArray payloadStorage;
  } packet;
  packet.opcode = 7;
  packet.vtable = reinterpret_cast<void**>(0x1424bfcc8);
  packet.payloadStorage = {reinterpret_cast<void**>(kVtRequestByteArray), nullptr, 0, 0};
  packet.characterId = *characterId;
  packet.payload = payload;
  packet.serverId = *serverId;
  SendRequest(requests, &packet, 0x141635410, 0x141634650, 0x142ab6920);  // "CharacterLoginRequest"
  DestroyRequestArray(packet.payloadStorage);
}

// 0x14163d5a0: CharacterCreateRequest(payload, serverId) - the payload is copied.
void LoginRequestCharacterCreate(uint8_t* requests, const RequestByteArray* payload, const uint64_t* serverId) {
  struct : RequestHeader {
    uint64_t serverId;
    RequestByteArray payload;
  } packet;
  packet.opcode = 5;
  packet.vtable = reinterpret_cast<void**>(0x1424bfca8);
  packet.serverId = *reinterpret_cast<uint64_t*>(0x143c78118);  // invalid server id
  packet.payload = {reinterpret_cast<void**>(kVtRequestByteArray), nullptr, 0, 0};
  if (&packet.payload != payload) {
    packet.payload.size = 0;
    const uint8_t* bytes = payload->size > 0 ? payload->data : nullptr;
    int count = payload->size > 0 ? payload->size : 0;
    game::Call<void (*)(RequestByteArray*, const uint8_t*, int)>(0x140313f70)(&packet.payload, bytes, count);  // Assign
  }
  packet.serverId = *serverId;
  SendRequest(requests, &packet, 0x1416350d0, 0x141634370, 0x142ab6910);  // "CharacterCreateRequest"
  DestroyRequestArray(packet.payload);
}

REBUILD_FUNCTION(Login_OnLoginReply, 0x14163dd30, LoginOnLoginReply);
REBUILD_FUNCTION(Login_OnForceDisconnect, 0x14163dd00, LoginOnForceDisconnect);
REBUILD_FUNCTION(Login_OnCharacterCreateReply, 0x14163dba0, LoginOnCharacterCreateReply);
REBUILD_FUNCTION(Login_OnCharacterLoginReply, 0x14163dc20, LoginOnCharacterLoginReply);
REBUILD_FUNCTION(Login_OnCharacterDeleteReply, 0x14163dbe0, LoginOnCharacterDeleteReply);
REBUILD_FUNCTION(Login_OnCharacterSelectInfoReply, 0x14163dc60, LoginOnCharacterSelectInfoReply);
REBUILD_FUNCTION(Login_OnServerListReply, 0x14163ddc0, LoginOnServerListReply);
REBUILD_FUNCTION(Login_OnServerUpdate, 0x14163dde0, LoginOnServerUpdate);
REBUILD_FUNCTION(Login_OnTunnelAppPacket, 0x14163de00, LoginOnTunnelAppPacket);
REBUILD_FUNCTION(Login_RequestCharacterDelete, 0x14163d900, LoginRequestCharacterDelete);
REBUILD_FUNCTION(Login_RequestCharacterSelectInfo, 0x14163db00, LoginRequestCharacterSelectInfo);
REBUILD_FUNCTION(Login_RequestCharacterLogin, 0x14163e310, LoginRequestCharacterLogin);
REBUILD_FUNCTION(Login_RequestCharacterCreate, 0x14163d5a0, LoginRequestCharacterCreate);
REBUILD_FUNCTION(Login_OnCharacterTransferReply, 0x14163dca0, LoginOnCharacterTransferReply);

}  // namespace rebuild::game_net
