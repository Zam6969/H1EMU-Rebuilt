// UdpManager destruction, socket setup, and its connection-level API
// (EstablishConnection, expect-incoming, disconnect-all, event pop, dump).
#include <cstdlib>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"
#include "soeutil/Mutex.h"
#include "udp/UdpConnection.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

constexpr int kErrorConditionCouldNotBindSocket = 2;

// Vtables for the destructor's list resets.
void** const kCallbackEventListVtable = reinterpret_cast<void**>(0x142051768);
void** const kPooledPacketListVtable = reinterpret_cast<void**>(0x1420516d8);
void** const kSimulateListVtable = reinterpret_cast<void**>(0x1420516c8);
void** const kExpectIncomingListVtable = reinterpret_cast<void**>(0x1420516b8);
void** const kConnectionListVtable = reinterpret_cast<void**>(0x1420516a8);

// Not rebuilt yet (SoeUtil DynamicMemoryPool internals).
void MutexDestroy(void* mutex) { game::Call<void (*)(void*)>(0x14032f060)(mutex); }
void PoolTreeDelete(void* map, void* node) { game::Call<void (*)(void*, void*)>(0x140343700)(map, node); }
void PoolBlockListDestruct(void* list) { game::Call<void (*)(void*)>(0x1403218d0)(list); }
void PoolDestructBase(void* pool) { game::Call<void (*)(void*)>(0x140321500)(pool); }
void SmallPoolDestruct(void* pool) { game::Call<void (*)(void*)>(0x14033cc30)(pool); }
UdpConnection* ConstructOutgoing(void* memory, UdpManager* m, const UdpIpAddress* ip, int port, int timeout) {
  using Fn = UdpConnection* (*)(void*, UdpManager*, const UdpIpAddress*, int, int);
  return game::Call<Fn>(0x1403452d0)(memory, m, ip, port, timeout);
}
const char* IpToString(const UdpIpAddress* ip, char* buffer, int size) {
  return game::Call<const char* (*)(const UdpIpAddress*, char*, int)>(0x14034a5c0)(ip, buffer, size);
}
template <class... Args>
void StringAppendFormat(void* string, const char* format, Args... args) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402ed6c0)(string, format, args...);
}
void StringAppend(void* string, const char* text) {
  game::Call<void (*)(void*, const char*)>(0x1402bd730)(string, text);
}

bool DriverGetHostByName(UdpManager* m, UdpIpAddress* ip, const char* host) {
  using Fn = bool (*)(UdpPlatformDriver*, UdpIpAddress*, const char*);
  return reinterpret_cast<Fn>(m->driver->vtable[kSlotGetHostByName])(m->driver, ip, host);
}

// "host[:port]" -> host in `buffer` (max 511 chars), port overrides `port`.
void SplitHostPort(const char* address, char (&buffer)[512], int& port) {
  char* out = buffer;
  for (const char* in = address; *in && out < buffer + 0x1FF;) *out++ = *in++;
  *out = 0;
  if (char* colon = std::strchr(buffer, ':')) {
    *colon = 0;
    port = std::atoi(colon + 1);
  }
}

// One pass of the pool map teardown done twice by the destructor (once with
// each Map vtable): delete the tree under the root, free the root's block
// unless it is the inline one, then hand the root back to the map.
void DestroyPoolMapRoot(uint8_t* map) {
  auto* root = *reinterpret_cast<uint8_t**>(map + 8);
  if (root) {
    PoolTreeDelete(map, *reinterpret_cast<void**>(root + 0x28));
    PoolTreeDelete(map, *reinterpret_cast<void**>(root + 0x30));
    auto block = *reinterpret_cast<uintptr_t*>(root + 8);
    uintptr_t inlineBlock = (*reinterpret_cast<uintptr_t*>(root) + 0x517) & ~uintptr_t{7};
    if (block && block != inlineBlock) {
      if (soeutil::ThreadAllocatorCount() == 0)
        soeutil::FreeArray(reinterpret_cast<void*>(block));
      else
        soeutil::MemoryFree(reinterpret_cast<void*>(block), 8);
    }
    auto** vtable = *reinterpret_cast<void***>(map);
    reinterpret_cast<void (*)(void*, void*)>(vtable[3])(map, root);
  }
  *reinterpret_cast<void**>(map + 8) = nullptr;
  *reinterpret_cast<int*>(map + 0x10) = 0;
}

}  // namespace

