#pragma once

#include <cstddef>
#include <cstdint>

#include "udp/UdpHashTable.h"
#include "udp/UdpLinkedList.h"
#include "udp/UdpParams.h"
#include "udp/UdpRefCount.h"

// UdpLibrary::UdpManager (vtable 0x142051778, size 0x660). Partially
// reconstructed: unknown ranges are opaque padding until their users are
// rebuilt. Constructor 0x14033bdd0, destructor 0x14033cfe0.
namespace rebuild::udp {

struct UdpPlatformDriver;
struct PooledLogicalPacket;
struct UdpConnection;

// UdpLibrary::UdpIpAddress is a 4-byte class with a user-defined copy
// constructor, so MSVC passes it by hidden pointer; rebuilt signatures take
// `const uint32_t*` (network byte order) in its place.
using UdpIpAddress = uint32_t;

// One slot of the rotating receive buffer ring (0x18 bytes).
struct UdpPacketBuffer {
  uint8_t* data;    // +0x00 maxRawPacketSize bytes
  UdpIpAddress ip;  // +0x08
  int port;         // +0x0C
  int length;       // +0x10
};
static_assert(sizeof(UdpPacketBuffer) == 0x18);

// UdpManager::SimulateQueueEntry (0x30 bytes): a delayed outgoing datagram.
struct SimulateQueueEntry {
  SimulateQueueEntry* prev;  // +0x00 list link (offset 0)
  SimulateQueueEntry* next;  // +0x08
  uint8_t* data;             // +0x10 operator new[]
  int length;                // +0x18
  UdpIpAddress ip;           // +0x1C
  int port;                  // +0x20
  int64_t queueTime;         // +0x28
};
static_assert(offsetof(SimulateQueueEntry, ip) == 0x1C);
static_assert(sizeof(SimulateQueueEntry) == 0x30);

// UdpConnection fields the manager touches (UdpConnection itself is not
// reconstructed yet).
struct UdpConnectionFields {
  static constexpr size_t kAddressKey = 0x20, kAddressNext = 0x28;
  static constexpr size_t kCodeKey = 0x30, kCodeNext = 0x38;
  static constexpr size_t kIp = 0xA0, kPort = 0xA4, kSimulateBytes = 0xA8;
  static constexpr size_t kListLink = 0xB0;
  static constexpr size_t kConnectCode = 0xE8;
};

using ConnectionAddressTable =
    UdpHashTable<UdpConnection, UdpConnectionFields::kAddressKey, UdpConnectionFields::kAddressNext>;
using ConnectionCodeTable =
    UdpHashTable<UdpConnection, UdpConnectionFields::kCodeKey, UdpConnectionFields::kCodeNext>;

struct UdpManager;

// UdpManager::ExpectIncomingEntry: NAT punch-through for a connection the
// application expects from this address. Sends a probe (00 1F + connect code)
// once a second until the entry times out, so our NAT accepts the peer's
// connect request.
struct ExpectIncomingEntry {
  ExpectIncomingEntry* prev;  // +0x00 list link (offset 0)
  ExpectIncomingEntry* next;  // +0x08
  UdpManager* manager;           // +0x10
  UdpIpAddress ip;               // +0x18
  int port;                      // +0x1C
  int timeout;                   // +0x20 ms
  uint32_t connectCode;          // +0x24 0 = do not send anything
  int64_t startTime;             // +0x28
  int64_t lastSendTime;          // +0x30
};
static_assert(offsetof(ExpectIncomingEntry, timeout) == 0x20);
static_assert(sizeof(ExpectIncomingEntry) == 0x38);

// UdpConnection fields the manager reads directly.
struct UdpConnectionInternals {
  static constexpr size_t kStatus = 0xD4;   // 2 = cStatusDisconnected
  static constexpr size_t kGuard = 0x2E0;   // UdpPlatformGuardObject
  static constexpr size_t kTerminateCode = 0xD0;
  static constexpr size_t kEncryptCode = 0x1F0;
};

// SoeUtil::RefArray<unsigned char>: a shared byte buffer whose lifetime is
// tracked by an atomic {strong, weak} control block.
struct SharedByteArray {
  void** arrayVtable;   // +0x00
  uint8_t* data;        // +0x08
  int length;           // +0x10
  void** refVtable;     // +0x18 SoeUtil::RefCounted subobject; slot 1 destroys
  int* control;         // +0x20 {strong, weak}, operator new(0x10)
};

// UdpManager::CallbackEvent (0x40 bytes): a handler callback deferred until
// DeliverEvents when event queuing is on.
enum CallbackEventType {
  kEventRoutePacket = 1,
  kEventConnectComplete = 2,
  kEventTerminated = 3,
  kEventCrcReject = 4,
  kEventPacketCorrupt = 5,
  kEventConnectRequest = 6,
};
struct CallbackEvent {
  int type;                  // +0x00 CallbackEventType
  UdpConnection* connection; // +0x08 referenced
  UdpRefCount* packet;       // +0x10 LogicalPacket, referenced
  int reason;                // +0x18
  int64_t time;              // +0x20
  SharedByteArray* payload;  // +0x28
  CallbackEvent* prev;       // +0x30 list link (offset 0x30)
  CallbackEvent* next;       // +0x38
};
static_assert(offsetof(CallbackEvent, payload) == 0x28);
static_assert(sizeof(CallbackEvent) == 0x40);

// Binary min-heap of connections ordered by when they next need time. Each
// connection stores its heap index at +0x18 (-1 when not queued).
struct ConnectionPriorityQueue {
  struct Entry {
    UdpConnection* connection;
    int64_t time;
  };
  Entry* entries;  // +0x00
  int capacity;    // +0x08
  int count;       // +0x0C

