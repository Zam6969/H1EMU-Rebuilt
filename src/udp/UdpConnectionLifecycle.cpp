// Connection lifecycle bookkeeping shared by UdpManager and UdpConnection:
// scheduling, removal, the disconnecting list, terminate and flush.
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

const uint32_t* const kCrcTable = reinterpret_cast<const uint32_t*>(0x1429fb8c0);  // CRC-32 (0xEDB88320)

struct MethodSlot {
  uintptr_t function;
  int64_t thisAdjust;
  int64_t padding;
};

}  // namespace

// 0x14034b470. CRC-32 of the data, with the CRC register first primed by
// the four bytes of `seed` (the connection's encrypt code), low byte first.
uint32_t Crc32(const uint8_t* data, int length, uint32_t seed) {
  uint32_t crc = 0xFFFFFFFF;
  for (int shift = 0; shift < 32; shift += 8)
    crc = kCrcTable[(crc ^ (seed >> shift)) & 0xFF] ^ (crc >> 8);
  if (data + length >= data) {  // the original treats a wrapping length as 0
    for (int i = 0; i < length; ++i) crc = kCrcTable[(data[i] ^ crc) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

// 0x140346630
void ConnectionUpdateSendBuckets(UdpConnection* c) {
  AdvanceBandwidthBuckets(ConnectionClock(c), ConnField<int64_t>(c, 0x2F8), ConnField<int>(c, 0x308),
                          &ConnField<int>(c, 0x310));
}

// 0x1403495a0. The datagram leaves through the manager; the connection
// records it in its statistics and asks to be scheduled again.
void PhysicalSendFinal(UdpConnection* c, const uint8_t* data, int length) {
  UdpIpAddress ip = ConnField<UdpIpAddress>(c, O::kIp);
  ActualSend(ConnField<UdpManager*>(c, O::kManager), data, length, &ip, ConnField<int>(c, O::kPort));
  ConnField<int64_t>(c, 0x100) += 1;       // packets sent
  ConnField<int64_t>(c, 0xF0) += length;   // bytes sent
  int64_t now = ConnectionClock(c);
  ConnField<int64_t>(c, 0x238) = now;      // last send
  ConnField<int64_t>(c, 0x248) = now;
  ConnectionUpdateSendBuckets(c);
  ConnField<int>(c, 0x310 + static_cast<size_t>(ConnField<int64_t>(c, 0x2F8) % 40) * 4) += length;
  ConnField<int>(c, 0x308) += length;
  if (!ConnField<uint8_t>(c, O::kInGiveTime) && ConnField<UdpManager*>(c, O::kManager))
    ManagerScheduleConnection(ConnField<UdpManager*>(c, O::kManager), c, 0);
}

// 0x140345830. Queues `c` for `time`, or moves it if already queued.
// Returns null only when the heap is full.
UdpConnection* PriorityQueueUpdate(ConnectionPriorityQueue* queue, UdpConnection* c, int64_t time) {
  int index = ConnectionPriorityQueue::HeapIndex(c);
  if (index == -1) {
    if (queue->count >= queue->capacity) return nullptr;
    queue->entries[queue->count] = {c, time};
    ConnectionPriorityQueue::HeapIndex(queue->entries[queue->count].connection) = queue->count;
    ++queue->count;
  } else {
    if (queue->entries[index].time == time) return c;
    queue->entries[index].time = time;
  }
  Reprioritize(queue, c);
  return c;
}

// 0x140348070. Runs a datagram through both encryption passes and appends
// the CRC, then hands it to the final send. Only connected or
// disconnect-pending connections send. `writable` says the caller's buffer
// has room for the CRC; otherwise it is copied first.
void PhysicalSend(UdpConnection* c, const uint8_t* data, int length, bool writable) {
  int status = ConnField<int>(c, UdpConnectionInternals::kStatus);
  if (!ConnField<UdpManager*>(c, O::kManager) || (status != kStatusConnected && status != 3)) return;

  uint8_t buffers[2][0x2004];
  uint8_t* out = const_cast<uint8_t*>(data);
  for (int pass = 0; pass < 2; ++pass) {
    if (ConnField<int>(c, O::kEncryptMethods + pass * 4) == kEncryptNone) continue;
    uint8_t* buffer = buffers[pass % 2];
    // Overrun sentinel past the largest possible encrypted size.
    std::memset(buffer + length + ConnField<int>(c, O::kEncryptExpansionBytes), 0xCE, 4);
    auto& method = ConnField<MethodSlot>(c, O::kEncryptPasses + pass * sizeof(MethodSlot));
    using CipherFn = int (*)(void*, uint8_t*, const uint8_t*, int);
    auto cipher = reinterpret_cast<CipherFn>(method.function);
    void* self = reinterpret_cast<uint8_t*>(c) + static_cast<int>(method.thisAdjust);

    // The opcode prefix (one byte, or two for 00 xx protocol packets) stays
    // in the clear.
    buffer[0] = out[0];
    uint8_t* body;
    int produced;
    if (out[0] == 0) {
      buffer[1] = out[1];
      body = buffer + 2;
      produced = cipher(self, body, out + 2, length - 2);
      if (!ConnField<UdpManager*>(c, O::kManager)) return;
    } else {
      body = buffer + 1;
      produced = cipher(self, body, out + 1, length - 1);
    }
    if (produced == -1) return;
    writable = true;
    length = static_cast<int>(body + produced - buffer);
    out = buffer;
  }

  int crcBytes = ConnField<int>(c, 0x1F4);
  if (crcBytes > 0) {
    if (!writable) {
      std::memcpy(buffers[0], out, length);
      out = buffers[0];
    }
    uint32_t crc = Crc32(out, length, ConnField<uint32_t>(c, O::kEncryptCode));
    uint8_t* tail = out + length;
    switch (crcBytes) {
      case 1: tail[0] = static_cast<uint8_t>(crc); break;
      case 2: tail[0] = static_cast<uint8_t>(crc >> 8); tail[1] = static_cast<uint8_t>(crc); break;
      case 3:
        tail[0] = static_cast<uint8_t>(crc >> 16);
        tail[1] = static_cast<uint8_t>(crc >> 8);
        tail[2] = static_cast<uint8_t>(crc);
        break;
      case 4:
        tail[0] = static_cast<uint8_t>(crc >> 24);
        tail[1] = static_cast<uint8_t>(crc >> 16);
        tail[2] = static_cast<uint8_t>(crc >> 8);
        tail[3] = static_cast<uint8_t>(crc);
        break;
    }
    length += crcBytes;
  }
  PhysicalSendFinal(c, out, length);
}

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
REBUILD_FUNCTION(UdpConnection_PhysicalSend, 0x140348070, PhysicalSend);
REBUILD_FUNCTION(UdpConnection_PhysicalSendFinal, 0x1403495a0, PhysicalSendFinal);
REBUILD_FUNCTION(UdpConnection_UpdateSendBuckets, 0x140346630, ConnectionUpdateSendBuckets);
REBUILD_FUNCTION(UdpMisc_Crc32, 0x14034b470, Crc32);
REBUILD_FUNCTION(UdpManager_PriorityQueue_Update, 0x140345830, PriorityQueueUpdate);
REBUILD_FUNCTION(UdpConnection_SendTerminatePacket, 0x140349980, SendTerminatePacket);
REBUILD_FUNCTION(UdpConnection_FlushChannels, 0x140346820, FlushChannels);
REBUILD_FUNCTION(UdpManager_ScheduleConnection, 0x1403499e0, ManagerScheduleConnection);
REBUILD_FUNCTION(UdpManager_AddDisconnecting, 0x1403421d0, ManagerAddDisconnecting);
REBUILD_FUNCTION(UdpManager_RemoveConnection, 0x1403437b0, ManagerRemoveConnection);
REBUILD_FUNCTION(UdpManager_CodeTable_Remove, 0x1403433e0, CodeTableRemove);

}  // namespace rebuild::udp
