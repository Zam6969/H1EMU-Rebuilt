// UdpManager's constructor.
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "soeutil/Mutex.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpManager.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

void** const kUdpManagerVtable = reinterpret_cast<void**>(0x142051778);
void** const kConnectionListVtable = reinterpret_cast<void**>(0x1420516a8);
void** const kExpectIncomingListVtable = reinterpret_cast<void**>(0x1420516b8);
void** const kSimulateListVtable = reinterpret_cast<void**>(0x1420516c8);
void** const kPooledPacketListVtable = reinterpret_cast<void**>(0x1420516d8);
void** const kCallbackEventListVtable = reinterpret_cast<void**>(0x142051768);
// SoeUtil memory pool pieces
void* const kHashListMapBlockVtable = reinterpret_cast<void*>(0x14204d338);  // HashListMap<int,uchar*,113,4>
void* const kHashListBlockVtable = reinterpret_cast<void*>(0x14204d370);     // HashList<uchar*,113,4>
void* const kPoolMapVtable = reinterpret_cast<void*>(0x142051818);           // Map<int,DynamicMemoryPool<0,8>::Block,1,0>

constexpr int kErrorConditionCouldNotBindSocket = 2;

template <class T>
void InitList(UdpLinkedList<T>& list, void** vtable, int linkOffset) {
  list.vtable = vtable;
  list.first = nullptr;
  list.last = nullptr;
  list.linkOffset = linkOffset;
  list.count = 0;
}
void InitConnectionList(UdpConnectionList& list, int linkOffset) {
  list.vtable = kConnectionListVtable;
  list.first = nullptr;
  list.last = nullptr;
  list.linkOffset = linkOffset;
  list.unknown1C = 0;
  list.unknown20 = 0;
  list.count = 0;
}

void* AllocateGrowArray(int64_t count, size_t elementSize) {
  uint64_t bytes = static_cast<uint64_t>(count) * elementSize;
  if (count < 0 || (count != 0 && bytes / elementSize != static_cast<uint64_t>(count))) bytes = ~0ull;
  return soeutil::AllocateArray(bytes);
}

// SoeUtil::DynamicMemoryPool<0,8> owned at +0x610 (0x560 bytes): a
// HashListMap<int,uchar*,113,4> of free lists plus a block map and mutex.
uint8_t* NewMemoryPool() {
  auto* p = static_cast<uint8_t*>(soeutil::Allocate(0x560));
  if (!p) return nullptr;
  auto at = [p](size_t offset) -> uint8_t* { return p + offset; };
  *reinterpret_cast<int*>(at(0x20)) = 0;
  *reinterpret_cast<uint64_t*>(at(0x10)) = 0;
  *reinterpret_cast<uint64_t*>(at(0x18)) = 0;
  std::memset(at(0x28), 0, 0x388);
  *reinterpret_cast<int*>(at(0x08)) = 0;
  *reinterpret_cast<int*>(at(0x0C)) = 0x7FFFFFFF;
  *reinterpret_cast<void**>(at(0x00)) = kHashListMapBlockVtable;
  *reinterpret_cast<uint64_t*>(at(0x3B0)) = 0;
  *reinterpret_cast<int*>(at(0x3B8)) = 0;
  *at(0x3CC) = 0;
  // Three free-list nodes carved from inline storage, chained head-first.
  auto* inlineNodes = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(at(0x3D4)) & ~uintptr_t{7});
  inlineNodes[0] = 0;
  auto*& freeHead = *reinterpret_cast<uint64_t**>(at(0x3C0));
  inlineNodes[5] = reinterpret_cast<uint64_t>(inlineNodes);
  freeHead = inlineNodes + 5;
  inlineNodes[10] = reinterpret_cast<uint64_t>(freeHead);
  freeHead = inlineNodes + 10;
  inlineNodes[15] = reinterpret_cast<uint64_t>(freeHead);
  freeHead = inlineNodes + 15;
  *reinterpret_cast<int*>(at(0x3C8)) = 4;
  *reinterpret_cast<void**>(at(0x00)) = kHashListBlockVtable;
  *reinterpret_cast<uint64_t*>(at(0x480)) = 0;
  *reinterpret_cast<int*>(at(0x488)) = 0;
  *reinterpret_cast<void**>(at(0x478)) = kPoolMapVtable;
  *reinterpret_cast<uint64_t*>(at(0x490)) = 0;
  *reinterpret_cast<int*>(at(0x498)) = 0;
  *at(0x4AC) = 0;
  auto* inlineBlock = reinterpret_cast<uint64_t*>(reinterpret_cast<uintptr_t>(at(0x4B4)) & ~uintptr_t{7});
  inlineBlock[0] = 0;
  *reinterpret_cast<uint64_t**>(at(0x4A0)) = inlineBlock;
  *reinterpret_cast<int*>(at(0x4A8)) = 1;
  *reinterpret_cast<uint64_t*>(at(0x4F8)) = 0x80000;
  *reinterpret_cast<uint64_t*>(at(0x500)) = 0;
  *reinterpret_cast<uint64_t*>(at(0x508)) = 0;
  auto* mutex = reinterpret_cast<CRITICAL_SECTION*>(at(0x518));
  soeutil::MutexConstruct(mutex, 4000, nullptr);
  soeutil::MutexLock(mutex);
  *reinterpret_cast<int*>(at(0x4F8)) = 0x100000;  // block size
  if (mutex) soeutil::MutexUnlock(mutex);
  return p;
}

