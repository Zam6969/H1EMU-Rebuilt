// The game's zone-server connection wrapper (0x80 bytes, built by
// 0x14063b470): owns the Gateway::ExternalGatewayApi (+0x00) and forwards to
// it. The zone client object (vtable 0x1420dd6e0, constructed at 0x14063cc10)
// owns one of these and registers itself as the gateway listener.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/ByteStream.h"
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

// Packets the connection queues: a base record and two variants carrying a
// copy of the payload in a SoeUtil byte stream (vtables 0x1420dd1d0 /
// 0x1420dd1f0 / 0x1420dd210).
struct QueuedPacket {
  void** vtable;
  uint8_t* api;     // +0x08, the gateway api to send through
  int mode;         // +0x10: 0 unreliable, 1 reliable, 2 secure
  int padding;
};
static_assert(sizeof(QueuedPacket) == 0x18);

struct QueuedDataPacket : QueuedPacket {
  soeutil::ByteStream stream;  // +0x18
};
static_assert(offsetof(QueuedDataPacket, stream) == 0x18 && sizeof(QueuedDataPacket) == 0x2050);

struct QueuedDataPacketEx : QueuedDataPacket {
  int channel;  // +0x2050, tunnel channel
  int padding2;
};
static_assert(offsetof(QueuedDataPacketEx, channel) == 0x2050 && sizeof(QueuedDataPacketEx) == 0x2058);

constexpr uintptr_t kVtQueuedPacket = 0x1420dd1d0;
constexpr uintptr_t kVtQueuedDataPacketEx = 0x1420dd210;

void SizedDelete(void* object, size_t size) { game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size); }

// 0x14063b750: QueuedPacket scalar deleting destructor.
QueuedPacket* QueuedPacketDeletingDestructor(QueuedPacket* self, unsigned flags) {
  self->vtable = reinterpret_cast<void**>(kVtQueuedPacket);
  if (flags & 1) SizedDelete(self, sizeof(QueuedPacket));
  return self;
}

// 0x14063b780 / 0x14063b7d0: data packet deleting destructors.
QueuedDataPacket* QueuedDataPacketDeletingDestructor(QueuedDataPacket* self, unsigned flags) {
  soeutil::ByteArrayDestroy(&self->stream.inlineArray);
  self->vtable = reinterpret_cast<void**>(kVtQueuedPacket);
  if (flags & 1) SizedDelete(self, sizeof(QueuedDataPacket));
  return self;
}
QueuedDataPacketEx* QueuedDataPacketExDeletingDestructor(QueuedDataPacketEx* self, unsigned flags) {
  soeutil::ByteArrayDestroy(&self->stream.inlineArray);
  self->vtable = reinterpret_cast<void**>(kVtQueuedPacket);
  if (flags & 1) SizedDelete(self, sizeof(QueuedDataPacketEx));
  return self;
}

// 0x14063b590: QueuedDataPacketEx(api, data, length, channel, mode).
QueuedDataPacketEx* QueuedDataPacketExConstruct(QueuedDataPacketEx* self, uint8_t* api, const void* data, int length,
                                                int channel, int mode) {
  self->api = api;
  self->mode = mode;
  self->vtable = reinterpret_cast<void**>(kVtQueuedDataPacketEx);
  soeutil::ByteStream& stream = self->stream;
  stream.inlineArray.data = nullptr;
  stream.inlineArray.size = 0;
  stream.inlineArray.unknown14 = 0;
  stream.inlineArray.vtable = reinterpret_cast<void**>(soeutil::kVtByteArray8k);
  stream.unknown202C = 0;
  stream.writePos = 0;
  stream.array = &stream.inlineArray;
  stream.maxSize = soeutil::kByteStreamMaxSize;
  self->channel = channel;
  soeutil::StreamPut(&stream, data, length);
  return self;
}

const uint8_t* StreamBytes(const soeutil::ByteStream& stream) {
  return stream.array->size != 0 ? stream.array->data : nullptr;
}

// 0x14063bf20 (QueuedDataPacket slot 1): send through the gateway api.
bool QueuedDataPacketSend(QueuedDataPacket* self) {
  int mode = self->mode;
  if (mode < 0) return false;
  int size = self->stream.array->size;
  if (mode <= 1) return game::Call<bool (*)(uint8_t*, const uint8_t*, int, bool)>(0x14162db20)(self->api, StreamBytes(self->stream), size, mode == 1);
  if (mode == 2) return game::Call<bool (*)(uint8_t*, const uint8_t*, int)>(0x14162dba0)(self->api, StreamBytes(self->stream), size);
  return false;
}

// 0x14063bf90 (QueuedDataPacketEx slot 1): send as a tunnel packet.
bool QueuedDataPacketExSend(QueuedDataPacketEx* self) {
  int mode = self->mode;
  if (mode < 0) return false;
  using TunnelFn = bool (*)(uint8_t*, const uint8_t*, int, uint8_t, bool, bool);
  int size = self->stream.array->size;
  if (mode <= 1)
    return game::Call<TunnelFn>(0x14162dcc0)(self->api, StreamBytes(self->stream), size,
                                             static_cast<uint8_t>(self->channel), mode == 1, false);
  if (mode == 2)
    return game::Call<TunnelFn>(0x14162dcc0)(self->api, StreamBytes(self->stream), size,
                                             static_cast<uint8_t>(self->channel), true, true);
  return false;
}

