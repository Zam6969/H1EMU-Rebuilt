#include "udp/UdpRefCount.h"

#include "core/hook.h"
#include "soeutil/Memory.h"

namespace rebuild::udp {

void UdpPlatformGuardObject::Construct() {
  section = static_cast<CRITICAL_SECTION*>(soeutil::Allocate(sizeof(CRITICAL_SECTION)));
  InitializeCriticalSection(section);
}

void UdpPlatformGuardObject::Destruct() {
  DeleteCriticalSection(section);
  soeutil::Free(section, sizeof(CRITICAL_SECTION));
}

void UdpPlatformGuardObject::Enter() { EnterCriticalSection(section); }
void UdpPlatformGuardObject::Leave() { LeaveCriticalSection(section); }

void UdpGuardedRefCount::ConstructBase() {
  refCount = 1;
  flag = false;
  vtable = kUdpGuardedRefCountVtable;
  guard.Construct();
}

namespace {

UdpPlatformGuardObject* GuardConstruct(UdpPlatformGuardObject* self) {
  self->Construct();
  return self;
}
void GuardDestruct(UdpPlatformGuardObject* self) { self->Destruct(); }
void GuardEnter(UdpPlatformGuardObject* self) { self->Enter(); }
void GuardLeave(UdpPlatformGuardObject* self) { self->Leave(); }

// 0x14033e1e0, UdpRefCount slot 0
void AddRef(UdpRefCount* self) { ++self->refCount; }

// 0x140343000, UdpRefCount slot 1
void Release(UdpRefCount* self) {
  if (--self->refCount == 0) self->VirtualDelete();
}

// 0x140342470, slot 2. The compiler folded every `this->byte_0C = true`
// setter into this one body, so other classes' vtables point here too.
void SetFlag(UdpRefCount* self) { self->flag = true; }

// 0x140340f00, slot 3. Also a folded body (`return this->int_08`).
int GetRefCount(UdpRefCount* self) { return self->refCount; }

// 0x14033d940, UdpRefCount slot 4
UdpRefCount* RefCountDeletingDestructor(UdpRefCount* self, unsigned flags) {
  self->vtable = kUdpRefCountVtable;
  if (flags & 1) soeutil::Free(self, sizeof(UdpRefCount));
  return self;
}

// 0x14034bbe0, UdpGuardedRefCount slot 0
void GuardedAddRef(UdpGuardedRefCount* self) {
  self->guard.Enter();
  ++self->refCount;
  self->guard.Leave();
}

// 0x14034bc20, UdpGuardedRefCount slot 1
void GuardedRelease(UdpGuardedRefCount* self) {
  self->guard.Enter();
  int remaining = --self->refCount;
  self->guard.Leave();
  if (remaining == 0) self->VirtualDelete();
}

// 0x14033d870, UdpGuardedRefCount slot 4 (Ghidra's function-ID mislabels it
// as a Concurrency runtime destructor because the bodies are identical).
UdpGuardedRefCount* GuardedDeletingDestructor(UdpGuardedRefCount* self, unsigned flags) {
  self->guard.Destruct();
  self->vtable = kUdpRefCountVtable;
  if (flags & 1) soeutil::Free(self, sizeof(UdpGuardedRefCount));
  return self;
}

}  // namespace

REBUILD_FUNCTION(UdpPlatformGuardObject_Construct, 0x14034a030, GuardConstruct);
REBUILD_FUNCTION(UdpPlatformGuardObject_Destruct, 0x14034a1d0, GuardDestruct);
REBUILD_FUNCTION(UdpPlatformGuardObject_Enter, 0x14034a560, GuardEnter);
REBUILD_FUNCTION(UdpPlatformGuardObject_Leave, 0x14034a8b0, GuardLeave);
REBUILD_FUNCTION_TOO_SMALL(UdpRefCount_AddRef, 0x14033e1e0, AddRef);
REBUILD_FUNCTION(UdpRefCount_Release, 0x140343000, Release);
REBUILD_FUNCTION(UdpRefCount_SetFlag, 0x140342470, SetFlag);
REBUILD_FUNCTION_TOO_SMALL(UdpRefCount_GetRefCount, 0x140340f00, GetRefCount);
REBUILD_FUNCTION(UdpRefCount_DeletingDestructor, 0x14033d940, RefCountDeletingDestructor);
REBUILD_FUNCTION(UdpGuardedRefCount_AddRef, 0x14034bbe0, GuardedAddRef);
REBUILD_FUNCTION(UdpGuardedRefCount_Release, 0x14034bc20, GuardedRelease);
REBUILD_FUNCTION(UdpGuardedRefCount_DeletingDestructor, 0x14033d870, GuardedDeletingDestructor);

}  // namespace rebuild::udp