  static int& HeapIndex(UdpConnection* c) {
    return *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(c) + 0x18);
  }
};

// UdpLinkedList<UdpConnection> is 0x28 bytes, not 0x20 like the other
// instantiations: its count lives at +0x24 (+0x1C..+0x23 unknown).
struct UdpConnectionList {
  void** vtable;           // +0x00
  UdpConnection* first;    // +0x08
  UdpConnection* last;     // +0x10
  int linkOffset;          // +0x18
  int unknown1C;           // +0x1C
  int unknown20;           // +0x20
  int count;               // +0x24

  UdpConnection*& Prev(UdpConnection* c) {
    return *reinterpret_cast<UdpConnection**>(reinterpret_cast<uint8_t*>(c) + linkOffset);
  }
  UdpConnection*& Next(UdpConnection* c) {
    return *reinterpret_cast<UdpConnection**>(reinterpret_cast<uint8_t*>(c) + linkOffset + 8);
  }
  void AddHead(UdpConnection* c) {
    Next(c) = first;
    if (first)
      Prev(first) = c;
    else
      last = c;
    first = c;
    ++count;
  }
  void AddTail(UdpConnection* c) {
    Prev(c) = last;
    if (last)
      Next(last) = c;
    else
      first = c;
    last = c;
    ++count;
  }
  void Remove(UdpConnection* c) {
    UdpConnection* next = Next(c);
    if (Prev(c))
      Next(Prev(c)) = next;
    else
      first = next;
    if (next)
      Prev(next) = Prev(c);
    else
      last = Prev(c);
    Next(c) = nullptr;
    Prev(c) = nullptr;
    --count;
  }
};
static_assert(sizeof(UdpConnectionList) == 0x28);

struct UdpManager : UdpGuardedRefCount {
  static constexpr int kBandwidthBuckets = 40;  // 25 ms each -> 1 s window

