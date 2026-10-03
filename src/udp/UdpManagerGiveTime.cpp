// UdpManager::GiveTime - the manager's per-frame pump.
#include <climits>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpManager.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

// Not rebuilt yet.
void ConnectionGiveTime(UdpConnection* connection, bool fromManager) {
  game::Call<void (*)(UdpConnection*, bool)>(0x140346e80)(connection, fromManager);
}

UdpRefCount* Ref(UdpConnection* connection) { return reinterpret_cast<UdpRefCount*>(connection); }

int ClampToInt(int64_t value) { return value > INT_MAX ? INT_MAX : static_cast<int>(value); }

int64_t RefreshClock(UdpManager* self) { return ManagerClock(self); }

void SwapEntries(ConnectionPriorityQueue* queue, int a, int b) {
  ConnectionPriorityQueue::Entry temp = queue->entries[a];
  queue->entries[a] = queue->entries[b];
  queue->entries[b] = temp;
  ConnectionPriorityQueue::HeapIndex(queue->entries[a].connection) = a;
  ConnectionPriorityQueue::HeapIndex(queue->entries[b].connection) = b;
}

}  // namespace

// 0x14033f030: reads the driver clock into the cache and returns it.
int64_t ManagerClock(UdpManager* self) {
  self->ClockGuard().Enter();
  int64_t now = reinterpret_cast<int64_t (*)(UdpPlatformDriver*)>(self->driver->vtable[kSlotClock])(
      self->driver);
  self->cachedClock = now;
  self->ClockGuard().Leave();
  return now;
}

// 0x14033f0a0: ms since `since` by a fresh clock read, clamped to INT_MAX.
int ManagerClockElapsed(UdpManager* self, int64_t since) { return ClampToInt(ManagerClock(self) - since); }

// 0x140342e50. Restores heap order after `connection`'s time changed: sift
// up toward the root, and only if it did not move, sift down.
void Reprioritize(ConnectionPriorityQueue* queue, UdpConnection* connection) {
  int index = ConnectionPriorityQueue::HeapIndex(connection);
  if (index > 0) {
    bool moved = false;
    for (;;) {
      int parent = (index - 1) / 2;
      if (queue->entries[parent].time <= queue->entries[index].time) break;
      SwapEntries(queue, index, parent);
      moved = true;
      index = parent;
      if (parent < 1) return;
    }
    if (moved) return;
  }
  for (int left = index * 2 + 1; left < queue->count; left = index * 2 + 1) {
    int right = index * 2 + 2;
    int child = left;
    if (right < queue->count && queue->entries[right].time <= queue->entries[left].time) child = right;
    if (queue->entries[index].time <= queue->entries[child].time) return;
    SwapEntries(queue, index, child);
    index = child;
  }
}

// 0x1403449d0. Next datagram to process: straight from the socket, or with
// incoming latency simulation through a delay queue.
UdpPacketBuffer* NextIncomingPacket(UdpManager* self) {
  if (self->simulateIncomingQueue.count == 0 && self->simulateIncomingLatency == 0)
    return ActualReceive(self);

  int64_t now = self->CachedClock();
  for (UdpPacketBuffer* buffer = ActualReceive(self); buffer; buffer = ActualReceive(self)) {
    auto* entry = static_cast<SimulateQueueEntry*>(soeutil::Allocate(sizeof(SimulateQueueEntry)));
    if (entry) {
      UdpIpAddress ip = buffer->ip;
      ConstructSimulateQueueEntry(entry, buffer->data, buffer->length, &ip, buffer->port, now);
    }
    self->simulateIncomingQueue.AddTail(entry);
  }

  SimulateQueueEntry* entry = self->simulateIncomingQueue.first;
  if (!entry || ClampToInt(self->CachedClock() - entry->queueTime) < self->simulateIncomingLatency)
    return nullptr;

  self->simulateIncomingQueue.Remove(entry);
  UdpPacketBuffer* buffer = self->receiveBuffers[self->receiveBufferIndex];
  std::memcpy(buffer->data, entry->data, entry->length);
  buffer->length = entry->length;
  buffer->ip = entry->ip;
  buffer->port = entry->port;
  self->receiveBufferIndex = (self->receiveBufferIndex + 1) % self->ReceiveBufferCount();
  soeutil::FreeArray(entry->data);
  soeutil::Free(entry, sizeof(SimulateQueueEntry));
  return buffer;
}

