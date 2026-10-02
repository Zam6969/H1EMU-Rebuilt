#include "udp/LogicalPacket.h"

#include <cstring>

#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpManager.h"

namespace rebuild::udp {

template <>
void** FixedLogicalPacketVtable<128>() { return reinterpret_cast<void**>(0x142052628); }
template <>
void** FixedLogicalPacketVtable<256>() { return reinterpret_cast<void**>(0x142052680); }
template <>
void** FixedLogicalPacketVtable<512>() { return reinterpret_cast<void**>(0x1420526d8); }
template <>
void** FixedLogicalPacketVtable<1024>() { return reinterpret_cast<void**>(0x142052730); }

namespace {

// ---- LogicalPacket ----

// 0x14034bca0
LogicalPacket* LogicalPacketConstruct(LogicalPacket* self) {
  self->refCount = 1;
  self->vtable = kLogicalPacketVtable;
  self->flag = false;
  self->listPrev = nullptr;
  self->listNext = nullptr;
  return self;
}

// 0x14034be10
void LogicalPacketDestruct(LogicalPacket* self) { self->vtable = kUdpRefCountVtable; }

// 0x14034bf00, slot 4
LogicalPacket* LogicalPacketDeletingDestructor(LogicalPacket* self, unsigned flags) {
  self->vtable = kUdpRefCountVtable;
  if (flags & 1) soeutil::Free(self, sizeof(LogicalPacket));
  return self;
}

// 0x14034c200, slot 9 (shared by every class except GroupLogicalPacket)
bool IsNotInternalPacket(LogicalPacket*) { return false; }

// 0x14034c300: a devirtualized copy of UdpRefCount::Release.
void LogicalPacketRelease(LogicalPacket* self) {
  if (--self->refCount == 0) self->VirtualDelete();
}

// ---- SimpleLogicalPacket ----

// 0x14034bd60
SimpleLogicalPacket* SimpleConstruct(SimpleLogicalPacket* self, const void* data, int dataLen) {
  LogicalPacketConstruct(self);
  self->vtable = kSimpleLogicalPacketVtable;
  self->dataLen = dataLen;
  self->data = static_cast<uint8_t*>(soeutil::AllocateArray(dataLen));
  if (data) std::memcpy(self->data, data, dataLen);
  return self;
}

// 0x14034be70
void SimpleDestruct(SimpleLogicalPacket* self) {
  self->vtable = kSimpleLogicalPacketVtable;
  soeutil::FreeArray(self->data);
  self->vtable = kUdpRefCountVtable;
}

// 0x14034bfb0, slot 4
SimpleLogicalPacket* SimpleDeletingDestructor(SimpleLogicalPacket* self, unsigned flags) {
  SimpleDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(SimpleLogicalPacket));
  return self;
}

uint8_t* SimpleGetDataPtr(SimpleLogicalPacket* self) { return self->data; }   // 0x14034c1e0, slot 5
uint8_t* SimpleGetDataPtrConst(SimpleLogicalPacket* self) { return self->data; }  // 0x14034c1d0, slot 6
int SimpleGetDataLen(SimpleLogicalPacket* self) { return self->dataLen; }        // 0x14034c180, slot 7
void SimpleSetDataLen(SimpleLogicalPacket* self, int len) { self->dataLen = len; }  // 0x14034c2f0, slot 8

// ---- GroupLogicalPacket ----

// 0x14034bb40: resizes a buffer whose int capacity sits just before it,
// rounding up to a multiple of `granularity`. A size of 0 frees it.
uint8_t* SizedRealloc(uint8_t* buffer, int size, int granularity) {
  int rounded = size - 1 + granularity;
  rounded -= rounded % granularity;
  if (rounded == 0) {
    if (buffer) soeutil::CrtFree(buffer - 4);
    return nullptr;
  }
  int* block;
  if (!buffer) {
    block = static_cast<int*>(soeutil::CrtMalloc(rounded + 4));
  } else {
    if (rounded == reinterpret_cast<int*>(buffer)[-1]) return buffer;
    block = static_cast<int*>(soeutil::CrtRealloc(buffer - 4, rounded + 4));
  }
  if (!block) return nullptr;
  *block = rounded;
  return reinterpret_cast<uint8_t*>(block + 1);
}

// 0x14034bdd0
void GroupDestruct(GroupLogicalPacket* self) {
  self->vtable = kGroupLogicalPacketVtable;
  SizedRealloc(self->data, 0, 1);
  self->vtable = kUdpRefCountVtable;
}

// 0x14034bea0, slot 4
GroupLogicalPacket* GroupDeletingDestructor(GroupLogicalPacket* self, unsigned flags) {
  GroupDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(GroupLogicalPacket));
  return self;
}

