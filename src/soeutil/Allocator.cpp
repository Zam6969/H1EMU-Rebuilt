#include "soeutil/Allocator.h"

#include <cstdint>

#include "core/crt.h"
#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"

namespace rebuild::soeutil {
namespace {

int* const kTlsSlotGuard = reinterpret_cast<int*>(0x142b06c64);
DWORD* const kTlsSlot = reinterpret_cast<DWORD*>(0x142b06c60);
constexpr uintptr_t kTlsSlotAtexit = 0x142016030;  // TlsFree(slot) at exit

// operator new[](size, std::nothrow); 0x143c46658 is std::nothrow.
void* NewArrayNoThrow(size_t size) {
  return game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(size,
                                                                  reinterpret_cast<void*>(0x143c46658));
}

}  // namespace

// 0x14032f9e0: function-local static TLS slot.
DWORD* AllocatorTlsSlot() {
  crt::ThreadSafeStatic(kTlsSlotGuard, [] {
    *kTlsSlot = TlsAlloc();
    if (*kTlsSlot == TLS_OUT_OF_INDEXES) __debugbreak();
    crt::Atexit(kTlsSlotAtexit);
  });
  return kTlsSlot;
}

// 0x14032f8b0. Returns the thread's previous allocator.
IAllocator* SetThreadAllocator(IAllocator* allocator) {
  auto* previous = static_cast<IAllocator*>(TlsGetValue(*AllocatorTlsSlot()));
  if (allocator) InterlockedIncrement(&ThreadAllocatorCount());
  if (previous) InterlockedDecrement(&ThreadAllocatorCount());
  TlsSetValue(*AllocatorTlsSlot(), allocator);
  return previous;
}

// 0x14032f910
void* MemoryAllocate(int size, int alignment) {
  if (auto* thread = static_cast<IAllocator*>(TlsGetValue(*AllocatorTlsSlot())))
    return thread->Allocate(size, alignment);
  if (IAllocator* global = GlobalAllocator()) return global->Allocate(size, alignment);
  return DefaultAllocate(size, alignment);
}

// 0x14032f980. Faithful to the original: unlike Allocate, this does not
// stop at the first allocator - the thread allocator, the global allocator
// and the default free are all called in turn.
void MemoryFree(void* memory, int alignment) {
  if (auto* thread = static_cast<IAllocator*>(TlsGetValue(*AllocatorTlsSlot())))
    thread->Free(memory, alignment);
  if (IAllocator* global = GlobalAllocator()) global->Free(memory, alignment);
  DefaultFree(memory, alignment);
}

// 0x14033a580. Over-aligned blocks store the distance back to the real
// allocation in the byte just before the returned pointer.
void* DefaultAllocate(int size, int alignment) {
  if (alignment > 8) {
    auto raw = reinterpret_cast<uintptr_t>(NewArrayNoThrow(static_cast<size_t>(size + alignment)));
    uintptr_t aligned = (raw + alignment) & ~static_cast<uintptr_t>(alignment - 1);
    reinterpret_cast<uint8_t*>(aligned)[-1] = static_cast<uint8_t>(aligned - raw);
    return reinterpret_cast<void*>(aligned);
  }
  return NewArrayNoThrow(static_cast<size_t>(size));
}

// 0x14033a5d0
void DefaultFree(void* memory, int alignment) {
  if (!memory) return;
  auto* block = static_cast<uint8_t*>(memory);
  if (alignment > 8) block -= block[-1];
  FreeArray(block);
}

REBUILD_FUNCTION(SoeUtil_AllocatorTlsSlot, 0x14032f9e0, AllocatorTlsSlot);
REBUILD_FUNCTION(SoeUtil_SetThreadAllocator, 0x14032f8b0, SetThreadAllocator);
REBUILD_FUNCTION(SoeUtil_MemoryAllocate, 0x14032f910, MemoryAllocate);
REBUILD_FUNCTION(SoeUtil_MemoryFree, 0x14032f980, MemoryFree);
REBUILD_FUNCTION(SoeUtil_DefaultAllocate, 0x14033a580, DefaultAllocate);
REBUILD_FUNCTION(SoeUtil_DefaultFree, 0x14033a5d0, DefaultFree);

}  // namespace rebuild::soeutil
