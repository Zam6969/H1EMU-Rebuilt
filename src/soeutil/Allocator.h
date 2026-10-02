#pragma once

#include <windows.h>

#include <cstddef>

// SoeUtil's pluggable allocator. A thread can install its own allocator
// (SetThreadAllocator); otherwise a process-wide one is used if set, and
// otherwise aligned operator new[].
namespace rebuild::soeutil {

// Interface: slot 1 Allocate(size, alignment), slot 2 Free(ptr, alignment).
struct IAllocator {
  void** vtable;

  void* Allocate(int size, int alignment) {
    return reinterpret_cast<void* (*)(IAllocator*, int, int)>(vtable[1])(this, size, alignment);
  }
  void Free(void* memory, int alignment) {
    reinterpret_cast<void (*)(IAllocator*, void*, int)>(vtable[2])(this, memory, alignment);
  }
};

// Number of threads that currently have their own allocator; containers
// check it to choose between SoeUtil Allocate/Free and plain new[]/delete[].
inline volatile LONG& ThreadAllocatorCount() { return *reinterpret_cast<volatile LONG*>(0x143e09638); }
inline IAllocator*& GlobalAllocator() { return *reinterpret_cast<IAllocator**>(0x142b06c58); }

DWORD* AllocatorTlsSlot();                             // 0x14032f9e0
IAllocator* SetThreadAllocator(IAllocator* allocator);  // 0x14032f8b0
void* MemoryAllocate(int size, int alignment);               // 0x14032f910
void MemoryFree(void* memory, int alignment);                // 0x14032f980
void* DefaultAllocate(int size, int alignment);        // 0x14033a580
void DefaultFree(void* memory, int alignment);         // 0x14033a5d0

}  // namespace rebuild::soeutil
