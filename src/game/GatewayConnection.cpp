// The game's zone-server connection wrapper (0x80 bytes, built by
// 0x14063b470): owns the Gateway::ExternalGatewayApi (+0x00) and forwards to
// it. The zone client object (vtable 0x1420dd6e0, constructed at 0x14063cc10)
// owns one of these and registers itself as the gateway listener.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Mutex.h"

namespace rebuild::game_net {
namespace {

uint8_t* Api(uint8_t** wrapper) { return *wrapper; }

template <typename Fn>
Fn ApiVirtual(uint8_t* api, size_t slot) {
  return reinterpret_cast<Fn>((*reinterpret_cast<void***>(api))[slot]);
}

}  // namespace

// Fields after the api pointer (constructor 0x14063b470).
struct GatewayConnection {
  uint8_t* api;              // +0x00, Gateway::ExternalGatewayApi (0x1068 bytes, own manager)
  uint64_t createdMs;        // +0x08
  void** listVtable;         // +0x10 (0x1420dd230)
  void* listHead;            // +0x18
  void* listTail;            // +0x20
  int listCount;             // +0x28
  int padding;
  bool flag;                 // +0x30
  CRITICAL_SECTION mutex;    // +0x38
};
static_assert(offsetof(GatewayConnection, listCount) == 0x28 && offsetof(GatewayConnection, mutex) == 0x38);

// 0x14063b470: create the gateway api (with its own UdpManager) and the
// connection's bookkeeping.
GatewayConnection* GatewayConnectionConstruct(GatewayConnection* self, const uint64_t* characterId, const char* ticket,
                                              const char* clientProtocol, const char* clientBuild, bool threaded,
                                              bool compression, int port, void* params, int unused, bool flag) {
  uint8_t* api = nullptr;
  if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x1068)) {
    uint64_t id = *characterId;
    using ConstructFn = uint8_t* (*)(void*, uint64_t*, const char*, const char*, const char*, bool, bool, int, void*,
                                     int, bool);
    api = game::Call<ConstructFn>(0x14162c9f0)(memory, &id, ticket, clientProtocol, clientBuild, threaded, compression,
                                               port, params, unused, flag);
  }
  self->api = api;
  self->createdMs = game::Call<uint64_t (*)()>(0x14032e7b0)();
  self->listVtable = reinterpret_cast<void**>(0x1420dd230);
  self->listCount = 0;
  self->listHead = nullptr;
  self->listTail = nullptr;
  self->flag = false;
  soeutil::MutexConstruct(&self->mutex, 4000, nullptr);
  return self;
}

// 0x14063bbe0: reliable channel statistics of the zone connection.
void GatewayConnectionGetReliableStats(uint8_t** wrapper, uint8_t* out) {
  game::Call<void (*)(uint8_t*, uint8_t*)>(0x1415f4290)(Api(wrapper), out);
}

// 0x14063bbf0 / 0x14063bc50: connection counters (cached after disconnect).
int GatewayConnectionValue1C0(uint8_t** wrapper) { return game::Call<int (*)(uint8_t*)>(0x1415f42b0)(Api(wrapper)); }
int GatewayConnectionValue1C4(uint8_t** wrapper) { return game::Call<int (*)(uint8_t*)>(0x1415f4340)(Api(wrapper)); }

// 0x14063bc10: the character id the gateway logs in with.
uint64_t* GatewayConnectionCharacterId(uint8_t** wrapper, uint64_t* out) {
  *out = game::Field<uint64_t>(Api(wrapper), 0xFC0);
  return out;
}

// 0x14063bca0: the UdpConnection (or null).
void* GatewayConnectionUdpConnection(uint8_t** wrapper) { return game::Field<void*>(Api(wrapper), 0x2C0); }

// 0x14063bcb0: the UdpManager behind the api's BaseUdpManager.
void* GatewayConnectionUdpManager(uint8_t** wrapper) {
  return game::Field<void*>(game::Field<uint8_t*>(Api(wrapper), 0x2D0), 0x240);
}

