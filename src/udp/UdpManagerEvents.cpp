// UdpManager's callback event queue. With event queuing on, handler
// callbacks are recorded as CallbackEvents and replayed by DeliverEvents on
// the application's thread instead of firing inside GiveTime.
#include <climits>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpManager.h"

namespace rebuild::udp {
namespace {

UdpRefCount* Ref(UdpConnection* connection) { return reinterpret_cast<UdpRefCount*>(connection); }

int PacketLength(UdpRefCount* packet) {
  return reinterpret_cast<int (*)(UdpRefCount*)>(packet->vtable[7])(packet);  // GetDataLen
}
void* PacketData(UdpRefCount* packet) {
  return reinterpret_cast<void* (*)(UdpRefCount*)>(packet->vtable[5])(packet);  // GetDataPtr
}

int EventPayloadBytes(CallbackEvent* event, bool* known) {
  *known = true;
  if (event->packet) return PacketLength(event->packet);
  if (event->payload) return event->payload->length;
  *known = false;
  return 0;
}

// UdpConnection callbacks, not rebuilt yet.
void ConnectionOnRoutePacket(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140347f70)(c, data, length);
}
void ConnectionOnConnectComplete(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140347de0)(c); }
void ConnectionOnTerminated(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140348000)(c); }
void ConnectionOnCrcReject(UdpConnection* c, const void* data, int length) {
  game::Call<void (*)(UdpConnection*, const void*, int)>(0x140347e50)(c, data, length);
}
void ConnectionOnPacketCorrupt(UdpConnection* c, const void* data, int length, int reason) {
  game::Call<void (*)(UdpConnection*, const void*, int, int)>(0x140347ee0)(c, data, length, reason);
}
void ConnectionDisconnect(UdpConnection* c, int flushTimeout, int reason) {
  game::Call<void (*)(UdpConnection*, int, int)>(0x1403471e0)(c, flushTimeout, reason);
}

// The connection's own handler pointer (+0x2B0), read under its guard at +0x2E8.
void* ConnectionHandler(UdpConnection* c) {
  auto& guard = game::Field<UdpPlatformGuardObject>(c, 0x2E8);
  guard.Enter();
  void* handler = game::Field<void*>(c, 0x2B0);
  guard.Leave();
  return handler;
}

// The handler declined to adopt the new connection: drop it.
void DisconnectIfUnhandled(UdpConnection* c) {
  if (!ConnectionHandler(c)) ConnectionDisconnect(c, 0, 10);
}

void HandlerOnConnectRequest(UdpManager* self, UdpConnection* c) {
  UdpRefCount* handler = self->Handler();
  reinterpret_cast<void (*)(UdpRefCount*, UdpConnection*)>(handler->vtable[1])(handler, c);
}

}  // namespace

// 0x14033e840: a recycled or new zeroed event.
CallbackEvent* AllocEvent(UdpManager* self) {
  self->EventPoolGuard().Enter();
  CallbackEvent* event = self->eventPool.RemoveHead();
  if (!event) {
    event = static_cast<CallbackEvent*>(soeutil::Allocate(sizeof(CallbackEvent)));
    if (event) {
      event->prev = nullptr;
      event->next = nullptr;
      event->type = 0;
      event->connection = nullptr;
      event->packet = nullptr;
      event->reason = 0;
      event->time = 0;
      event->payload = nullptr;
    }
  }
  self->EventPoolGuard().Leave();
  return event;
}

// 0x14033ef90: drops every reference the event holds.
void ClearEvent(CallbackEvent* event) {
  if (event->connection) {
    Ref(event->connection)->VirtualRelease();
    event->connection = nullptr;
  }
  if (event->packet) {
    event->packet->VirtualRelease();
    event->packet = nullptr;
  }
  if (SharedByteArray* payload = event->payload) {
    int* control = payload->control;
    int strong = InterlockedDecrement(reinterpret_cast<volatile LONG*>(&control[0]));
    int weakBefore = InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(&control[1]), -1);
    if (weakBefore == 1 && control) soeutil::Free(control, 0x10);
    if (strong == 0) {
      void* refCounted = &payload->refVtable;
      reinterpret_cast<void (*)(void*)>(payload->refVtable[1])(refCounted);
    }
    event->payload = nullptr;
  }
}

// 0x14033e8d0: back to the pool, or freed if the pool is full.
void ReleaseEvent(UdpManager* self, CallbackEvent* event) {
  if (self->eventPool.count < self->EventPoolMax()) {
    self->EventPoolGuard().Enter();
    self->eventPool.AddHead(event);
    self->EventPoolGuard().Leave();
  } else if (event) {
    ClearEvent(event);
    soeutil::Free(event, sizeof(CallbackEvent));
  }
}

