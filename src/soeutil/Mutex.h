#pragma once

#include <windows.h>

// SoeUtil::Mutex: a CRITICAL_SECTION, except that when its DebugInfo field
// holds the game's marker (0x142b06ba8) the struct is repurposed as a
// Win32 mutex wrapper: LockSemaphore holds the mutex handle and the owner
// and recursion fields are maintained by hand.
namespace rebuild::soeutil {

inline void* const kMutexHandleMarker = reinterpret_cast<void*>(0x142b06ba8);

void MutexLock(CRITICAL_SECTION* mutex);    // 0x14032f270
void MutexUnlock(CRITICAL_SECTION* mutex);  // 0x14032f360

}  // namespace rebuild::soeutil