// 0x1403427f0. Releases the manager's reference to connections on the
// disconnecting list once they have reached cStatusDisconnected.
void ReleaseDisconnectedConnections(UdpManager* self) {
  self->DisconnectingGuard().Enter();
  for (UdpConnection* connection = self->disconnectingList.first; connection;) {
    UdpConnection* next = self->disconnectingList.Next(connection);
    auto& guard = game::Field<UdpPlatformGuardObject>(connection, UdpConnectionInternals::kGuard);
    guard.Enter();
    int status = game::Field<int>(connection, UdpConnectionInternals::kStatus);
    guard.Leave();
    if (status == 2) {
      self->disconnectingList.Remove(connection);
      Ref(connection)->VirtualRelease();
    }
    connection = next;
  }
  self->DisconnectingGuard().Leave();
}

// 0x140341310. Sends the punch-through probe; returns true once the entry has
// timed out and can be deleted.
bool ExpectIncomingGiveTime(ExpectIncomingEntry* self) {
  if (ManagerClockElapsed(self->manager, self->startTime) > self->timeout) return true;
  if (self->connectCode != 0) {
    if (self->lastSendTime != 0 && ManagerClockElapsed(self->manager, self->lastSendTime) <= 1000) return false;
    self->lastSendTime = ManagerClock(self->manager);
    uint8_t packet[6] = {0x00, 0x1F,
                         static_cast<uint8_t>(self->connectCode >> 24),
                         static_cast<uint8_t>(self->connectCode >> 16),
                         static_cast<uint8_t>(self->connectCode >> 8),
                         static_cast<uint8_t>(self->connectCode)};
    UdpIpAddress ip = self->ip;
    ActualSend(self->manager, packet, sizeof(packet), &ip, self->port);
  }
  return false;
}

// 0x140342900. Runs every expect-incoming entry and deletes finished ones.
void ProcessExpectIncoming(UdpManager* self) {
  self->ExpectIncomingGuard().Enter();
  for (ExpectIncomingEntry* entry = self->expectIncoming.first; entry;) {
    ExpectIncomingEntry* next = self->expectIncoming.Next(entry);
    if (ExpectIncomingGiveTime(entry)) {
      self->expectIncoming.Remove(entry);
      if (entry) soeutil::Free(entry, sizeof(ExpectIncomingEntry));
    }
    entry = next;
  }
  self->ExpectIncomingGuard().Leave();
}

namespace {

void PriorityQueueReprioritize(ConnectionPriorityQueue* queue, UdpConnection* connection) {
  Reprioritize(queue, connection);
}
void AfterConnectionsPassA(UdpManager* self) { ReleaseDisconnectedConnections(self); }
void AfterConnectionsPassB(UdpManager* self) { ProcessExpectIncoming(self); }

// Pops the earliest connection if it is due by `now`, with a reference added.
UdpConnection* PopDueConnection(UdpManager* self, int64_t now) {
  ConnectionPriorityQueue* queue = self->priorityQueue;
  if (queue->count < 1 || now < queue->entries[0].time) return nullptr;
  UdpConnection* top = queue->entries[0].connection;
  int index = ConnectionPriorityQueue::HeapIndex(top);
  if (index != -1) {
    int last = --queue->count;
    if (index != last) {
      queue->entries[index] = queue->entries[last];
      ConnectionPriorityQueue::HeapIndex(queue->entries[index].connection) = index;
      PriorityQueueReprioritize(queue, queue->entries[index].connection);
    }
    ConnectionPriorityQueue::HeapIndex(top) = -1;
  }
  if (top) Ref(top)->VirtualAddRef();
  return top;
}

void GiveConnectionsTime(UdpManager* self) {
  if (!self->priorityQueue) {
    // Every connection, walking the list with a reference on the current one.
    self->ConnectionGuard().Enter();
    UdpConnection* connection = self->connectionList.first;
    if (connection) Ref(connection)->VirtualAddRef();
    self->ConnectionGuard().Leave();
    while (connection) {
      ConnectionGiveTime(connection, true);
      self->ConnectionGuard().Enter();
      UdpConnection* next = self->connectionList.Next(connection);
      if (next) Ref(next)->VirtualAddRef();
      self->ConnectionGuard().Leave();
      Ref(connection)->VirtualRelease();
      connection = next;
    }
  } else {
    // Only connections whose scheduled time has come.
    int64_t now = self->CachedClock();
    self->ConnectionGuard().Enter();
    self->priorityProcessTime = now + 1;
    self->ConnectionGuard().Leave();
    int processed = 0;
    for (;;) {
      self->ConnectionGuard().Enter();
      UdpConnection* connection = PopDueConnection(self, now);
      self->ConnectionGuard().Leave();
      if (!connection) break;
      ConnectionGiveTime(connection, true);
      Ref(connection)->VirtualRelease();
      ++processed;
    }
    self->StatsGuard().Enter();
    self->priorityConnectionsProcessed += processed;
    self->priorityConnectionsTotal += self->connectionList.count;
    self->StatsGuard().Leave();
  }
  AfterConnectionsPassA(self);
  AfterConnectionsPassB(self);
}

// Sends every simulated-latency datagram whose time has come.
void FlushSimulateQueue(UdpManager* self) {
  self->SimulateGuard().Enter();
  int64_t now = self->CachedClock();
  SimulateQueueEntry* entry = self->simulateQueue.first;
  while (entry && now >= self->simulateOutgoingNextTime) {
    self->simulateQueue.Remove(entry);
    SimulateQueueEntry* next = self->simulateQueue.first;
    if (next) {
      int age = ClampToInt(self->CachedClock() - next->queueTime);
      self->simulateOutgoingNextTime = (self->simulateOutgoingLatency - age) + now;
    }
    if (self->simulateOutgoingByteRate > 0) {
      int64_t due = (entry->length * 1000) / self->simulateOutgoingByteRate + now;
      if (due < self->simulateOutgoingNextTime) due = self->simulateOutgoingNextTime;
      self->simulateOutgoingNextTime = due;
    }
    UdpIpAddress ip = entry->ip;
    ActualSendHelper(self, entry->data, entry->length, &ip, entry->port);
    UdpIpAddress lookup = entry->ip;
    if (auto* connection = Ref(GetConnection(self, &lookup, entry->port))) {
      game::Field<int>(connection, UdpConnectionFields::kSimulateBytes) -= entry->length;
      connection->VirtualRelease();
    }
    self->simulateQueueBytes -= entry->length;
    soeutil::FreeArray(entry->data);
    soeutil::Free(entry, sizeof(SimulateQueueEntry));
    entry = next;
  }
  self->SimulateGuard().Leave();
}

}  // namespace