// The small 0x68-byte pool at +0x618.
uint8_t* NewSmallPool() {
  auto* o = static_cast<uint8_t*>(soeutil::Allocate(0x68));
  if (!o) return nullptr;
  *reinterpret_cast<uint64_t*>(o) = 0;
  *reinterpret_cast<int*>(o + 8) = 0;
  o[0x1C] = 0;
  *reinterpret_cast<uint64_t*>(o + 0x10) = 0;
  *reinterpret_cast<int*>(o + 0x18) = 0;
  soeutil::MutexConstruct(reinterpret_cast<CRITICAL_SECTION*>(o + 0x20), 4000, nullptr);
  return o;
}

bool OpenOn(UdpManager* self, int port) {
  using CloseFn = void (*)(UdpPlatformDriver*);
  reinterpret_cast<CloseFn>(self->driver->vtable[kSlotSocketClose])(self->driver);
  game::Field<int>(self, 0x2DC) = 0;
  using OpenFn = bool (*)(UdpPlatformDriver*, int, int, int, const char*);
  bool opened = reinterpret_cast<OpenFn>(self->driver->vtable[kSlotSocketOpen])(
      self->driver, port, self->params.At<int>(0x18), self->params.At<int>(0x14),
      reinterpret_cast<const char*>(&self->params.At<uint8_t>(0x74)));
  if (!opened) game::Field<int>(self, 0x2DC) = kErrorConditionCouldNotBindSocket;
  return opened;
}

}  // namespace

// 0x1403440e0
void ManagerClearStatistics(UdpManager* self) {
  self->StatsGuard().Enter();
  game::Field<int64_t>(self, 0x418) = self->CachedClock();  // statistics start time
  std::memset(&self->bytesSent, 0, 0xD0);
  self->StatsGuard().Leave();
}