uint8_t* GroupGetDataPtr(GroupLogicalPacket* self) { return self->data; }       // 0x14034c1a0, slot 5
uint8_t* GroupGetDataPtrConst(GroupLogicalPacket* self) { return self->data; }  // 0x14034c190, slot 6
int GroupGetDataLen(GroupLogicalPacket* self) { return self->dataLen; }         // 0x14034c160, slot 7
void GroupSetDataLen(GroupLogicalPacket*, int) {}                               // 0x14034c2d0, slot 8
bool GroupIsInternalPacket(GroupLogicalPacket*) { return true; }                // 0x14034c1f0, slot 9

// 0x14034c080. Appends one packet: a variable-length size, then the bytes.
// Application packets starting with 0x00 get an extra 0x00 so the receiver
// does not mistake them for UdpLibrary protocol opcodes.
void GroupAddPacket(GroupLogicalPacket* self, const uint8_t* data, int dataLen, bool isInternal) {
  if (dataLen == 0) return;
  self->data = SizedRealloc(self->data, dataLen + 10 + self->dataLen, 0x200);
  if (self->dataLen == 0) {
    self->data[0] = 0x00;
    self->data[1] = 0x19;  // UdpConnection::cUdpPacketGroup
    self->dataLen = 2;
  }
  uint8_t* out = self->data + self->dataLen;
  if (!isInternal && data[0] == 0) {
    out += PutVariableValue(out, dataLen + 1);
    *out++ = 0;
  } else {
    out += PutVariableValue(out, dataLen);
  }
  std::memcpy(out, data, dataLen);
  self->dataLen = static_cast<int>(out - self->data) + dataLen;
}

// ---- PooledLogicalPacket ----

// 0x14034bcd0
PooledLogicalPacket* PooledConstruct(PooledLogicalPacket* self, UdpManager* manager, int capacity) {
  self->refCount = 1;
  self->flag = false;
  self->listPrev = nullptr;
  self->listNext = nullptr;
  self->vtable = kPooledLogicalPacketVtable;
  self->availablePrev = nullptr;
  self->availableNext = nullptr;
  self->createdPrev = nullptr;
  self->createdNext = nullptr;
  self->capacity = capacity;
  self->data = static_cast<uint8_t*>(soeutil::AllocateArray(capacity));
  self->dataLen = 0;
  self->manager = manager;
  PoolCreated(manager, self);
  return self;
}

// 0x14034c150, slot 0
void PooledAddRef(PooledLogicalPacket* self) { ++self->refCount; }

// 0x14034c210, slot 1. The last outside reference hands the packet back to
// its manager's pool instead of deleting it.
void PooledRelease(PooledLogicalPacket* self) {
  int refs = reinterpret_cast<int (*)(UdpRefCount*)>(self->vtable[UdpRefCount::kGetRefCount])(self);
  if (refs == 1 && self->manager) {
    PoolReturn(self->manager, self);
    return;
  }
  if (--self->refCount == 0) self->VirtualDelete();
}

// 0x14034bf30, slot 4
PooledLogicalPacket* PooledDeletingDestructor(PooledLogicalPacket* self, unsigned flags) {
  self->vtable = kPooledLogicalPacketVtable;
  if (self->manager) {
    PoolDestroyed(self->manager, self);
    self->manager = nullptr;
  }
  soeutil::FreeArray(self->data);
  self->vtable = kUdpRefCountVtable;
  if (flags & 1) soeutil::Free(self, sizeof(PooledLogicalPacket));
  return self;
}