// 0x1403413c0. Polls the socket for up to maxPollingTime ms, optionally
// gives every connection time, and flushes the latency simulator. Returns
// whether any packet was processed. Re-entrant calls (from a callback) do
// nothing.
bool GiveTime(UdpManager* self, int maxPollingTime, bool giveConnectionsTime) {
  self->VirtualAddRef();
  self->GiveTimeGuard().Enter();
  RefreshClock(self);

  bool result = false;
  if (!self->EventQueuing() && self->eventQueue.count > 0) {
    DeliverEvents(self, maxPollingTime);
    result = true;
  } else if (!self->inGiveTime) {
    self->inGiveTime = true;
    self->StatsGuard().Enter();
    self->giveTimeCalls += 1;
    self->StatsGuard().Leave();

    bool timedOut = false;
    if (maxPollingTime != 0) {
      int64_t start = self->CachedClock();
      bool processedAny = false;
      UdpPacketBuffer* buffer = NextIncomingPacket(self);
      if (buffer) {
        result = true;
        do {
          self->lastPollDelta = ClampToInt(self->CachedClock() - self->lastPollTime);
          ProcessRawPacket(self, buffer);
          int elapsed = ClampToInt(RefreshClock(self) - start);
          if (maxPollingTime <= elapsed) {
            self->StatsGuard().Enter();
            self->pollTimeouts += 1;
            self->StatsGuard().Leave();
            timedOut = true;
            break;
          }
          buffer = NextIncomingPacket(self);
          processedAny = true;
        } while (buffer);
      }
      if (!timedOut) {
        result = processedAny;
        self->lastPollTime = self->CachedClock();
      }
    }

    if (giveConnectionsTime) GiveConnectionsTime(self);
    FlushSimulateQueue(self);
    self->inGiveTime = false;
  }

  self->GiveTimeGuard().Leave();
  self->VirtualRelease();
  return result;
}

REBUILD_FUNCTION(UdpManager_GiveTime, 0x1403413c0, GiveTime);
REBUILD_FUNCTION(UdpManager_Clock, 0x14033f030, ManagerClock);
REBUILD_FUNCTION(UdpManager_ClockElapsed, 0x14033f0a0, ManagerClockElapsed);
REBUILD_FUNCTION(UdpManager_Reprioritize, 0x140342e50, Reprioritize);
REBUILD_FUNCTION(UdpManager_NextIncomingPacket, 0x1403449d0, NextIncomingPacket);
REBUILD_FUNCTION(UdpManager_ReleaseDisconnectedConnections, 0x1403427f0, ReleaseDisconnectedConnections);
REBUILD_FUNCTION(UdpManager_ProcessExpectIncoming, 0x140342900, ProcessExpectIncoming);
REBUILD_FUNCTION(UdpManager_ExpectIncomingGiveTime, 0x140341310, ExpectIncomingGiveTime);

}  // namespace rebuild::udp
