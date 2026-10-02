#pragma once

#include <cstddef>
#include <cstdint>

#include "udp/UdpRefCount.h"

namespace rebuild::udp {

struct UdpManager;  // not reconstructed yet; fields reached by offset

struct UdpThreadHandle {
  uintptr_t handle;  // +0x00 from _beginthreadex
  bool running;      // +0x08 true while Run() executes
};
static_assert(sizeof(UdpThreadHandle) == 0x10);

// UdpLibrary::UdpPlatformThreadObject (vtable 0x142052528, size 0x20).
// Slot 5 Start(), slot 6 IsRunning(), slot 7 Run() (pure virtual).
struct UdpPlatformThreadObject : UdpGuardedRefCount {
  UdpThreadHandle* thread;  // +0x18

  enum Slot { kStart = 5, kIsRunning = 6, kRun = 7 };
};
static_assert(offsetof(UdpPlatformThreadObject, thread) == 0x18);
static_assert(sizeof(UdpPlatformThreadObject) == 0x20);

// UdpLibrary::UdpManagerThread (vtable 0x1420517a8, size 0x30): keeps a
// UdpManager pumped from a background thread.
struct UdpManagerThread : UdpPlatformThreadObject {
  UdpManager* manager;  // +0x20, AddRef'd for the thread's lifetime
  bool stop;            // +0x28
  int sleepTime;        // +0x2C milliseconds between GiveTime calls
};
static_assert(offsetof(UdpManagerThread, manager) == 0x20);
static_assert(offsetof(UdpManagerThread, stop) == 0x28);
static_assert(offsetof(UdpManagerThread, sleepTime) == 0x2C);
static_assert(sizeof(UdpManagerThread) == 0x30);

inline void** const kUdpPlatformThreadObjectVtable = reinterpret_cast<void**>(0x142052528);
inline void** const kUdpManagerThreadVtable = reinterpret_cast<void**>(0x1420517a8);

}  // namespace rebuild::udp
