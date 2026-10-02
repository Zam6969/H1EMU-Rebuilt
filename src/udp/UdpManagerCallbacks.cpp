// UdpManager's connection callbacks: each either calls the connection's
// handler right away, or (event queuing on) records a CallbackEvent for
// DeliverEvents.
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "soeutil/Mutex.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpManager.h"

namespace rebuild::udp {
namespace {

UdpRefCount* Ref(UdpConnection* connection) { return reinterpret_cast<UdpRefCount*>(connection); }

// UdpConnection handler entry points, not rebuilt yet.
void ConnectionOnRoutePacket(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140347f70)(c, data, length);
}
void ConnectionOnConnectComplete(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140347de0)(c); }
void ConnectionOnTerminated(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140348000)(c); }
void ConnectionOnCrcReject(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140347e50)(c, data, length);
}
void ConnectionOnPacketCorrupt(UdpConnection* c, const uint8_t* data, int length, int reason) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int, int)>(0x140347ee0)(c, data, length, reason);
}

// The shared-payload memory pool (+0x610) and its byte-array factory, not
// rebuilt yet (SoeUtil DynamicMemoryPool).
SharedByteArray* PoolCreateByteArray(UdpManager* self, const uint8_t** data, int* length) {
  using Fn = SharedByteArray* (*)(void*, void*, const uint8_t**, int*);
  return game::Call<Fn>(0x14033b440)(game::Field<void*>(self, 0x618),
                                     reinterpret_cast<uint8_t*>(self) + 0x610, data, length);
}
SharedByteArray* ConstructByteArray(void* memory, const uint8_t* data, int length) {
  return game::Call<SharedByteArray* (*)(void*, const uint8_t*, int)>(0x14033b940)(memory, data, length);
}

// Every event records when it happened by the owning manager's clock (or
// the connection's cached time once detached from its manager).
int64_t ConnectionEventTime(UdpConnection* c) {
  auto* owner = game::Field<UdpManager*>(c, 0xE0);
  return owner ? owner->CachedClock() : game::Field<int64_t>(c, 0x260);
}

CallbackEvent* NewConnectionEvent(UdpManager* self, int type, UdpConnection* c) {
  CallbackEvent* event = AllocEvent(self);
  event->type = type;
  event->connection = c;
  event->time = ConnectionEventTime(c);
  Ref(event->connection)->VirtualAddRef();
  return event;
}

void ReleaseByteArray(SharedByteArray* array) {
  int* control = array->control;
  int strong = InterlockedDecrement(reinterpret_cast<volatile LONG*>(&control[0]));
  int weakBefore = InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(&control[1]), -1);
  if (weakBefore == 1 && control) soeutil::Free(control, 0x10);
  if (strong == 0) reinterpret_cast<void (*)(void*)>(array->refVtable[1])(&array->refVtable);
}

}  // namespace

// 0x14034c260
void PooledSetData(PooledLogicalPacket* self, const uint8_t* data, int length, const uint8_t* data2,
                   int length2) {
  self->dataLen = length + length2;
  if (data) std::memcpy(self->data, data, length);
  if (data2) std::memcpy(self->data + length, data2, length2);
}

// 0x14033f340. A logical packet holding data+data2: from the manager's
// pool when pooling is on and it fits, otherwise a quick packet.
LogicalPacket* CreatePacket(UdpManager* self, const uint8_t* data, int length, const uint8_t* data2,
                            int length2) {
  int pooledSize = self->params.At<int>(0x60);
  if (self->PooledPacketMax() < 1 || pooledSize < length + length2)
    return CreateQuickLogicalPacket(data, length, data2, length2);

  self->PoolGuard().Enter();
  PooledLogicalPacket* packet = self->pooledAvailable.RemoveHead();
  if (!packet) {
    void* memory = soeutil::Allocate(sizeof(PooledLogicalPacket));
    packet = memory ? game::Call<PooledLogicalPacket* (*)(void*, UdpManager*, int)>(0x14034bcd0)(
                          memory, self, pooledSize)
                    : nullptr;
  }
  PooledSetData(packet, data, length, data2, length2);
  self->PoolGuard().Leave();
  return packet;
}

// 0x14033e980
void CallbackConnectComplete(UdpManager* self, UdpConnection* connection) {
  if (!self->EventQueuing()) {
    ConnectionOnConnectComplete(connection);
    return;
  }
  QueueEvent(self, NewConnectionEvent(self, kEventConnectComplete, connection));
}

