#include "soeutil/Mutex.h"

#include "core/hook.h"

namespace rebuild::soeutil {

// 0x14032f270
void MutexLock(CRITICAL_SECTION* mutex) {
  if (mutex->DebugInfo != kMutexHandleMarker) {
    EnterCriticalSection(mutex);
    return;
  }
  if (WaitForSingleObject(mutex->LockSemaphore, INFINITE) != WAIT_OBJECT_0) {
    GetLastError();
    __debugbreak();
    return;
  }
  DWORD thread = GetCurrentThreadId();
  mutex->RecursionCount += 1;
  mutex->OwningThread = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(thread));
  mutex->LockCount = 1;
}

// 0x14032f360. Unlocking from a thread that does not own it is a hard
// error (breakpoint) in both modes.
void MutexUnlock(CRITICAL_SECTION* mutex) {
  DWORD thread = GetCurrentThreadId();
  if (mutex->OwningThread != reinterpret_cast<HANDLE>(static_cast<uintptr_t>(thread))) {
    __debugbreak();
    return;
  }
  if (mutex->DebugInfo == kMutexHandleMarker) {
    if (--mutex->RecursionCount == 0) {
      mutex->OwningThread = nullptr;
      mutex->LockCount = 0;
    }
    if (!ReleaseMutex(mutex->LockSemaphore)) {
      GetLastError();
      __debugbreak();
    }
    return;
  }
  LeaveCriticalSection(mutex);
}

REBUILD_FUNCTION(SoeUtil_MutexLock, 0x14032f270, MutexLock);
REBUILD_FUNCTION(SoeUtil_MutexUnlock, 0x14032f360, MutexUnlock);

}  // namespace rebuild::soeutil