// 0x14033f540 / 0x14033f5f0 / 0x14033f680: free every node of a list.
void DeleteAllEvents(UdpLinkedList<CallbackEvent>* list) {
  while (CallbackEvent* event = list->first) {
    list->Remove(event);
    ClearEvent(event);
    soeutil::Free(event, sizeof(CallbackEvent));
  }
}
void DeleteAllExpectIncoming(UdpLinkedList<ExpectIncomingEntry>* list) {
  while (ExpectIncomingEntry* entry = list->first) {
    list->Remove(entry);
    soeutil::Free(entry, sizeof(ExpectIncomingEntry));
  }
}
void DeleteAllSimulated(UdpLinkedList<SimulateQueueEntry>* list) {
  while (SimulateQueueEntry* entry = list->first) {
    list->Remove(entry);
    soeutil::FreeArray(entry->data);
    soeutil::Free(entry, sizeof(SimulateQueueEntry));
  }
}

// 0x1403430b0 / 0x140343020: unlink and release every element.
void ReleaseAllConnections(UdpConnectionList* list) {
  while (UdpConnection* c = list->first) {
    list->Remove(c);
    Ref(c)->VirtualRelease();
  }
}
void ReleaseAllPooled(UdpLinkedList<PooledLogicalPacket>* list) {
  while (PooledLogicalPacket* p = list->first) {
    list->Remove(p);
    reinterpret_cast<UdpRefCount*>(p)->VirtualRelease();
  }
}

// 0x14033f2d0: (re)opens the socket; error condition 2 if bind fails.
void ManagerOpenSocket(UdpManager* self, int port) {
  using CloseFn = void (*)(UdpPlatformDriver*);
  reinterpret_cast<CloseFn>(self->driver->vtable[kSlotSocketClose])(self->driver);
  game::Field<int>(self, 0x2DC) = 0;
  using OpenFn = bool (*)(UdpPlatformDriver*, int, int, int, const char*);
  bool opened = reinterpret_cast<OpenFn>(self->driver->vtable[kSlotSocketOpen])(
      self->driver, port, self->params.At<int>(0x18), self->params.At<int>(0x14),
      reinterpret_cast<const char*>(&self->params.At<uint8_t>(0x74)));
  if (!opened) game::Field<int>(self, 0x2DC) = kErrorConditionCouldNotBindSocket;
}

// 0x14033bc40
ExpectIncomingEntry* ExpectIncomingConstruct(ExpectIncomingEntry* e, UdpManager* manager, const UdpIpAddress* ip,
                                             int port, int timeout, uint32_t connectCode) {
  e->prev = nullptr;
  e->next = nullptr;
  e->manager = manager;
  e->ip = *ip;
  e->port = port;
  e->timeout = timeout;
  e->connectCode = connectCode;
  e->startTime = ManagerClock(e->manager);
  e->lastSendTime = 0;
  ExpectIncomingGiveTime(e);
  return e;
}

// 0x140340090: expect a connection from "host[:port]" (punch-through).
void ManagerExpectIncoming(UdpManager* self, const char* address, int port, int timeout, uint32_t connectCode) {
  self->ExpectIncomingGuard().Enter();
  char host[512];
  SplitHostPort(address, host, port);
  UdpIpAddress ip = 0;
  if (DriverGetHostByName(self, &ip, host)) {
    UdpIpAddress copy = ip;
    TakeExpectIncoming(self, &copy, port);
    auto* entry = static_cast<ExpectIncomingEntry*>(soeutil::Allocate(sizeof(ExpectIncomingEntry)));
    if (entry) {
      UdpIpAddress entryIp = ip;
      ExpectIncomingConstruct(entry, self, &entryIp, port, timeout, connectCode);
    }
    self->expectIncoming.AddTail(entry);
  }
  self->ExpectIncomingGuard().Leave();
}

// 0x14033fda0: connect out to "host[:port]". Null if at the connection
// limit, the host does not resolve, or a connection to it already exists.
UdpConnection* ManagerEstablishConnection(UdpManager* self, const char* address, int port, int timeout) {
  self->GiveTimeGuard().Enter();
  char host[512];
  SplitHostPort(address, host, port);
  UdpConnection* result = nullptr;
  if (self->connectionList.count < self->MaxConnections()) {
    UdpIpAddress ip = 0;
    if (DriverGetHostByName(self, &ip, host)) {
      UdpIpAddress lookup = ip;
      if (UdpConnection* existing = GetConnection(self, &lookup, port)) {
        Ref(existing)->VirtualRelease();
      } else {
        void* memory = soeutil::Allocate(0x450);
        UdpIpAddress target = ip;
        result = memory ? ConstructOutgoing(memory, self, &target, port, timeout) : nullptr;
      }
    }
  }
  self->GiveTimeGuard().Leave();
  return result;
}