// 0x14033ff20
void QueueEvent(UdpManager* self, CallbackEvent* event) {
  self->EventQueueGuard().Enter();
  self->eventQueue.AddTail(event);
  bool known;
  int bytes = EventPayloadBytes(event, &known);
  if (known) self->queuedEventBytes += bytes;
  if (self->maxQueuedEventBytes < self->queuedEventBytes) self->maxQueuedEventBytes = self->queuedEventBytes;
  if (self->maxQueuedEvents < self->eventQueue.count) self->maxQueuedEvents = self->eventQueue.count;
  self->EventQueueGuard().Leave();
}

// 0x14033ea30: a new incoming connection asks the manager's handler for
// acceptance, immediately or as a queued event.
void CallbackConnectRequest(UdpManager* self, UdpConnection* connection) {
  if (!self->EventQueuing()) {
    if (self->Handler()) HandlerOnConnectRequest(self, connection);
    DisconnectIfUnhandled(connection);
    return;
  }
  CallbackEvent* event = AllocEvent(self);
  event->type = kEventConnectRequest;
  event->connection = connection;
  auto* owner = game::Field<UdpManager*>(connection, 0xE0);
  event->time = owner ? owner->CachedClock() : game::Field<int64_t>(connection, 0x260);
  Ref(event->connection)->VirtualAddRef();
  QueueEvent(self, event);
}

// 0x14033f730. Replays queued events until the queue is empty or
// maxPollingTime ms have passed.
void DeliverEvents(UdpManager* self, int maxPollingTime) {
  self->VirtualAddRef();
  self->DeliverEventsGuard().Enter();
  int64_t start = ManagerClock(self);
  int elapsed;
  do {
    self->EventQueueGuard().Enter();
    CallbackEvent* event = self->eventQueue.RemoveHead();
    if (event) {
      bool known;
      int bytes = EventPayloadBytes(event, &known);
      if (known) self->queuedEventBytes -= bytes;
    }
    self->EventQueueGuard().Leave();
    if (!event) {
      self->DeliverEventsGuard().Leave();
      self->VirtualRelease();
      return;
    }

    self->StatsGuard().Enter();
    self->lastEventTime = event->time;
    self->StatsGuard().Leave();

    switch (event->type) {
      case kEventRoutePacket: {
        SharedByteArray* payload = event->payload;
        ConnectionOnRoutePacket(event->connection, payload->length != 0 ? payload->data : nullptr,
                                payload->length);
        break;
      }
      case kEventConnectComplete:
        ConnectionOnConnectComplete(event->connection);
        break;
      case kEventTerminated:
        ConnectionOnTerminated(event->connection);
        break;
      case kEventCrcReject: {
        int length = PacketLength(event->packet);
        ConnectionOnCrcReject(event->connection, PacketData(event->packet), length);
        break;
      }
      case kEventPacketCorrupt: {
        int length = PacketLength(event->packet);
        ConnectionOnPacketCorrupt(event->connection, PacketData(event->packet), length, event->reason);
        break;
      }
      case kEventConnectRequest:
        if (self->Handler()) HandlerOnConnectRequest(self, event->connection);
        DisconnectIfUnhandled(event->connection);
        break;
    }
    ClearEvent(event);
    ReleaseEvent(self, event);
    int64_t now = ManagerClock(self);
    elapsed = now - start > INT_MAX ? INT_MAX : static_cast<int>(now - start);
  } while (elapsed < maxPollingTime);

  self->StatsGuard().Enter();
  self->eventDeliveryTimeouts += 1;
  self->StatsGuard().Leave();
  self->DeliverEventsGuard().Leave();
  self->VirtualRelease();
}

// 0x140343e00: UdpLinkedList<CallbackEvent>::RemoveHead (out of line).
static CallbackEvent* EventListRemoveHead(UdpLinkedList<CallbackEvent>* list) { return list->RemoveHead(); }

REBUILD_FUNCTION(UdpManager_AllocEvent, 0x14033e840, AllocEvent);
REBUILD_FUNCTION(UdpManager_ClearEvent, 0x14033ef90, ClearEvent);
REBUILD_FUNCTION(UdpManager_ReleaseEvent, 0x14033e8d0, ReleaseEvent);
REBUILD_FUNCTION(UdpManager_QueueEvent, 0x14033ff20, QueueEvent);
REBUILD_FUNCTION(UdpManager_CallbackConnectRequest, 0x14033ea30, CallbackConnectRequest);
REBUILD_FUNCTION(UdpManager_DeliverEvents, 0x14033f730, DeliverEvents);
REBUILD_FUNCTION(UdpLinkedList_CallbackEvent_RemoveHead, 0x140343e00, EventListRemoveHead);

}  // namespace rebuild::udp