// 0x14033eee0
void CallbackTerminated(UdpManager* self, UdpConnection* connection) {
  if (!self->EventQueuing()) {
    ConnectionOnTerminated(connection);
    return;
  }
  QueueEvent(self, NewConnectionEvent(self, kEventTerminated, connection));
}

// 0x14033eb20
void CallbackCrcReject(UdpManager* self, UdpConnection* connection, const uint8_t* data, int length) {
  if (!self->EventQueuing()) {
    ConnectionOnCrcReject(connection, data, length);
    return;
  }
  CallbackEvent* event = AllocEvent(self);
  LogicalPacket* packet = CreatePacket(self, data, length, nullptr, 0);
  event->type = kEventCrcReject;
  event->connection = connection;
  event->time = ConnectionEventTime(connection);
  Ref(event->connection)->VirtualAddRef();
  if (packet) {
    event->packet = packet;
    packet->VirtualAddRef();
  }
  packet->VirtualRelease();
  QueueEvent(self, event);
}

// 0x14033ec20
void CallbackPacketCorrupt(UdpManager* self, UdpConnection* connection, const uint8_t* data, int length,
                           int reason) {
  if (!self->EventQueuing()) {
    ConnectionOnPacketCorrupt(connection, data, length, reason);
    return;
  }
  CallbackEvent* event = AllocEvent(self);
  LogicalPacket* packet = CreatePacket(self, data, length, nullptr, 0);
  event->type = kEventPacketCorrupt;
  event->connection = connection;
  event->time = ConnectionEventTime(connection);
  Ref(event->connection)->VirtualAddRef();
  if (packet) {
    event->packet = packet;
    packet->VirtualAddRef();
  }
  event->reason = reason;
  packet->VirtualRelease();
  QueueEvent(self, event);
}

// 0x14033ed30. Queued application packets are copied into a shared byte
// array, from the manager's memory pool while it is under budget.
void CallbackRoutePacket(UdpManager* self, UdpConnection* connection, const uint8_t* data, int length) {
  if (!self->EventQueuing()) {
    ConnectionOnRoutePacket(connection, data, length);
    return;
  }
  CallbackEvent* event = AllocEvent(self);

  uint8_t* pool = game::Field<uint8_t*>(self, 0x610);
  auto* poolLock = reinterpret_cast<CRITICAL_SECTION*>(pool + 0x518);
  soeutil::MutexLock(poolLock);
  int poolBytes = *reinterpret_cast<int*>(pool + 0x50C) + *reinterpret_cast<int*>(pool + 0x508) +
                  *reinterpret_cast<int*>(pool + 0x500);
  if (poolLock) soeutil::MutexUnlock(poolLock);

  SharedByteArray* payload;
  if (poolBytes < self->params.At<int>(0x60) * self->PooledPacketMax()) {
    payload = PoolCreateByteArray(self, &data, &length);
  } else {
    void* memory = soeutil::Allocate(0x28);
    payload = memory ? ConstructByteArray(memory, data, length) : nullptr;
  }

  event->type = kEventRoutePacket;
  event->connection = connection;
  event->time = ConnectionEventTime(connection);
  Ref(event->connection)->VirtualAddRef();
  if (payload) {
    event->payload = payload;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&payload->control[1]));
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&payload->control[0]));
  }
  ReleaseByteArray(payload);
  QueueEvent(self, event);
}

// 0x140343e70: UdpLinkedList<PooledLogicalPacket>::RemoveHead (out of line).
static PooledLogicalPacket* PooledListRemoveHead(UdpLinkedList<PooledLogicalPacket>* list) {
  return list->RemoveHead();
}

REBUILD_FUNCTION(PooledLogicalPacket_SetData, 0x14034c260, PooledSetData);
REBUILD_FUNCTION(UdpManager_CreatePacket, 0x14033f340, CreatePacket);
REBUILD_FUNCTION(UdpManager_CallbackConnectComplete, 0x14033e980, CallbackConnectComplete);
REBUILD_FUNCTION(UdpManager_CallbackTerminated, 0x14033eee0, CallbackTerminated);
REBUILD_FUNCTION(UdpManager_CallbackCrcReject, 0x14033eb20, CallbackCrcReject);
REBUILD_FUNCTION(UdpManager_CallbackPacketCorrupt, 0x14033ec20, CallbackPacketCorrupt);
REBUILD_FUNCTION(UdpManager_CallbackRoutePacket, 0x14033ed30, CallbackRoutePacket);
REBUILD_FUNCTION(UdpLinkedList_PooledLogicalPacket_RemoveHead, 0x140343e70, PooledListRemoveHead);

}  // namespace rebuild::udp