// 0x14033fb60: disconnect every connection (reason Application).
void ManagerDisconnectAll(UdpManager* self) {
  self->VirtualAddRef();
  self->ConnectionGuard().Enter();
  UdpConnection* c = self->connectionList.first;
  if (c) Ref(c)->VirtualAddRef();
  self->ConnectionGuard().Leave();
  while (c) {
    self->ConnectionGuard().Enter();
    UdpConnection* next = self->connectionList.Next(c);
    if (next) Ref(next)->VirtualAddRef();
    self->ConnectionGuard().Leave();
    Ref(c)->VirtualAddRef();
    auto& guard = ConnField<UdpPlatformGuardObject>(c, ConnectionOffsets::kStatusGuard);
    guard.Enter();
    ConnectionDisconnect(c, 0, kDisconnectReasonApplication);
    guard.Leave();
    Ref(c)->VirtualRelease();
    Ref(c)->VirtualRelease();
    c = next;
  }
  self->VirtualRelease();
}

// 0x140340000: next queued event (for applications delivering events
// themselves), or null.
CallbackEvent* ManagerPopEvent(UdpManager* self) {
  self->EventQueueGuard().Enter();
  CallbackEvent* event = self->eventQueue.RemoveHead();
  if (event) {
    if (event->packet)
      self->queuedEventBytes -= reinterpret_cast<int (*)(UdpRefCount*)>(event->packet->vtable[7])(event->packet);
    else if (event->payload)
      self->queuedEventBytes -= event->payload->length;
  }
  self->EventQueueGuard().Leave();
  return event;
}

// 0x14033fc60: hex dump of the receive ring, oldest first.
void ManagerDumpPacketHistory(UdpManager* self, void* string) {
  self->GiveTimeGuard().Enter();
  for (int i = 0; i < self->ReceiveBufferCount(); ++i) {
    UdpPacketBuffer* buffer = self->receiveBuffers[(self->receiveBufferIndex + i) % self->ReceiveBufferCount()];
    if (buffer->length > 0) {
      char text[256];
      StringAppendFormat(string, "%16s,%5d %3d: ", IpToString(&buffer->ip, text, sizeof(text)), buffer->port,
                         buffer->length);
      const uint8_t* byte = buffer->data;
      for (int n = buffer->length; n > 0; --n) StringAppendFormat(string, "%02x ", *byte++);
      StringAppend(string, reinterpret_cast<const char*>(0x142047048));  // "\n"
    }
  }
  self->GiveTimeGuard().Leave();
}