  UdpParams params;                         // +0x018
  float simulateOutgoingLossPercent;        // +0x1D0
  float simulateIncomingLossPercent;        // +0x1D4
  int simulateIncomingByteRate;             // +0x1D8
  int simulateOutgoingByteRate;             // +0x1DC
  int simulateOutgoingQueueMax;             // +0x1E0 bytes, all connections
  int simulateConnectionQueueMax;           // +0x1E4 bytes, per connection
  int simulateOutgoingLatency;              // +0x1E8 ms
  int simulateIncomingLatency;              // +0x1EC ms
  UdpPacketBuffer** receiveBuffers;         // +0x1F0 ring of params[0x1C] entries
  int receiveBufferIndex;                   // +0x1F8
  int randomSeed;                           // +0x1FC
  int queuedEventBytes;                     // +0x200 payload bytes waiting in eventQueue
  uint8_t unknown204[0x218 - 0x204];        // +0x204
  UdpConnectionList connectionList;         // +0x218 every connection (link at +0xB0)
  UdpConnectionList disconnectingList;      // +0x240 waiting to reach cStatusDisconnected (link at +0xC0)
  UdpLinkedList<ExpectIncomingEntry> expectIncoming;  // +0x268
  ConnectionAddressTable* addressTable;     // +0x288 keyed by ip ^ port
  ConnectionCodeTable* codeTable;           // +0x290 keyed by connect code
  ConnectionPriorityQueue* priorityQueue;   // +0x298 null = give time to every connection
  int64_t priorityProcessTime;              // +0x2A0
  UdpPlatformDriver* driver;                // +0x2A8
  bool inGiveTime;                          // +0x2B0 reentrancy flag
  uint8_t unknown2B1[7];                    // +0x2B1
  int64_t lastReceiveTime;                  // +0x2B8
  int64_t lastSendTime;                     // +0x2C0
  int64_t lastPollTime;                     // +0x2C8
  int64_t lastEventTime;                    // +0x2D0 time of the event being delivered
  int lastPollDelta;                        // +0x2D8 ms since the previous poll, clamped
  int unknown2DC;                           // +0x2DC
  int64_t cachedClock;                      // +0x2E0 guarded by guards[0]
  UdpPlatformGuardObject guards[12];        // +0x2E8
  int64_t bytesSent;                        // +0x348 } guarded by guards[5]
  int64_t packetsSent;                      // +0x350 }
  int64_t bytesReceived;                    // +0x358 }
  int64_t packetsReceived;                  // +0x360 }
  int64_t unknown368;                       // +0x368
  int64_t crcRejects;                       // +0x370
  uint8_t unknown378[0x3A8 - 0x378];        // +0x378
  int64_t priorityConnectionsProcessed;     // +0x3A8
  int64_t priorityConnectionsTotal;         // +0x3B0 sum of connection counts per pass
  uint8_t unknown3B8[0x3C8 - 0x3B8];        // +0x3B8
  int64_t giveTimeCalls;                    // +0x3C8
  int64_t corruptPackets;                   // +0x3D0
  int64_t sendFailures;                     // +0x3D8
  int64_t pollTimeouts;                     // +0x3E0 polls cut short by maxPollingTime
  int64_t eventDeliveryTimeouts;            // +0x3E8 DeliverEvents runs that used up their time
  uint8_t unknown3F0[0x400 - 0x3F0];        // +0x3F0
  int maxQueuedEventBytes;                  // +0x400 high-water marks
  int maxQueuedEvents;                      // +0x404
  uint8_t unknown408[0x420 - 0x408];        // +0x408
  int64_t sendBucketTime;                   // +0x420 clock / 25
  int64_t receiveBucketTime;                // +0x428
  int sendBucketTotal;                      // +0x430
  int receiveBucketTotal;                   // +0x434
  int sendBuckets[kBandwidthBuckets];       // +0x438
  int receiveBuckets[kBandwidthBuckets];    // +0x4D8
  int simulateQueueBytes;                   // +0x578
  int unknown57C;                           // +0x57C
  int64_t simulateIncomingNextTime;         // +0x580
  int64_t simulateOutgoingNextTime;         // +0x588
  UdpLinkedList<SimulateQueueEntry> simulateIncomingQueue;  // +0x590
  UdpLinkedList<SimulateQueueEntry> simulateQueue;          // +0x5B0 outgoing
  UdpLinkedList<PooledLogicalPacket> pooledCreated;      // +0x5D0 every live pooled packet (link +0x48)
  UdpLinkedList<PooledLogicalPacket> pooledAvailable;    // +0x5F0 idle pooled packets (link +0x38)
  uint8_t unknown610[0x620 - 0x610];        // +0x610
  UdpLinkedList<CallbackEvent> eventPool;   // +0x620 recycled events (link +0x30)
  UdpLinkedList<CallbackEvent> eventQueue;  // +0x640 events waiting for DeliverEvents

