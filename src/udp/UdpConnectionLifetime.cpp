// UdpConnection construction and destruction.
#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpConnection.h"
#include "udp/UdpReliableChannel.h"

namespace rebuild::udp {

void ReliableDestruct(UdpReliableChannel* channel);  // UdpReliableChannelSend.cpp

namespace {

using O = ConnectionOffsets;

constexpr size_t kUdpConnectionSize = 0x450;
constexpr size_t kHelperSize = 0x218;
inline void** const kUdpConnectionVtable = reinterpret_cast<void**>(0x142051fc8);

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

// The 0x218-byte helper object at +0x2F0 lives outside UdpLibrary
// (0x14165xxxx) and is not rebuilt yet.
void* NewHelper() {
  void* memory = soeutil::Allocate(kHelperSize);
  void* helper = memory ? game::Call<void* (*)(void*)>(0x14165aaf0)(memory) : nullptr;
  game::Call<void (*)(void*, int, int)>(0x14165af80)(helper, 0, 0);
  return helper;
}
void ProcessRawPacket(UdpConnection* c, UdpPacketBuffer* buffer) {
  game::Call<void (*)(UdpConnection*, UdpPacketBuffer*)>(0x1403491e0)(c, buffer);
}
void InternalGiveTime(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140347360)(c); }

// Shared constructor prologue (inlined into both constructors).
void ConstructBase(UdpConnection* c) {
  auto* self = reinterpret_cast<UdpGuardedRefCount*>(c);
  self->refCount = 1;
  self->flag = false;
  self->vtable = kUdpGuardedRefCountVtable;
  self->guard.Construct();
  ConnField<int>(c, O::kHeapIndex) = -1;
  self->vtable = kUdpConnectionVtable;
  ConnField<UdpIpAddress>(c, O::kIp) = 0;
  for (size_t link = 0xB0; link <= 0xC8; link += 8) ConnField<void*>(c, link) = nullptr;
  ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard).Construct();
  ConnField<UdpPlatformGuardObject>(c, O::kHandlerGuard).Construct();
}

// Shared epilogue: one update pass with the manager pinned, then drop the
// constructor's own reference. The status guard was entered once by the
// caller and once here; both are left.
void FirstTickAndRelease(UdpConnection* c) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  if (auto* manager = ConnField<UdpManager*>(c, O::kManager)) {
    manager->VirtualAddRef();
    ConnField<uint8_t>(c, O::kInGiveTime) = 1;
    InternalGiveTime(c);
    ConnField<uint8_t>(c, O::kInGiveTime) = 0;
    manager->VirtualRelease();
  }
  guard.Leave();
  guard.Leave();
  Ref(c)->VirtualRelease();
}

}  // namespace

// 0x140345090: server side, created by the manager for an incoming connect
// request (buffer holds that request).
UdpConnection* ConnectionConstructIncoming(UdpConnection* c, UdpManager* manager, UdpPacketBuffer* buffer) {
  ConstructBase(c);
  Ref(c)->VirtualAddRef();
  ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard).Enter();
  UdpIpAddress ip = buffer->ip;
  ConnectionInit(c, manager, &ip, buffer->port);
  ConnField<int>(c, UdpConnectionInternals::kStatus) = kStatusConnected;
  UdpManager* owner = ConnField<UdpManager*>(c, O::kManager);
  ConnField<int>(c, O::kEncryptMethods) = owner->params.At<int>(0x1A8);
  ConnField<int>(c, O::kEncryptMethods + 4) = owner->params.At<int>(0x1AC);
  ConnField<int>(c, 0x1F4) = owner->params.At<int>(0x1A4);  // CRC bytes
  ConnField<int>(c, 0x200) = owner->MaxRawPacketSize();
  ConnField<int>(c, O::kEncryptCode) = Random(&owner->randomSeed);
  ConnectionSetupEncryption(c);
  ConnField<void*>(c, 0x2F0) = NewHelper();
  const uint8_t* request = buffer->data;
  ConnField<uint32_t>(c, O::kConnectCode) = static_cast<uint32_t>(request[6]) << 24 |
                                            static_cast<uint32_t>(request[7]) << 16 |
                                            static_cast<uint32_t>(request[8]) << 8 | request[9];
  AddNewConnection(ConnField<UdpManager*>(c, O::kManager), c);
  ProcessRawPacket(c, buffer);
  FirstTickAndRelease(c);
  return c;
}

// 0x1403452d0: client side, connecting out to ip:port.
UdpConnection* ConnectionConstructOutgoing(UdpConnection* c, UdpManager* manager, const UdpIpAddress* ip, int port,
                                           int connectTimeout) {
  ConstructBase(c);
  Ref(c)->VirtualAddRef();
  ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard).Enter();
  UdpIpAddress address = *ip;
  ConnectionInit(c, manager, &address, port);
  ConnField<int>(c, 0x1B8) = connectTimeout;
  ConnField<int>(c, UdpConnectionInternals::kStatus) = kStatusNegotiating;
  ConnField<int>(c, O::kConnectCode) = Random(&ConnField<UdpManager*>(c, O::kManager)->randomSeed);
  AddNewConnection(ConnField<UdpManager*>(c, O::kManager), c);
  ConnField<void*>(c, 0x2F0) = NewHelper();
  FirstTickAndRelease(c);
  return c;
}

// 0x140345580
void ConnectionDestruct(UdpConnection* c) {
  Ref(c)->vtable = kUdpConnectionVtable;
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  for (size_t slot = 0x1D0; slot <= 0x1E8; slot += 8) {
    if (auto* channel = ConnField<UdpReliableChannel*>(c, slot)) {
      ReliableDestruct(channel);
      soeutil::Free(channel, 0x158);
    }
  }
  soeutil::FreeArray(ConnField<void*>(c, 0x250));
  soeutil::FreeArray(ConnField<void*>(c, O::kXorBuffer));
  if (void* helper = ConnField<void*>(c, 0x2F0)) {
    game::Call<void (*)(void*)>(0x14165ac00)(helper);
    soeutil::Free(helper, kHelperSize);
  }
  guard.Leave();
  ConnField<UdpPlatformGuardObject>(c, O::kHandlerGuard).Destruct();
  ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard).Destruct();
  reinterpret_cast<UdpGuardedRefCount*>(c)->guard.Destruct();
  Ref(c)->vtable = kUdpRefCountVtable;
}

// 0x1403457a0, slot 4
UdpConnection* ConnectionDeletingDestructor(UdpConnection* c, unsigned flags) {
  ConnectionDestruct(c);
  if (flags & 1) soeutil::Free(c, kUdpConnectionSize);
  return c;
}

REBUILD_FUNCTION(UdpConnection_ConstructIncoming, 0x140345090, ConnectionConstructIncoming);
REBUILD_FUNCTION(UdpConnection_ConstructOutgoing, 0x1403452d0, ConnectionConstructOutgoing);
REBUILD_FUNCTION(UdpConnection_Destruct, 0x140345580, ConnectionDestruct);
REBUILD_FUNCTION(UdpConnection_DeletingDestructor, 0x1403457a0, ConnectionDeletingDestructor);

}  // namespace rebuild::udp