uint8_t* PooledGetDataPtr(PooledLogicalPacket* self) { return self->data; }       // 0x14034c1c0, slot 5
uint8_t* PooledGetDataPtrConst(PooledLogicalPacket* self) { return self->data; }  // 0x14034c1b0, slot 6
int PooledGetDataLen(PooledLogicalPacket* self) { return self->dataLen; }         // 0x14034c170, slot 7
void PooledSetDataLen(PooledLogicalPacket* self, int len) { self->dataLen = len; }  // 0x14034c2e0, slot 8

// ---- FixedLogicalPacket<N> ----

template <int N>
FixedLogicalPacket<N>* FixedConstruct(FixedLogicalPacket<N>* self, const void* data, int dataLen) {
  LogicalPacketConstruct(self);
  self->dataLen = dataLen;
  self->vtable = FixedLogicalPacketVtable<N>();
  if (data) std::memcpy(self->data, data, dataLen);
  return self;
}

template <int N>
FixedLogicalPacket<N>* FixedDeletingDestructor(FixedLogicalPacket<N>* self, unsigned flags) {
  LogicalPacketDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(FixedLogicalPacket<N>));
  return self;
}

template <int N>
uint8_t* FixedGetDataPtr(FixedLogicalPacket<N>* self) { return self->data; }
template <int N>
uint8_t* FixedGetDataPtrConst(FixedLogicalPacket<N>* self) { return self->data; }
template <int N>
int FixedGetDataLen(FixedLogicalPacket<N>* self) { return self->dataLen; }
template <int N>
void FixedSetDataLen(FixedLogicalPacket<N>* self, int len) { self->dataLen = len; }

template <int N>
LogicalPacket* NewFixed(int dataLen) {
  auto* packet = static_cast<FixedLogicalPacket<N>*>(soeutil::Allocate(sizeof(FixedLogicalPacket<N>)));
  LogicalPacketConstruct(packet);
  packet->vtable = FixedLogicalPacketVtable<N>();
  packet->dataLen = dataLen;
  return packet;
}

}  // namespace

// 0x14034b9c0. 1 byte below 0xFE, FF + 16-bit big-endian below 0xFFFF,
// otherwise FF FF FF + 32-bit big-endian. Returns bytes written.
int PutVariableValue(uint8_t* buffer, uint32_t value) {
  if (value < 0xFE) {
    buffer[0] = static_cast<uint8_t>(value);
    return 1;
  }
  if (value < 0xFFFF) {
    buffer[0] = 0xFF;
    buffer[1] = static_cast<uint8_t>(value >> 8);
    buffer[2] = static_cast<uint8_t>(value);
    return 3;
  }
  buffer[0] = buffer[1] = buffer[2] = 0xFF;
  buffer[3] = static_cast<uint8_t>(value >> 24);
  buffer[4] = static_cast<uint8_t>(value >> 16);
  buffer[5] = static_cast<uint8_t>(value >> 8);
  buffer[6] = static_cast<uint8_t>(value);
  return 7;
}

// 0x14034b950. Returns bytes consumed.
int GetVariableValue(const uint8_t* buffer, uint32_t* value) {
  if (buffer[0] != 0xFF) {
    *value = buffer[0];
    return 1;
  }
  if (buffer[1] == 0xFF && buffer[2] == 0xFF) {
    *value = static_cast<uint32_t>(buffer[3]) << 24 | static_cast<uint32_t>(buffer[4]) << 16 |
             static_cast<uint32_t>(buffer[5]) << 8 | buffer[6];
    return 7;
  }
  *value = static_cast<uint32_t>(buffer[1]) << 8 | buffer[2];
  return 3;
}

// 0x14034ba20. Park-Miller minimal standard generator (Schrage's method)
// with a +123 offset; updates and returns the seed.
int Random(int* seed) {
  int hi = *seed / 127773;
  int lo = *seed - hi * 127773;
  int next = static_cast<int>(static_cast<uint32_t>(lo) * 16807u - static_cast<uint32_t>(hi) * 2836u + 123u);
  if (next <= 0) next = static_cast<int>(static_cast<uint32_t>(next) + 0x7FFFFFFFu);
  *seed = next;
  return next;
}

