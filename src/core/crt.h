#pragma once

#include <windows.h>

#include <cstdint>

#include "core/game.h"

// Entry points into H1Z1.exe's statically linked MSVC CRT, so rebuilt code
// shares the game's runtime state instead of this DLL's separate CRT.
namespace rebuild::crt {

// The game's _tls_index (the exe's TLS slot in the TEB array).
inline uint32_t TlsIndex() { return *reinterpret_cast<uint32_t*>(0x143c466a8); }

// The per-thread epoch MSVC's thread-safe statics compare against: the int at
// +0x10 in the exe's TLS block.
inline int ThreadEpoch() {
  auto** tlsArray = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
  return *reinterpret_cast<int*>(tlsArray[TlsIndex()] + 0x10);
}

inline void InitThreadHeader(int* guard) { game::Call<void (*)(int*)>(0x140d100ac)(guard); }
inline void InitThreadFooter(int* guard) { game::Call<void (*)(int*)>(0x140d1004c)(guard); }
inline int Atexit(uintptr_t function) { return game::Call<int (*)(uintptr_t)>(0x140d107a0)(function); }

// What MSVC emits for `static T x = init();` in game code, using the game's
// guard variable so original and rebuilt code agree on the initialization.
template <class Init>
void ThreadSafeStatic(int* guard, Init&& init) {
  if (ThreadEpoch() < *guard) {
    InitThreadHeader(guard);
    if (*guard == -1) {
      init();
      InitThreadFooter(guard);
    }
  }
}

}  // namespace rebuild::crt
