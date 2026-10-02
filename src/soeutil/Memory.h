#pragma once

#include <cstddef>

#include "core/game.h"

// The game's allocators. Anything the original code may free (or that we free
// after the original allocated it) must go through these, never through this
// DLL's own CRT heap.
namespace rebuild::soeutil {

// global operator new / sized operator delete
inline void* Allocate(size_t size) { return game::Call<void* (*)(size_t)>(0x1402fc0f0)(size); }
inline void Free(void* memory, size_t size) {
  game::Call<void (*)(void*, size_t)>(0x140d0fb84)(memory, size);
}

// operator new[] / operator delete[]
inline void* AllocateArray(size_t size) {
  return game::Call<void* (*)(size_t)>(0x1402fc140)(size);
}
inline void FreeArray(void* memory) { game::Call<void (*)(void*)>(0x1402fc170)(memory); }

// The game's statically linked CRT heap (_malloc_base / _realloc_base / _free_base)
inline void* CrtMalloc(size_t size) { return game::Call<void* (*)(size_t)>(0x140d42430)(size); }
inline void* CrtRealloc(void* memory, size_t size) {
  return game::Call<void* (*)(void*, size_t)>(0x140d47d1c)(memory, size);
}
inline void CrtFree(void* memory) { game::Call<void (*)(void*)>(0x140d42428)(memory); }

}  // namespace rebuild::soeutil
