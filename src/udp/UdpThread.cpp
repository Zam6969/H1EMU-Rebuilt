#include "udp/UdpThread.h"

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

// The game's statically linked CRT _beginthreadex, so the new thread gets the
// game CRT's per-thread state rather than this DLL's.
uintptr_t BeginThreadEx(void* security, unsigned stackSize, unsigned(__stdcall* start)(void*),
                        void* argument, unsigned flags, unsigned* threadId) {
  using Fn = uintptr_t (*)(void*, unsigned, unsigned(__stdcall*)(void*), void*, unsigned, unsigned*);
  return game::Call<Fn>(0x140d46e04)(security, stackSize, start, argument, flags, threadId);
}

// UdpManager fields used here (UdpManager is not reconstructed yet).
UdpPlatformDriver* ManagerDriver(UdpManager* manager) {
  return game::Field<UdpPlatformDriver*>(manager, 0x2a8);
}
UdpRefCount* ManagerRefCount(UdpManager* manager) { return reinterpret_cast<UdpRefCount*>(manager); }
void ManagerGiveTime(UdpManager* manager, int maxPollingTime, bool giveConnectionsTime) {
  game::Call<void (*)(UdpManager*, int, bool)>(0x1403413c0)(manager, maxPollingTime,
                                                            giveConnectionsTime);
}
void DriverSleep(UdpPlatformDriver* driver, int milliseconds) {
  reinterpret_cast<void (*)(UdpPlatformDriver*, int)>(driver->vtable[kSlotSleep])(driver,
                                                                                    milliseconds);
}

bool VirtualIsRunning(UdpPlatformThreadObject* self) {
  return reinterpret_cast<bool (*)(UdpPlatformThreadObject*)>(
      self->vtable[UdpPlatformThreadObject::kIsRunning])(self);
}

// ---- UdpPlatformThreadObject ----

// 0x14034a060
UdpPlatformThreadObject* ThreadObjectConstruct(UdpPlatformThreadObject* self) {
  self->ConstructBase();
  self->vtable = kUdpPlatformThreadObjectVtable;
  self->thread = static_cast<UdpThreadHandle*>(soeutil::Allocate(sizeof(UdpThreadHandle)));
  self->thread->handle = 0;
  self->thread->running = false;
  return self;
}

// 0x14034a200
void ThreadObjectDestruct(UdpPlatformThreadObject* self) {
  self->vtable = kUdpPlatformThreadObjectVtable;
  if (self->thread->handle) {
    CloseHandle(reinterpret_cast<HANDLE>(self->thread->handle));
    self->thread->handle = 0;
  }
  soeutil::Free(self->thread, sizeof(UdpThreadHandle));
  self->guard.Destruct();
  self->vtable = kUdpRefCountVtable;
}

// 0x14034a3a0, slot 4
UdpPlatformThreadObject* ThreadObjectDeletingDestructor(UdpPlatformThreadObject* self,
                                                        unsigned flags) {
  ThreadObjectDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(UdpPlatformThreadObject));
  return self;
}

// 0x14034a770: thread entry. Holds the reference Start() took until Run() returns.
unsigned __stdcall ThreadProc(void* argument) {
  auto* self = static_cast<UdpPlatformThreadObject*>(argument);
  self->thread->running = true;
  reinterpret_cast<void (*)(UdpPlatformThreadObject*)>(
      self->vtable[UdpPlatformThreadObject::kRun])(self);
  self->thread->running = false;
  self->VirtualRelease();
  return 0;
}

// 0x14034b1b0, slot 5
void Start(UdpPlatformThreadObject* self) {
  self->VirtualAddRef();
  unsigned threadId;
  self->thread->handle = BeginThreadEx(nullptr, 0, ThreadProc, self, 0, &threadId);
}

// 0x14034a8a0, slot 6
bool IsRunning(UdpPlatformThreadObject* self) { return self->thread->running; }

// ---- UdpManagerThread ----

// 0x14033c6c0
UdpManagerThread* ManagerThreadConstruct(UdpManagerThread* self, UdpManager* manager,
                                         int sleepTime) {
  ThreadObjectConstruct(self);
  self->vtable = kUdpManagerThreadVtable;
  self->manager = manager;
  ManagerRefCount(manager)->VirtualAddRef();
  self->stop = false;
  self->sleepTime = sleepTime;
  return self;
}

// 0x14033d560: asks the thread to stop and waits for it in 10 ms steps.
void ManagerThreadDestruct(UdpManagerThread* self) {
  self->vtable = kUdpManagerThreadVtable;
  self->stop = true;
  // The first check is a direct call, later ones go through the vtable.
  for (bool running = IsRunning(self); running; running = VirtualIsRunning(self))
    DriverSleep(ManagerDriver(self->manager), 10);
  ManagerRefCount(self->manager)->VirtualRelease();
  ThreadObjectDestruct(self);
}

// 0x14033d900, slot 4
UdpManagerThread* ManagerThreadDeletingDestructor(UdpManagerThread* self, unsigned flags) {
  ManagerThreadDestruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(UdpManagerThread));
  return self;
}

// 0x1403447e0, slot 7
void ManagerThreadRun(UdpManagerThread* self) {
  while (!self->stop) {
    ManagerGiveTime(self->manager, 500, true);
    DriverSleep(ManagerDriver(self->manager), self->sleepTime);
  }
}

}  // namespace

REBUILD_FUNCTION(UdpPlatformThreadObject_Construct, 0x14034a060, ThreadObjectConstruct);
REBUILD_FUNCTION(UdpPlatformThreadObject_Destruct, 0x14034a200, ThreadObjectDestruct);
REBUILD_FUNCTION(UdpPlatformThreadObject_DeletingDestructor, 0x14034a3a0, ThreadObjectDeletingDestructor);
REBUILD_FUNCTION(UdpPlatformThreadObject_ThreadProc, 0x14034a770, ThreadProc);
REBUILD_FUNCTION(UdpPlatformThreadObject_Start, 0x14034b1b0, Start);
REBUILD_FUNCTION(UdpPlatformThreadObject_IsRunning, 0x14034a8a0, IsRunning);
REBUILD_FUNCTION(UdpManagerThread_Construct, 0x14033c6c0, ManagerThreadConstruct);
REBUILD_FUNCTION(UdpManagerThread_Destruct, 0x14033d560, ManagerThreadDestruct);
REBUILD_FUNCTION(UdpManagerThread_DeletingDestructor, 0x14033d900, ManagerThreadDeletingDestructor);
REBUILD_FUNCTION(UdpManagerThread_Run, 0x1403447e0, ManagerThreadRun);

}  // namespace rebuild::udp
