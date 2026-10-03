#pragma once

#include <cstdint>

namespace rebuild {

struct HookDef {
  const char* name;      // key in rebuild.ini [hooks]
  uintptr_t address;     // original function in H1Z1.exe
  void* replacement;     // rebuilt C++ function
  void** original;       // optional: receives a trampoline to the original
  bool hookable = true;  // false: original is under 5 bytes, so it stays in place
};

// Called by REBUILD_FUNCTION at static-init time.
bool RegisterHook(const HookDef& def);

struct HookResult {
  int installed = 0;
  int failed = 0;
  int tooSmall = 0;  // rebuilt, but the original is too short for a jump
};

// Installs every registered hook not disabled in rebuild.ini.
HookResult InstallHooks();

}  // namespace rebuild

// Replaces the game function at `address` with `fn`. Turn a single one off
// with `<name>=0` under [hooks] in rebuild.ini to fall back to the original.
#define REBUILD_FUNCTION(name, address, fn)                                     \
  static const bool rebuild_hook_##name =                                       \
      ::rebuild::RegisterHook({#name, address, reinterpret_cast<void*>(&fn), nullptr})

// For originals shorter than the 5-byte jump a hook needs (e.g. `ret`-ending
// one-liners). The C++ is kept as the rebuilt source of truth, but the
// original stays in place since it cannot be patched safely.
#define REBUILD_FUNCTION_TOO_SMALL(name, address, fn)                           \
  static const bool rebuild_hook_##name = ::rebuild::RegisterHook(             \
      {#name, address, reinterpret_cast<void*>(&fn), nullptr, false})

// Like REBUILD_FUNCTION, but `original` (a function pointer variable) receives
// a trampoline to the original game code. Used for partially rebuilt
// functions, e.g. a dispatcher whose not-yet-rebuilt cases still run the
// original body.
#define REBUILD_FUNCTION_WITH_ORIGINAL(name, address, fn, original)             \
  static const bool rebuild_hook_##name = ::rebuild::RegisterHook(             \
      {#name, address, reinterpret_cast<void*>(&fn), reinterpret_cast<void**>(&original)})