  UdpPlatformGuardObject& ClockGuard() { return guards[0]; }
  UdpPlatformGuardObject& PoolGuard() { return guards[2]; }
  UdpPlatformGuardObject& StatsGuard() { return guards[5]; }
  UdpPlatformGuardObject& SimulateGuard() { return guards[8]; }
  UdpPlatformGuardObject& EventQueueGuard() { return guards[3]; }
  UdpPlatformGuardObject& EventPoolGuard() { return guards[4]; }
  UdpPlatformGuardObject& DisconnectingGuard() { return guards[6]; }
  UdpPlatformGuardObject& DeliverEventsGuard() { return guards[10]; }
  UdpPlatformGuardObject& ExpectIncomingGuard() { return guards[7]; }
  UdpPlatformGuardObject& GiveTimeGuard() { return guards[9]; }
  UdpPlatformGuardObject& ConnectionGuard() { return guards[11]; }
  // Params fields identified from their uses.
  UdpRefCount* Handler() { return params.At<UdpRefCount*>(0x00); }  // UdpManagerHandler*
  int MaxConnections() { return params.At<int>(0x08); }
  bool ReplyUnreachableConnection() { return params.At<uint8_t>(0x28) != 0; }
  bool AllowAddressRemapping() { return params.At<uint8_t>(0x29) != 0; }
  bool AllowRemapFromAnyAddress() { return params.At<uint8_t>(0x2A) != 0; }
  int IcmpErrorRetryPeriod() { return params.At<int>(0x2C); }
  bool ProcessIcmpErrors() { return params.At<uint8_t>(0x68) != 0; }
  bool ProcessIcmpErrorsDuringNegotiating() { return params.At<uint8_t>(0x69) != 0; }
  bool EventQueuing() { return params.At<uint8_t>(0x178) != 0; }
  bool OnlyAcceptExpectedConnections() { return params.At<uint8_t>(0x180) != 0; }
  int EventPoolMax() { return params.At<int>(0x64); }

  int ReceiveBufferCount() { return params.At<int>(0x1C); }
  int MaxRawPacketSize() { return params.At<int>(0x44); }
  int PooledPacketMax() { return params.At<int>(UdpParams::kPooledPacketMax); }

  int64_t CachedClock() {
    ClockGuard().Enter();
    int64_t now = cachedClock;
    ClockGuard().Leave();
    return now;
  }
};
static_assert(offsetof(UdpManager, params) == 0x18);
static_assert(offsetof(UdpManager, simulateOutgoingLossPercent) == 0x1D0);
static_assert(offsetof(UdpManager, receiveBuffers) == 0x1F0);
static_assert(offsetof(UdpManager, randomSeed) == 0x1FC);
static_assert(offsetof(UdpManager, connectionList) == 0x218);
static_assert(offsetof(UdpManager, disconnectingList) == 0x240);
static_assert(offsetof(UdpManager, expectIncoming) == 0x268);
static_assert(offsetof(UdpManager, addressTable) == 0x288);
static_assert(offsetof(UdpManager, priorityQueue) == 0x298);
static_assert(offsetof(UdpManager, driver) == 0x2A8);
static_assert(offsetof(UdpManager, inGiveTime) == 0x2B0);
static_assert(offsetof(UdpManager, lastPollTime) == 0x2C8);
static_assert(offsetof(UdpManager, lastPollDelta) == 0x2D8);
static_assert(offsetof(UdpManager, priorityConnectionsProcessed) == 0x3A8);
static_assert(offsetof(UdpManager, giveTimeCalls) == 0x3C8);
static_assert(offsetof(UdpManager, pollTimeouts) == 0x3E0);
static_assert(offsetof(UdpManager, eventPool) == 0x620);
static_assert(offsetof(UdpManager, eventQueue) == 0x640);
static_assert(offsetof(UdpManager, maxQueuedEventBytes) == 0x400);
static_assert(offsetof(UdpManager, eventDeliveryTimeouts) == 0x3E8);
static_assert(offsetof(UdpManager, lastReceiveTime) == 0x2B8);
static_assert(offsetof(UdpManager, cachedClock) == 0x2E0);
static_assert(offsetof(UdpManager, guards) == 0x2E8);
static_assert(offsetof(UdpManager, bytesSent) == 0x348);
static_assert(offsetof(UdpManager, sendFailures) == 0x3D8);
static_assert(offsetof(UdpManager, sendBucketTime) == 0x420);
static_assert(offsetof(UdpManager, sendBuckets) == 0x438);
static_assert(offsetof(UdpManager, receiveBuckets) == 0x4D8);
static_assert(offsetof(UdpManager, simulateQueueBytes) == 0x578);
static_assert(offsetof(UdpManager, simulateQueue) == 0x5B0);
static_assert(offsetof(UdpManager, pooledCreated) == 0x5D0);
static_assert(offsetof(UdpManager, pooledAvailable) == 0x5F0);
static_assert(sizeof(UdpManager) == 0x660);