// Slot 2: 8 for the base/data packets, the channel for tunnel packets.
int QueuedPacketKind(QueuedPacket*) { return 8; }                              // 0x14063bbc0
int QueuedDataPacketExChannel(QueuedDataPacketEx* self) { return self->channel; }  // 0x14063bbd0
// Slot 3: the payload stream (none for the base/data variants).
void* QueuedPacketStreamNone(QueuedPacket*) { return nullptr; }                   // 0x14063bc80
soeutil::ByteStream* QueuedDataPacketExStream(QueuedDataPacketEx* self) { return &self->stream; }  // 0x14063bc90

// Intrusive list of queued packets at connection+0x10 (vtable 0x1420dd230).
struct PacketListNode {
  void** vtable;
  PacketListNode* next;  // +0x08
  PacketListNode* prev;  // +0x10
};
struct PacketList {
  void** vtable;
  PacketListNode* head;
  PacketListNode* tail;
  int count;
  int padding;
};
static_assert(sizeof(PacketList) == 0x20);

bool ThreadAllocatorActive() { return *reinterpret_cast<uint64_t*>(0x143e09638) != 0; }

// 0x14063b8c0 (PacketList slot 2): allocate a 0x18-byte node.
void* PacketListAllocateNode(PacketList*) {
  if (!ThreadAllocatorActive())
    return game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(0x18, reinterpret_cast<const void*>(0x143c46658));  // new[](nothrow)
  return game::Call<void* (*)(size_t, size_t)>(0x14032f910)(0x18, 8);
}

// 0x14063bba0 (PacketList slot 3): free a node.
void PacketListFreeNode(PacketList*, PacketListNode* node) {
  if (!ThreadAllocatorActive()) {
    soeutil::FreeArray(node);
    return;
  }
  game::Call<void (*)(void*, size_t, void*)>(0x14032f980)(node, 8, node);
}

// 0x14063be00 (PacketList slot 1): always true.
bool PacketListAlwaysTrue(PacketList*) { return true; }

// 0x14063b640: unlink and free every node (slot 3 frees a node).
void PacketListDestroy(PacketList* list) {
  list->vtable = reinterpret_cast<void**>(0x1420dd230);
  while (list->head) {
    PacketListNode* node = list->head;
    if (!node) continue;
    if (node->prev)
      node->prev->next = node->next;
    else
      list->head = node->next;
    if (node->next)
      node->next->prev = node->prev;
    else
      list->tail = node->prev;
    --list->count;
    reinterpret_cast<void (*)(PacketList*, PacketListNode*)>(list->vtable[3])(list, node);
  }
}

// 0x14063b710: PacketList scalar deleting destructor.
PacketList* PacketListDeletingDestructor(PacketList* self, unsigned flags) {
  PacketListDestroy(self);
  if (flags & 1) SizedDelete(self, sizeof(PacketList));
  return self;
}

// 0x14063b6d0: GatewayConnection destructor.
void GatewayConnectionDestroy(GatewayConnection* self) {
  if (self->api) reinterpret_cast<void (*)(uint8_t*, int)>((*reinterpret_cast<void***>(self->api))[0])(self->api, 1);
  soeutil::MutexDestroy(&self->mutex);
  PacketListDestroy(reinterpret_cast<PacketList*>(&self->listVtable));
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
REBUILD_FUNCTION(GatewayConnection_Destroy, 0x14063b6d0, GatewayConnectionDestroy);
REBUILD_FUNCTION(GatewayPacketList_Destroy, 0x14063b640, PacketListDestroy);
REBUILD_FUNCTION(GatewayPacketList_DeletingDestructor, 0x14063b710, PacketListDeletingDestructor);
REBUILD_FUNCTION(GatewayQueuedPacket_DeletingDestructor, 0x14063b750, QueuedPacketDeletingDestructor);
REBUILD_FUNCTION(GatewayQueuedDataPacket_DeletingDestructor, 0x14063b780, QueuedDataPacketDeletingDestructor);
REBUILD_FUNCTION(GatewayQueuedDataPacketEx_DeletingDestructor, 0x14063b7d0, QueuedDataPacketExDeletingDestructor);
REBUILD_FUNCTION(GatewayQueuedDataPacketEx_Construct, 0x14063b590, QueuedDataPacketExConstruct);
REBUILD_FUNCTION(GatewayQueuedDataPacket_Send, 0x14063bf20, QueuedDataPacketSend);
REBUILD_FUNCTION(GatewayQueuedDataPacketEx_Send, 0x14063bf90, QueuedDataPacketExSend);
REBUILD_FUNCTION(GatewayQueuedPacket_Kind, 0x14063bbc0, QueuedPacketKind);
REBUILD_FUNCTION(GatewayQueuedDataPacketEx_Channel, 0x14063bbd0, QueuedDataPacketExChannel);
REBUILD_FUNCTION_TOO_SMALL(GatewayQueuedPacket_StreamNone, 0x14063bc80, QueuedPacketStreamNone);
REBUILD_FUNCTION(GatewayQueuedDataPacketEx_Stream, 0x14063bc90, QueuedDataPacketExStream);
REBUILD_FUNCTION(GatewayPacketList_AllocateNode, 0x14063b8c0, PacketListAllocateNode);
REBUILD_FUNCTION(GatewayPacketList_FreeNode, 0x14063bba0, PacketListFreeNode);
REBUILD_FUNCTION_TOO_SMALL(GatewayPacketList_AlwaysTrue, 0x14063be00, PacketListAlwaysTrue);
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