// 0x14033bdd0
UdpManager* ManagerConstruct(UdpManager* self, const UdpParams* params) {
  self->refCount = 1;
  self->flag = false;
  self->vtable = kUdpGuardedRefCountVtable;
  self->guard.Construct();
  self->vtable = kUdpManagerVtable;
  ConstructParams(&self->params, 0);
  std::memset(&self->simulateOutgoingLossPercent, 0, 0x20);  // +0x1D0..+0x1EF
  InitConnectionList(self->connectionList, 0xB0);
  InitConnectionList(self->disconnectingList, 0xC0);
  InitList(self->expectIncoming, kExpectIncomingListVtable, 0);
  for (auto& guard : self->guards) guard.Construct();
  InitList(self->simulateIncomingQueue, kSimulateListVtable, 0);
  InitList(self->simulateQueue, kSimulateListVtable, 0);
  InitList(self->pooledCreated, kPooledPacketListVtable, 0x48);
  InitList(self->pooledAvailable, kPooledPacketListVtable, 0x38);
  InitList(self->eventPool, kCallbackEventListVtable, 0x30);
  InitList(self->eventQueue, kCallbackEventListVtable, 0x30);

  std::memcpy(&self->params, params, sizeof(UdpParams));
  int& maxRaw = self->params.At<int>(0x44);
  if (maxRaw > 0x2000) maxRaw = 0x2000;
  if (self->params.At<int>(0x40) == -1) self->params.At<int>(0x40) = maxRaw;
  if (self->params.At<int>(0x60) == -1) self->params.At<int>(0x60) = maxRaw;
  if (maxRaw < self->params.At<int>(0x40)) self->params.At<int>(0x40) = maxRaw;
  int& bufferCount = self->params.At<int>(0x1C);
  if (bufferCount < 1) bufferCount = 1;
  self->receiveBufferIndex = 0;
  game::Field<int64_t>(self, 0x210) = 0;
  game::Field<int64_t>(self, 0x208) = 0;

  auto* driver = self->params.At<UdpPlatformDriver*>(0x170);  // application-supplied driver
  if (!driver) {
    void* memory = soeutil::Allocate(sizeof(UdpPlatformDriver));
    driver = memory ? Construct(static_cast<UdpPlatformDriver*>(memory)) : nullptr;
  }
  self->driver = driver;

  self->receiveBuffers = static_cast<UdpPacketBuffer**>(AllocateGrowArray(bufferCount, sizeof(void*)));
  for (int i = 0; i < self->ReceiveBufferCount(); ++i) {
    auto* buffer = static_cast<UdpPacketBuffer*>(soeutil::Allocate(sizeof(UdpPacketBuffer)));
    if (buffer) {
      int size = self->MaxRawPacketSize();
      buffer->ip = 0;
      buffer->data = static_cast<uint8_t*>(soeutil::AllocateArray(static_cast<size_t>(size)));
      buffer->port = 0;
      buffer->length = 0;
    }
    self->receiveBuffers[i] = buffer;
  }

  self->ClockGuard().Enter();
  self->cachedClock = static_cast<int64_t>(
      reinterpret_cast<uint64_t (*)(UdpPlatformDriver*)>(self->driver->vtable[kSlotClock])(self->driver));
  self->ClockGuard().Leave();
  ManagerClearStatistics(self);
  self->randomSeed = static_cast<int>(self->CachedClock());

  self->lastReceiveTime = 0;
  self->lastSendTime = 0;
  self->lastEventTime = 0;
  self->lastPollTime = 0;
  self->lastPollDelta = 0;
  self->priorityProcessTime = 0;
  self->inGiveTime = false;
  self->queuedEventBytes = 0;

  for (int i = 0; i < self->params.At<int>(UdpParams::kPooledPacketInitial) && i < self->PooledPacketMax(); ++i) {
    void* memory = soeutil::Allocate(sizeof(PooledLogicalPacket));
    auto* packet = memory ? game::Call<PooledLogicalPacket* (*)(void*, UdpManager*, int)>(0x14034bcd0)(
                                memory, self, self->params.At<int>(0x60))
                          : nullptr;
    PoolReturn(self, packet);
  }

  game::Field<uint8_t*>(self, 0x610) = NewMemoryPool();
  game::Field<uint8_t*>(self, 0x618) = NewSmallPool();
  self->simulateOutgoingNextTime = 0;
  self->simulateIncomingNextTime = 0;
  self->simulateQueueBytes = 0;
  std::memset(self->sendBuckets, 0, 0x140);
  self->sendBucketTotal = 0;
  self->receiveBucketTotal = 0;
  self->sendBucketTime = 0;
  self->receiveBucketTime = 0;

  if (!self->params.At<uint8_t>(0x4C)) {  // priority-queue scheduling unless disabled
    auto* queue = static_cast<ConnectionPriorityQueue*>(soeutil::Allocate(sizeof(ConnectionPriorityQueue)));
    if (queue) {
      int capacity = self->MaxConnections();
      queue->count = 0;
      queue->capacity = capacity;
      queue->entries = static_cast<ConnectionPriorityQueue::Entry*>(
          AllocateGrowArray(capacity, sizeof(ConnectionPriorityQueue::Entry)));
      queue->entries[0] = {nullptr, 0};
    }
    self->priorityQueue = queue;
  } else {
    self->priorityQueue = nullptr;
  }

  int tableSize = self->params.At<int>(0x48);
  auto* addressTable = static_cast<ConnectionAddressTable*>(soeutil::Allocate(sizeof(ConnectionAddressTable)));
  if (addressTable) {
    addressTable->buckets = nullptr;
    addressTable->bucketCount = 0;
    addressTable->count = 0;
    addressTable->usedBuckets = 0;
    addressTable->Resize(tableSize);
  }
  self->addressTable = addressTable;
  auto* codeTable = static_cast<ConnectionCodeTable*>(soeutil::Allocate(sizeof(ConnectionCodeTable)));
  if (codeTable) {
    int codeSize = self->params.At<int>(0x48) / 5;
    if (codeSize < 10) codeSize = 10;
    codeTable->buckets = nullptr;
    codeTable->bucketCount = 0;
    codeTable->count = 0;
    codeTable->usedBuckets = 0;
    codeTable->Resize(codeSize);
  }
  self->codeTable = codeTable;

  // Bind: a fixed port, or a random start within [port, port + range).
  int basePort = self->params.At<int>(0x0C);
  int range = self->params.At<int>(0x10);
  if (range == 0) {
    OpenOn(self, basePort);
  } else {
    int start = Random(&self->randomSeed);
    for (int i = 0; i < self->params.At<int>(0x10); ++i) {
      int port = (start % range + i) % self->params.At<int>(0x10) + self->params.At<int>(0x0C);
      if (OpenOn(self, port) && game::Field<int>(self, 0x2DC) != kErrorConditionCouldNotBindSocket) break;
    }
  }
  return self;
}

REBUILD_FUNCTION(UdpManager_ClearStatistics, 0x1403440e0, ManagerClearStatistics);
REBUILD_FUNCTION(UdpManager_Construct, 0x14033bdd0, ManagerConstruct);

}  // namespace rebuild::udp
