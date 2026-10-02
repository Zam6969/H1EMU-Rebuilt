#pragma once

#include <cstddef>
#include <cstdint>

#include "udp/UdpRefCount.h"

// UdpLibrary's application-payload classes. Every reliable or unreliable
// message the client sends is wrapped in one of these before UdpConnection
// fragments and sequences it.
namespace rebuild::udp {

struct UdpManager;

// UdpLibrary::LogicalPacket (vtable 0x1420528a0, size 0x20), abstract.
struct LogicalPacket : UdpRefCount {
  LogicalPacket* listPrev;  // +0x10 link for the connection's send queue
  LogicalPacket* listNext;  // +0x18

  enum Slot {
    kDeletingDestructor = 4,
    kGetDataPtr = 5,
    kGetDataPtrConst = 6,
    kGetDataLen = 7,
    kSetDataLen = 8,
    kIsInternalPacket = 9,
  };

  void* VirtualGetDataPtr() {
    return reinterpret_cast<void* (*)(LogicalPacket*)>(vtable[kGetDataPtr])(this);
  }
};
static_assert(offsetof(LogicalPacket, listPrev) == 0x10);
static_assert(sizeof(LogicalPacket) == 0x20);

// UdpLibrary::SimpleLogicalPacket (vtable 0x1420528f8, size 0x30): heap buffer.
struct SimpleLogicalPacket : LogicalPacket {
  uint8_t* data;  // +0x20 operator new[]
  int dataLen;    // +0x28
};
static_assert(offsetof(SimpleLogicalPacket, data) == 0x20);
static_assert(sizeof(SimpleLogicalPacket) == 0x30);

// UdpLibrary::GroupLogicalPacket (vtable 0x142052950, size 0x30): several
// small packets combined into one SOE multi-packet (opcode 00 19).
struct GroupLogicalPacket : LogicalPacket {
  uint8_t* data;  // +0x20 SizedRealloc buffer (int capacity stored at data[-4])
  int dataLen;    // +0x28
};
static_assert(sizeof(GroupLogicalPacket) == 0x30);

// UdpLibrary::PooledLogicalPacket (vtable 0x1420529a8, size 0x58): recycled
// through its UdpManager instead of being deleted.
struct PooledLogicalPacket : LogicalPacket {
  uint8_t* data;                     // +0x20 operator new[]
  int dataLen;                       // +0x28
  int capacity;                      // +0x2C
  UdpManager* manager;               // +0x30 owner, null once detached
  PooledLogicalPacket* availablePrev;  // +0x38 manager's available-pool list (offset 0x38)
  PooledLogicalPacket* availableNext;  // +0x40
  PooledLogicalPacket* createdPrev;    // +0x48 manager's all-pooled list (offset 0x48)
  PooledLogicalPacket* createdNext;    // +0x50
};
static_assert(offsetof(PooledLogicalPacket, manager) == 0x30);
static_assert(offsetof(PooledLogicalPacket, availablePrev) == 0x38);
static_assert(offsetof(PooledLogicalPacket, createdPrev) == 0x48);
static_assert(sizeof(PooledLogicalPacket) == 0x58);

// UdpLibrary::FixedLogicalPacket<N>: payload stored inline.
template <int N>
struct FixedLogicalPacket : LogicalPacket {
  uint8_t data[N];  // +0x20
  int dataLen;      // +0x20 + N
};
static_assert(sizeof(FixedLogicalPacket<128>) == 0xA8);
static_assert(sizeof(FixedLogicalPacket<256>) == 0x128);
static_assert(sizeof(FixedLogicalPacket<512>) == 0x228);
static_assert(sizeof(FixedLogicalPacket<1024>) == 0x428);

inline void** const kLogicalPacketVtable = reinterpret_cast<void**>(0x1420528a0);
inline void** const kSimpleLogicalPacketVtable = reinterpret_cast<void**>(0x1420528f8);
inline void** const kGroupLogicalPacketVtable = reinterpret_cast<void**>(0x142052950);
inline void** const kPooledLogicalPacketVtable = reinterpret_cast<void**>(0x1420529a8);

template <int N>
void** FixedLogicalPacketVtable();

// UdpMisc helpers
int PutVariableValue(uint8_t* buffer, uint32_t value);        // 0x14034b9c0
int GetVariableValue(const uint8_t* buffer, uint32_t* value); // 0x14034b950
int Random(int* seed);                                         // 0x14034ba20
LogicalPacket* CreateQuickLogicalPacket(const void* data, int dataLen, const void* data2,
                                        int dataLen2);  // 0x14034b560

}  // namespace rebuild::udp
