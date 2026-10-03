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

// 0x14032ee90. SoeUtil::Mutex is a CRITICAL_SECTION followed by a 32-byte
// debug name (max 31 characters).
CRITICAL_SECTION* MutexConstruct(CRITICAL_SECTION* mutex, DWORD spinCount, const char* name) {
  char* out = reinterpret_cast<char*>(mutex + 1);
  char* end = out + 0x1F;
  const char* in = name ? name : reinterpret_cast<const char*>(0x142046fcb);
  while (out != end && *in) *out++ = *in++;
  *out = 0;
  if (!InitializeCriticalSectionAndSpinCount(mutex, spinCount)) {
    GetLastError();
    __debugbreak();
  }
  return mutex;
}

// 0x14032f060: ~Mutex. Destroying a held mutex is a fatal error.
void MutexDestroy(CRITICAL_SECTION* mutex) {
  if (mutex->OwningThread) __debugbreak();
  if (mutex->DebugInfo != kMutexHandleMarker) {
    DeleteCriticalSection(mutex);
    return;
  }
  if (mutex->LockCount != 0) __debugbreak();
  HANDLE handle = InterlockedExchangePointer(&mutex->LockSemaphore, nullptr);
  if (!handle) __debugbreak();
  CloseHandle(handle);
}

REBUILD_FUNCTION(SoeUtil_MutexDestroy, 0x14032f060, MutexDestroy);
REBUILD_FUNCTION(SoeUtil_MutexConstruct, 0x14032ee90, MutexConstruct);
REBUILD_FUNCTION(SoeUtil_MutexLock, 0x14032f270, MutexLock);
REBUILD_FUNCTION(SoeUtil_MutexUnlock, 0x14032f360, MutexUnlock);

}  // namespace rebuild::soeutil