// Advances a 40 x 25 ms bandwidth window to `now`, clearing buckets that slid
// out of it (inlined in UdpManager and UdpConnection alike).
inline void AdvanceBandwidthBuckets(int64_t now, int64_t& bucketTime, int& total, int* buckets) {
  constexpr int kBuckets = 40;
  int64_t bucket = now / 25;
  int64_t elapsed = bucket - bucketTime;
  if (elapsed <= kBuckets) {
    for (int64_t i = 0; i < elapsed; ++i) {
      int index = static_cast<int>((i + bucket) % kBuckets);
      total -= buckets[index];
      buckets[index] = 0;
    }
  } else {
    for (int i = 0; i < kBuckets; ++i) buckets[i] = 0;
    total = 0;
  }
  bucketTime = bucket;
}

void PoolCreated(UdpManager* self, PooledLogicalPacket* packet);    // 0x1403425c0
void PoolDestroyed(UdpManager* self, PooledLogicalPacket* packet);  // 0x140342650
void PoolReturn(UdpManager* self, PooledLogicalPacket* packet);     // 0x1403426f0

int64_t ManagerClock(UdpManager* self);                          // 0x14033f030
int ManagerClockElapsed(UdpManager* self, int64_t since);         // 0x14033f0a0
bool GiveTime(UdpManager* self, int maxPollingTime, bool giveConnectionsTime);  // 0x1403413c0
UdpPacketBuffer* NextIncomingPacket(UdpManager* self);     // 0x1403449d0
void ProcessRawPacket(UdpManager* self, UdpPacketBuffer* buffer);  // 0x140342ad0
CallbackEvent* AllocEvent(UdpManager* self);                // 0x14033e840
void ClearEvent(CallbackEvent* event);                      // 0x14033ef90
void ReleaseEvent(UdpManager* self, CallbackEvent* event);  // 0x14033e8d0
void QueueEvent(UdpManager* self, CallbackEvent* event);    // 0x14033ff20
void DeliverEvents(UdpManager* self, int maxPollingTime);   // 0x14033f730
void CallbackConnectRequest(UdpManager* self, UdpConnection* connection);  // 0x14033ea30
int ConnectionGetStatus(UdpConnection* connection);        // 0x1403412c0
UdpConnection* GetConnectionByCode(UdpManager* self, uint32_t connectCode);  // 0x14033f190
void Reprioritize(ConnectionPriorityQueue* queue, UdpConnection* connection);  // 0x140342e50
void ReleaseDisconnectedConnections(UdpManager* self);     // 0x1403427f0
void ProcessExpectIncoming(UdpManager* self);           // 0x140342900
bool ExpectIncomingGiveTime(ExpectIncomingEntry* self);  // 0x140341310
SimulateQueueEntry* ConstructSimulateQueueEntry(SimulateQueueEntry* self, const uint8_t* data,
                                                int length, const UdpIpAddress* ip, int port,
                                                int64_t queueTime);  // 0x14033bd40

UdpConnection* GetConnection(UdpManager* self, const UdpIpAddress* ip, int port);  // 0x14033e1f0
bool TakeExpectIncoming(UdpManager* self, const UdpIpAddress* ip, int port);        // 0x1403438d0
void AddNewConnection(UdpManager* self, UdpConnection* connection);              // 0x14033e090

UdpPacketBuffer* ActualReceive(UdpManager* self);  // 0x14033d970
void ActualSend(UdpManager* self, const uint8_t* data, int length, const UdpIpAddress* ip,
                int port);  // 0x14033dba0
void ActualSendHelper(UdpManager* self, const uint8_t* data, int length, const UdpIpAddress* ip,
                      int port);  // 0x14033de20

}  // namespace rebuild::udp