// 0x14033cfe0
void ManagerDestruct(UdpManager* self) {
  self->vtable = reinterpret_cast<void**>(0x142051778);
  self->ConnectionGuard().Enter();
  while (UdpConnection* c = self->connectionList.first) {
    Ref(c)->VirtualAddRef();
    ConnectionDisconnect(c, 0, kDisconnectReasonManagerDeleted);
    Ref(c)->VirtualRelease();
  }
  self->ConnectionGuard().Leave();

  self->DisconnectingGuard().Enter();
  ReleaseAllConnections(&self->disconnectingList);
  self->DisconnectingGuard().Leave();
  self->ExpectIncomingGuard().Enter();
  DeleteAllExpectIncoming(&self->expectIncoming);
  self->ExpectIncomingGuard().Leave();

  self->PoolGuard().Enter();
  while (PooledLogicalPacket* p = self->pooledCreated.RemoveHead())
    *reinterpret_cast<UdpManager**>(reinterpret_cast<uint8_t*>(p) + 0x30) = nullptr;  // orphan it
  ReleaseAllPooled(&self->pooledAvailable);
  self->PoolGuard().Leave();

  if (int linger = self->params.At<int>(0x54))
    reinterpret_cast<void (*)(UdpPlatformDriver*, int)>(self->driver->vtable[kSlotSleep])(self->driver, linger);
  reinterpret_cast<void (*)(UdpPlatformDriver*)>(self->driver->vtable[kSlotSocketClose])(self->driver);
  if (self->params.At<void*>(0x170) == nullptr && self->driver)  // we created the driver
    reinterpret_cast<void* (*)(UdpPlatformDriver*, unsigned)>(self->driver->vtable[0])(self->driver, 1);
  self->driver = nullptr;

  void** tables[2] = {reinterpret_cast<void**>(self->addressTable), reinterpret_cast<void**>(self->codeTable)};
  for (void** table : tables) {
    if (table) {
      soeutil::FreeArray(table[0]);
      soeutil::Free(table, 0x18);
    }
  }
  if (self->priorityQueue) {
    soeutil::FreeArray(self->priorityQueue->entries);
    soeutil::Free(self->priorityQueue, 0x10);
  }
  for (int i = 0; i < self->ReceiveBufferCount(); ++i) {
    if (UdpPacketBuffer* buffer = self->receiveBuffers[i]) {
      soeutil::FreeArray(buffer->data);
      soeutil::Free(buffer, sizeof(UdpPacketBuffer));
    }
  }
  soeutil::FreeArray(self->receiveBuffers);
  DeleteAllSimulated(&self->simulateQueue);
  DeleteAllSimulated(&self->simulateIncomingQueue);
  DeleteAllEvents(&self->eventPool);
  DeleteAllEvents(&self->eventQueue);

  if (auto* pool = game::Field<uint8_t*>(self, 0x610)) {
    MutexDestroy(pool + 0x518);
    uint8_t* map = pool + 0x478;
    *reinterpret_cast<void**>(map) = reinterpret_cast<void*>(0x142051818);  // Map<int,Block,1,0>
    DestroyPoolMapRoot(map);
    PoolBlockListDestruct(map + 0x18);
    *reinterpret_cast<void**>(map) = reinterpret_cast<void*>(0x1420517f0);  // Map<int,Block,-1,0>
    DestroyPoolMapRoot(map);
    PoolDestructBase(pool);
    soeutil::Free(pool, 0x560);
  }
  if (auto* small = game::Field<uint8_t*>(self, 0x618)) {
    MutexDestroy(small + 0x20);
    SmallPoolDestruct(small);
    soeutil::Free(small, 0x68);
  }

  self->eventQueue.vtable = kCallbackEventListVtable;
  self->eventPool.vtable = kCallbackEventListVtable;
  self->pooledAvailable.vtable = kPooledPacketListVtable;
  self->pooledCreated.vtable = kPooledPacketListVtable;
  self->simulateQueue.vtable = kSimulateListVtable;
  self->simulateIncomingQueue.vtable = kSimulateListVtable;
  for (int i = 11; i >= 0; --i) self->guards[i].Destruct();
  self->expectIncoming.vtable = kExpectIncomingListVtable;
  self->disconnectingList.vtable = kConnectionListVtable;
  self->connectionList.vtable = kConnectionListVtable;
  self->guard.Destruct();
  self->vtable = kUdpRefCountVtable;
}

// 0x14033d8c0, slot 4
UdpManager* ManagerDeletingDestructor(UdpManager* self, unsigned flags) {
  ManagerDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(UdpManager));
  return self;
}

REBUILD_FUNCTION(UdpManager_DeleteAllEvents, 0x14033f540, DeleteAllEvents);
REBUILD_FUNCTION(UdpManager_DeleteAllExpectIncoming, 0x14033f5f0, DeleteAllExpectIncoming);
REBUILD_FUNCTION(UdpManager_DeleteAllSimulated, 0x14033f680, DeleteAllSimulated);
REBUILD_FUNCTION(UdpManager_ReleaseAllConnections, 0x1403430b0, ReleaseAllConnections);
REBUILD_FUNCTION(UdpManager_ReleaseAllPooled, 0x140343020, ReleaseAllPooled);
REBUILD_FUNCTION(UdpManager_OpenSocket, 0x14033f2d0, ManagerOpenSocket);
REBUILD_FUNCTION(UdpManager_ExpectIncomingEntry_Construct, 0x14033bc40, ExpectIncomingConstruct);
REBUILD_FUNCTION(UdpManager_ExpectIncoming, 0x140340090, ManagerExpectIncoming);
REBUILD_FUNCTION(UdpManager_EstablishConnection, 0x14033fda0, ManagerEstablishConnection);
REBUILD_FUNCTION(UdpManager_DisconnectAll, 0x14033fb60, ManagerDisconnectAll);
REBUILD_FUNCTION(UdpManager_PopEvent, 0x140340000, ManagerPopEvent);
REBUILD_FUNCTION(UdpManager_DumpPacketHistory, 0x14033fc60, ManagerDumpPacketHistory);
REBUILD_FUNCTION(UdpManager_Destruct, 0x14033cfe0, ManagerDestruct);
REBUILD_FUNCTION(UdpManager_DeletingDestructor, 0x14033d8c0, ManagerDeletingDestructor);

}  // namespace rebuild::udp
