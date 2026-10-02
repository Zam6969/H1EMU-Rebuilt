#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// UdpLibrary's reference-counted base classes and the critical-section
// wrapper they use.
namespace rebuild::udp {

// UdpLibrary::UdpPlatformGuardObject: owns a heap CRITICAL_SECTION.
struct UdpPlatformGuardObject {
  CRITICAL_SECTION* section;  // +0x00, operator new(0x28)

  void Construct();  // 0x14034a030
  void Destruct();   // 0x14034a1d0
  void Enter();      // 0x14034a560
  void Leave();      // 0x14034a8b0
};
static_assert(sizeof(UdpPlatformGuardObject) == 8);

// UdpLibrary::UdpRefCount (vtable 0x142051648, size 0x10). Starts at 1;
// Release() deletes through vtable slot 4 when the count reaches 0.
struct UdpRefCount {
  void** vtable;  // +0x00
  int refCount;   // +0x08
  bool flag;      // +0x0C, cleared by the constructor, set by slot 2

  enum Slot { kAddRef = 0, kRelease = 1, kSetFlag = 2, kGetRefCount = 3, kDeletingDestructor = 4 };

  void VirtualAddRef() { reinterpret_cast<void (*)(UdpRefCount*)>(vtable[kAddRef])(this); }
  void VirtualRelease() { reinterpret_cast<void (*)(UdpRefCount*)>(vtable[kRelease])(this); }
  void VirtualDelete() {
    reinterpret_cast<void* (*)(UdpRefCount*, unsigned)>(vtable[kDeletingDestructor])(this, 1);
  }
};
static_assert(offsetof(UdpRefCount, refCount) == 0x08);
static_assert(sizeof(UdpRefCount) == 0x10);

// UdpLibrary::UdpGuardedRefCount (vtable 0x142051678, size 0x18): the same
// count, changed under a lock so connections can be shared across threads.
struct UdpGuardedRefCount : UdpRefCount {
  UdpPlatformGuardObject guard;  // +0x10

  void ConstructBase();  // inlined into every derived constructor
};
static_assert(offsetof(UdpGuardedRefCount, guard) == 0x10);
static_assert(sizeof(UdpGuardedRefCount) == 0x18);

inline void** const kUdpRefCountVtable = reinterpret_cast<void**>(0x142051648);
inline void** const kUdpGuardedRefCountVtable = reinterpret_cast<void**>(0x142051678);

}  // namespace rebuild::udp
