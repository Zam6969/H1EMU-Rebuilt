#include "udp/UdpManager.h"

#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

constexpr float kPercentScale = 100.0f;  // 0x1425ba0a0: percent -> parts per 10000

int& ConnectionSimulateBytes(UdpRefCount* connection) {
  return game::Field<int>(connection, UdpConnectionFields::kSimulateBytes);
}

bool LoseSimulatedPacket(UdpManager* self, float lossPercent) {
  int roll = Random(&self->randomSeed);
  return static_cast<float>(roll % 10000) < lossPercent * kPercentScale;
}

// Advances a 40 x 25 ms bandwidth window to `now`, clearing buckets that
// slid out of it. Shared body of the two functions below.
void AdvanceBuckets(int64_t now, int64_t& bucketTime, int& total, int* buckets) {
  int64_t bucket = now / 25;
  int64_t elapsed = bucket - bucketTime;
  if (elapsed <= UdpManager::kBandwidthBuckets) {
    for (int64_t i = 0; i < elapsed; ++i) {
      int index = static_cast<int>((i + bucket) % UdpManager::kBandwidthBuckets);
      total -= buckets[index];
      buckets[index] = 0;
    }
  } else {
    std::memset(buckets, 0, sizeof(int) * UdpManager::kBandwidthBuckets);
    total = 0;
  }
  bucketTime = bucket;
}

}  // namespace

// 0x140340330
void UpdateSendBuckets(UdpManager* self) {
  AdvanceBuckets(self->CachedClock(), self->sendBucketTime, self->sendBucketTotal,
                 self->sendBuckets);
}

// 0x140340230
void UpdateReceiveBuckets(UdpManager* self) {
  AdvanceBuckets(self->CachedClock(), self->receiveBucketTime, self->receiveBucketTotal,
                 self->receiveBuckets);
}

// 0x14033bd40
SimulateQueueEntry* ConstructSimulateQueueEntry(SimulateQueueEntry* self, const uint8_t* data,
                                                int length, const UdpIpAddress* ip, int port,
                                                int64_t queueTime) {
  self->prev = nullptr;
  self->next = nullptr;
  self->ip = 0;
  self->data = static_cast<uint8_t*>(soeutil::AllocateArray(length));
  self->length = length;
  std::memcpy(self->data, data, length);
  self->ip = *ip;
  self->port = port;
  self->queueTime = queueTime;
  return self;
}

// 0x1403425c0: called by every new PooledLogicalPacket.
void PoolCreated(UdpManager* self, PooledLogicalPacket* packet) {
  self->PoolGuard().Enter();
  self->pooledCreated.AddHead(packet);
  self->PoolGuard().Leave();
}

// 0x140342650: called when a pooled packet is finally deleted.
void PoolDestroyed(UdpManager* self, PooledLogicalPacket* packet) {
  self->PoolGuard().Enter();
  self->pooledCreated.Remove(packet);
  self->PoolGuard().Leave();
}

// 0x1403426f0: a released pooled packet goes back on the available list,
// unless the pool is already at its configured maximum, in which case the
// pool's reference is dropped (deleting it).
void PoolReturn(UdpManager* self, PooledLogicalPacket* packet) {
  self->PoolGuard().Enter();
  if (self->pooledAvailable.count < self->PooledPacketMax()) {
    self->pooledAvailable.AddHead(packet);
  } else {
    // Devirtualized UdpRefCount::Release (0x14034c300).
    if (--packet->refCount == 0) packet->VirtualDelete();
  }
  self->PoolGuard().Leave();
}

// 0x14033d970. Reads one datagram into the next ring buffer, or returns null
// when nothing is waiting (or the simulated incoming byte rate says wait).
// Simulated incoming loss reads and discards datagrams.
UdpPacketBuffer* ActualReceive(UdpManager* self) {
  int64_t now = self->CachedClock();
  if (self->simulateIncomingByteRate > 0 && now < self->simulateIncomingNextTime) return nullptr;

  UdpIpAddress ip = 0;
  uint32_t port = 0;
  int index;
  int received;
  for (;;) {
    index = self->receiveBufferIndex;
    using ReceiveFn = int (*)(UdpPlatformDriver*, uint8_t*, int, UdpIpAddress*, uint32_t*);
    received = reinterpret_cast<ReceiveFn>(self->driver->vtable[kSlotSocketReceive])(
        self->driver, self->receiveBuffers[index]->data, self->MaxRawPacketSize(), &ip, &port);
    if (received < 0) return nullptr;
    if (self->simulateIncomingLossPercent <= 0.0f) break;
    if (!LoseSimulatedPacket(self, self->simulateIncomingLossPercent)) break;
  }

  if (self->simulateIncomingByteRate > 0)
    self->simulateIncomingNextTime = (received * 1000) / self->simulateIncomingByteRate + now;

  UdpPacketBuffer* buffer = self->receiveBuffers[index];
  buffer->length = received;
  buffer->ip = ip;
  buffer->port = static_cast<int>(port);
  self->receiveBufferIndex = (self->receiveBufferIndex + 1) % self->ReceiveBufferCount();

  self->StatsGuard().Enter();
  self->lastReceiveTime = now;
  self->bytesReceived += received;
  self->packetsReceived += 1;
  UpdateReceiveBuckets(self);
  self->receiveBuckets[self->receiveBucketTime % UdpManager::kBandwidthBuckets] += received;
  self->receiveBucketTotal += received;
  self->StatsGuard().Leave();
  return self->receiveBuffers[index];
}

