// Connection lifecycle bookkeeping shared by UdpManager and UdpConnection:
// scheduling, removal, the disconnecting list, terminate and flush.
#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

// Not rebuilt yet.
void PhysicalSend(UdpConnection* c, const uint8_t* data, int length, bool appendCrc) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int, bool)>(0x140348070)(c, data, length, appendCrc);
}
void PriorityQueueUpdate(ConnectionPriorityQueue* q, UdpConnection* c, int64_t time) {
  game::Call<void (*)(ConnectionPriorityQueue*, UdpConnection*, int64_t)>(0x140345830)(q, c, time);
}

}  // namespace

// 0x140345b20: "now" as this connection sees it.
int64_t ConnectionClock(UdpConnection* c) {
  if (UdpManager* manager = ConnField<UdpManager*>(c, O::kManager)) return manager->CachedClock();
  return ConnField<int64_t>(c, O::kCachedTime);
}

// 0x140349980: 00 05 <connect code BE32> <reason BE16>
void SendTerminatePacket(UdpConnection* c, uint32_t connectCode, uint16_t reason) {
  uint8_t packet[8] = {0x00,
                       0x05,
                       static_cast<uint8_t>(connectCode >> 24),
                       static_cast<uint8_t>(connectCode >> 16),
                       static_cast<uint8_t>(connectCode >> 8),
                       static_cast<uint8_t>(connectCode),
                       static_cast<uint8_t>(reason >> 8),
                       static_cast<uint8_t>(reason)};
  PhysicalSend(c, packet, sizeof(packet), true);
}

// 0x140346820. Sends whatever is waiting in the connection's multi-packet
// buffer. A buffer holding a single sub-packet (00 19 <len> <packet>) is sent
// as that packet alone, without the group header.
void FlushChannels(UdpConnection* c) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  uint8_t* start = ConnField<uint8_t*>(c, 0x250);
  int bytes = static_cast<int>(ConnField<uint8_t*>(c, 0x258) - start);
  if (bytes > 2) {
    const uint8_t* data = start;
    if (start[2] + 3 == bytes) {
      bytes -= 3;
      data += 3;
    }
    PhysicalSend(c, data, bytes, true);
    for (size_t channel = 0x1D0; channel <= 0x1E8; channel += 8) {
      if (uint8_t* reliable = ConnField<uint8_t*>(c, channel)) game::Field<int64_t>(reliable, 0x118) = 0;
    }
  }
  ConnField<uint8_t*>(c, 0x258) = start;
  guard.Leave();
}

// 0x1403499e0: (re)queue a connection for time no earlier than the pass
// currently being processed.
void ManagerScheduleConnection(UdpManager* self, UdpConnection* c, int64_t time) {
  self->ConnectionGuard().Enter();
  if (time < self->priorityProcessTime) time = self->priorityProcessTime;
  if (self->priorityQueue) PriorityQueueUpdate(self->priorityQueue, c, time);
  self->ConnectionGuard().Leave();
}

// 0x1403421d0
void ManagerAddDisconnecting(UdpManager* self, UdpConnection* c) {
  self->DisconnectingGuard().Enter();
  Ref(c)->VirtualAddRef();
  self->disconnectingList.AddTail(c);
  self->DisconnectingGuard().Leave();
}

// 0x1403437b0: the manager forgets a connection entirely and drops its
// reference.
void ManagerRemoveConnection(UdpManager* self, UdpConnection* c) {
  self->ConnectionGuard().Enter();
  ConnectionPriorityQueue* queue = self->priorityQueue;
  int& heapIndex = ConnectionPriorityQueue::HeapIndex(c);
  if (queue && heapIndex != -1) {
    int last = --queue->count;
    int index = heapIndex;
    if (index != last) {
      queue->entries[index] = queue->entries[last];
      ConnectionPriorityQueue::HeapIndex(queue->entries[index].connection) = index;
      Reprioritize(queue, queue->entries[index].connection);
    }
    heapIndex = -1;
  }
  self->connectionList.Remove(c);
  self->addressTable->Remove(c);
  self->codeTable->Remove(c);
  Ref(c)->VirtualRelease();
  self->ConnectionGuard().Leave();
}

// 0x1403433e0: the connect-code table's out-of-line Remove.
static bool CodeTableRemove(ConnectionCodeTable* table, UdpConnection* c) { return table->Remove(c); }

REBUILD_FUNCTION(UdpConnection_Clock, 0x140345b20, ConnectionClock);
REBUILD_FUNCTION(UdpConnection_SendTerminatePacket, 0x140349980, SendTerminatePacket);
REBUILD_FUNCTION(UdpConnection_FlushChannels, 0x140346820, FlushChannels);
REBUILD_FUNCTION(UdpManager_ScheduleConnection, 0x1403499e0, ManagerScheduleConnection);
REBUILD_FUNCTION(UdpManager_AddDisconnecting, 0x1403421d0, ManagerAddDisconnecting);
REBUILD_FUNCTION(UdpManager_RemoveConnection, 0x1403437b0, ManagerRemoveConnection);
REBUILD_FUNCTION(UdpManager_CodeTable_Remove, 0x1403433e0, CodeTableRemove);

}  // namespace rebuild::udp