// 0x14034b560. Picks the smallest FixedLogicalPacket that fits both pieces
// (a SimpleLogicalPacket above 1024 bytes) and copies them in back to back.
LogicalPacket* CreateQuickLogicalPacket(const void* data, int dataLen, const void* data2,
                                        int dataLen2) {
  int total = dataLen + dataLen2;
  LogicalPacket* packet;
  switch ((total - 1) / 128) {
    case 0: packet = NewFixed<128>(total); break;
    case 1: packet = NewFixed<256>(total); break;
    case 2: case 3: packet = NewFixed<512>(total); break;
    case 4: case 5: case 6: case 7: packet = NewFixed<1024>(total); break;
    default: {
      auto* simple = static_cast<SimpleLogicalPacket*>(soeutil::Allocate(sizeof(SimpleLogicalPacket)));
      packet = SimpleConstruct(simple, nullptr, total);
    }
  }
  auto* out = static_cast<uint8_t*>(packet->VirtualGetDataPtr());
  if (data) std::memcpy(out, data, dataLen);
  if (data2) std::memcpy(out + dataLen, data2, dataLen2);
  return packet;
}

REBUILD_FUNCTION(LogicalPacket_Construct, 0x14034bca0, LogicalPacketConstruct);
REBUILD_FUNCTION(LogicalPacket_Destruct, 0x14034be10, LogicalPacketDestruct);
REBUILD_FUNCTION(LogicalPacket_DeletingDestructor, 0x14034bf00, LogicalPacketDeletingDestructor);
REBUILD_FUNCTION_TOO_SMALL(LogicalPacket_IsInternalPacket, 0x14034c200, IsNotInternalPacket);
REBUILD_FUNCTION(LogicalPacket_Release, 0x14034c300, LogicalPacketRelease);

REBUILD_FUNCTION(SimpleLogicalPacket_Construct, 0x14034bd60, SimpleConstruct);
REBUILD_FUNCTION(SimpleLogicalPacket_Destruct, 0x14034be70, SimpleDestruct);
REBUILD_FUNCTION(SimpleLogicalPacket_DeletingDestructor, 0x14034bfb0, SimpleDeletingDestructor);
REBUILD_FUNCTION(SimpleLogicalPacket_GetDataPtr, 0x14034c1e0, SimpleGetDataPtr);
REBUILD_FUNCTION(SimpleLogicalPacket_GetDataPtrConst, 0x14034c1d0, SimpleGetDataPtrConst);
REBUILD_FUNCTION_TOO_SMALL(SimpleLogicalPacket_GetDataLen, 0x14034c180, SimpleGetDataLen);
REBUILD_FUNCTION_TOO_SMALL(SimpleLogicalPacket_SetDataLen, 0x14034c2f0, SimpleSetDataLen);

REBUILD_FUNCTION(GroupLogicalPacket_SizedRealloc, 0x14034bb40, SizedRealloc);
REBUILD_FUNCTION(GroupLogicalPacket_Destruct, 0x14034bdd0, GroupDestruct);
REBUILD_FUNCTION(GroupLogicalPacket_DeletingDestructor, 0x14034bea0, GroupDeletingDestructor);
REBUILD_FUNCTION(GroupLogicalPacket_GetDataPtr, 0x14034c1a0, GroupGetDataPtr);
REBUILD_FUNCTION(GroupLogicalPacket_GetDataPtrConst, 0x14034c190, GroupGetDataPtrConst);
REBUILD_FUNCTION_TOO_SMALL(GroupLogicalPacket_GetDataLen, 0x14034c160, GroupGetDataLen);
REBUILD_FUNCTION_TOO_SMALL(GroupLogicalPacket_SetDataLen, 0x14034c2d0, GroupSetDataLen);
REBUILD_FUNCTION_TOO_SMALL(GroupLogicalPacket_IsInternalPacket, 0x14034c1f0, GroupIsInternalPacket);
REBUILD_FUNCTION(GroupLogicalPacket_AddPacket, 0x14034c080, GroupAddPacket);