// 0x14063bcd0: the BaseUdpManager's UdpManager::Params (+0x88).
uint8_t* GatewayConnectionUdpParams(uint8_t** wrapper) { return game::Field<uint8_t*>(Api(wrapper), 0x2D0) + 0x88; }

// 0x14063bda0 / 0x14063bdb0: BaseApi IsConnecting (slot 11) / IsConnected (slot 10).
bool GatewayConnectionIsConnecting(uint8_t** wrapper) {
  return ApiVirtual<bool (*)(uint8_t*)>(Api(wrapper), 0x58 / 8)(Api(wrapper));
}
bool GatewayConnectionIsConnected(uint8_t** wrapper) {
  return ApiVirtual<bool (*)(uint8_t*)>(Api(wrapper), 0x50 / 8)(Api(wrapper));
}

// 0x14063bdc0: a session cipher is installed.
bool GatewayConnectionHasSessionKey(uint8_t** wrapper) { return game::Field<void*>(Api(wrapper), 0xD28) != nullptr; }

// 0x14063bdd0: logged in to the gateway and still connected.
bool GatewayConnectionIsLoggedIn(uint8_t** wrapper) {
  uint8_t* api = Api(wrapper);
  return game::Field<bool>(api, 0x1060) && ApiVirtual<bool (*)(uint8_t*)>(api, 0x50 / 8)(api);
}

// 0x14063c290: whether OnConnect sends the login request.
void GatewayConnectionSetLoginPending(uint8_t** wrapper, bool pending) { game::Field<bool>(Api(wrapper), 0x1061) = pending; }

// 0x14063c2a0: install the gateway listener (the zone client).
void GatewayConnectionSetListener(uint8_t** wrapper, void* listener) { game::Field<void*>(Api(wrapper), 0x1058) = listener; }

// 0x14063c2b0: BaseApi::WaitForDisconnect / Disconnect slot 12 (timeoutMs, force).
void GatewayConnectionDisconnect(uint8_t** wrapper, int timeoutMs, bool force) {
  ApiVirtual<void (*)(uint8_t*, int, bool)>(Api(wrapper), 0x60 / 8)(Api(wrapper), timeoutMs, force);
}

REBUILD_FUNCTION(GatewayConnection_Construct, 0x14063b470, GatewayConnectionConstruct);
REBUILD_FUNCTION(GatewayConnection_GetReliableStats, 0x14063bbe0, GatewayConnectionGetReliableStats);
REBUILD_FUNCTION(GatewayConnection_Value1C0, 0x14063bbf0, GatewayConnectionValue1C0);
REBUILD_FUNCTION(GatewayConnection_Value1C4, 0x14063bc50, GatewayConnectionValue1C4);
REBUILD_FUNCTION(GatewayConnection_CharacterId, 0x14063bc10, GatewayConnectionCharacterId);
REBUILD_FUNCTION(GatewayConnection_UdpConnection, 0x14063bca0, GatewayConnectionUdpConnection);
REBUILD_FUNCTION(GatewayConnection_UdpManager, 0x14063bcb0, GatewayConnectionUdpManager);
REBUILD_FUNCTION(GatewayConnection_UdpParams, 0x14063bcd0, GatewayConnectionUdpParams);
REBUILD_FUNCTION(GatewayConnection_IsConnecting, 0x14063bda0, GatewayConnectionIsConnecting);
REBUILD_FUNCTION(GatewayConnection_IsConnected, 0x14063bdb0, GatewayConnectionIsConnected);
REBUILD_FUNCTION(GatewayConnection_HasSessionKey, 0x14063bdc0, GatewayConnectionHasSessionKey);
REBUILD_FUNCTION(GatewayConnection_IsLoggedIn, 0x14063bdd0, GatewayConnectionIsLoggedIn);
REBUILD_FUNCTION(GatewayConnection_SetLoginPending, 0x14063c290, GatewayConnectionSetLoginPending);
REBUILD_FUNCTION(GatewayConnection_SetListener, 0x14063c2a0, GatewayConnectionSetListener);
REBUILD_FUNCTION(GatewayConnection_Disconnect, 0x14063c2b0, GatewayConnectionDisconnect);

}  // namespace rebuild::game_net