// 0x14033dba0. Sends a datagram, or with outgoing simulation enabled queues
// it (subject to the global and per-connection queue limits) for later.
void ActualSend(UdpManager* self, const uint8_t* data, int length, const UdpIpAddress* ip,
                int port) {
  self->StatsGuard().Enter();
  self->lastSendTime = self->CachedClock();
  self->bytesSent += length;
  self->packetsSent += 1;
  self->StatsGuard().Leave();

  if (self->simulateOutgoingByteRate == 0 && self->simulateOutgoingLatency == 0) {
    UdpIpAddress copy = *ip;
    ActualSendHelper(self, data, length, &copy, port);
    return;
  }

  self->SimulateGuard().Enter();
  if (self->simulateOutgoingQueueMax < 1 ||
      self->simulateQueueBytes + length <= self->simulateOutgoingQueueMax) {
    UdpIpAddress copy = *ip;
    if (auto* connection = reinterpret_cast<UdpRefCount*>(GetConnection(self, &copy, port))) {
      if (self->simulateConnectionQueueMax > 0 &&
          self->simulateConnectionQueueMax < ConnectionSimulateBytes(connection) + length) {
        connection->VirtualRelease();
        self->SimulateGuard().Leave();
        return;
      }
      ConnectionSimulateBytes(connection) += length;
      connection->VirtualRelease();
    }
    self->simulateQueueBytes += length;

    auto* entry = static_cast<SimulateQueueEntry*>(soeutil::Allocate(sizeof(SimulateQueueEntry)));
    if (entry) {
      self->StatsGuard().Enter();
      int64_t sendTime = self->lastSendTime;
      self->StatsGuard().Leave();
      UdpIpAddress entryIp = *ip;
      ConstructSimulateQueueEntry(entry, data, length, &entryIp, port, sendTime);
    }
    self->simulateQueue.AddTail(entry);

    if (self->simulateQueue.count == 1) {
      self->StatsGuard().Enter();
      int64_t sendTime = self->lastSendTime;
      self->StatsGuard().Leave();
      int64_t due = self->simulateOutgoingLatency + sendTime;
      if (due < self->simulateOutgoingNextTime) due = self->simulateOutgoingNextTime;
      self->simulateOutgoingNextTime = due;
    }
  }
  self->SimulateGuard().Leave();
}

// 0x14033de20. The last step before the socket: simulated outgoing loss,
// the send itself, then the bandwidth statistics.
void ActualSendHelper(UdpManager* self, const uint8_t* data, int length, const UdpIpAddress* ip,
                      int port) {
  if (self->simulateOutgoingLossPercent > 0.0f &&
      LoseSimulatedPacket(self, self->simulateOutgoingLossPercent))
    return;

  using SendFn = bool (*)(UdpPlatformDriver*, const uint8_t*, int, const UdpIpAddress*, int);
  bool sent = reinterpret_cast<SendFn>(self->driver->vtable[kSlotSocketSend])(self->driver, data,
                                                                               length, ip, port);
  self->StatsGuard().Enter();
  if (!sent) {
    self->sendFailures += 1;
  } else {
    UpdateSendBuckets(self);
    self->sendBuckets[self->sendBucketTime % UdpManager::kBandwidthBuckets] += length;
    self->sendBucketTotal += length;
  }
  self->StatsGuard().Leave();
}

REBUILD_FUNCTION(UdpManager_PoolCreated, 0x1403425c0, PoolCreated);
REBUILD_FUNCTION(UdpManager_PoolDestroyed, 0x140342650, PoolDestroyed);
REBUILD_FUNCTION(UdpManager_PoolReturn, 0x1403426f0, PoolReturn);
REBUILD_FUNCTION(UdpManager_UpdateSendBuckets, 0x140340330, UpdateSendBuckets);
REBUILD_FUNCTION(UdpManager_UpdateReceiveBuckets, 0x140340230, UpdateReceiveBuckets);
REBUILD_FUNCTION(UdpManager_SimulateQueueEntry_Construct, 0x14033bd40, ConstructSimulateQueueEntry);
REBUILD_FUNCTION(UdpManager_ActualReceive, 0x14033d970, ActualReceive);
REBUILD_FUNCTION(UdpManager_ActualSend, 0x14033dba0, ActualSend);
REBUILD_FUNCTION(UdpManager_ActualSendHelper, 0x14033de20, ActualSendHelper);

}  // namespace rebuild::udp