REBUILD_FUNCTION(PooledLogicalPacket_Construct, 0x14034bcd0, PooledConstruct);
REBUILD_FUNCTION_TOO_SMALL(PooledLogicalPacket_AddRef, 0x14034c150, PooledAddRef);
REBUILD_FUNCTION(PooledLogicalPacket_Release, 0x14034c210, PooledRelease);
REBUILD_FUNCTION(PooledLogicalPacket_DeletingDestructor, 0x14034bf30, PooledDeletingDestructor);
REBUILD_FUNCTION(PooledLogicalPacket_GetDataPtr, 0x14034c1c0, PooledGetDataPtr);
REBUILD_FUNCTION(PooledLogicalPacket_GetDataPtrConst, 0x14034c1b0, PooledGetDataPtrConst);
REBUILD_FUNCTION_TOO_SMALL(PooledLogicalPacket_GetDataLen, 0x14034c170, PooledGetDataLen);
REBUILD_FUNCTION_TOO_SMALL(PooledLogicalPacket_SetDataLen, 0x14034c2e0, PooledSetDataLen);

REBUILD_FUNCTION(FixedLogicalPacket128_Construct, 0x14034b310, FixedConstruct<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_Construct, 0x14034b1f0, FixedConstruct<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_Construct, 0x14034b250, FixedConstruct<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_Construct, 0x14034b2b0, FixedConstruct<1024>);
REBUILD_FUNCTION(FixedLogicalPacket128_DeletingDestructor, 0x14034b430, FixedDeletingDestructor<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_DeletingDestructor, 0x14034b370, FixedDeletingDestructor<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_DeletingDestructor, 0x14034b3b0, FixedDeletingDestructor<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_DeletingDestructor, 0x14034b3f0, FixedDeletingDestructor<1024>);
REBUILD_FUNCTION(FixedLogicalPacket128_GetDataPtr, 0x14034b7e0, FixedGetDataPtr<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_GetDataPtr, 0x14034b780, FixedGetDataPtr<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_GetDataPtr, 0x14034b7a0, FixedGetDataPtr<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_GetDataPtr, 0x14034b7c0, FixedGetDataPtr<1024>);
REBUILD_FUNCTION(FixedLogicalPacket128_GetDataPtrConst, 0x14034b7d0, FixedGetDataPtrConst<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_GetDataPtrConst, 0x14034b770, FixedGetDataPtrConst<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_GetDataPtrConst, 0x14034b790, FixedGetDataPtrConst<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_GetDataPtrConst, 0x14034b7b0, FixedGetDataPtrConst<1024>);
REBUILD_FUNCTION(FixedLogicalPacket128_GetDataLen, 0x14034b760, FixedGetDataLen<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_GetDataLen, 0x14034b730, FixedGetDataLen<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_GetDataLen, 0x14034b740, FixedGetDataLen<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_GetDataLen, 0x14034b750, FixedGetDataLen<1024>);
REBUILD_FUNCTION(FixedLogicalPacket128_SetDataLen, 0x14034baa0, FixedSetDataLen<128>);
REBUILD_FUNCTION(FixedLogicalPacket256_SetDataLen, 0x14034ba70, FixedSetDataLen<256>);
REBUILD_FUNCTION(FixedLogicalPacket512_SetDataLen, 0x14034ba80, FixedSetDataLen<512>);
REBUILD_FUNCTION(FixedLogicalPacket1024_SetDataLen, 0x14034ba90, FixedSetDataLen<1024>);

REBUILD_FUNCTION(UdpMisc_PutVariableValue, 0x14034b9c0, PutVariableValue);
REBUILD_FUNCTION(UdpMisc_GetVariableValue, 0x14034b950, GetVariableValue);
REBUILD_FUNCTION(UdpMisc_Random, 0x14034ba20, Random);
REBUILD_FUNCTION(UdpMisc_CreateQuickLogicalPacket, 0x14034b560, CreateQuickLogicalPacket);

}  // namespace rebuild::udp
