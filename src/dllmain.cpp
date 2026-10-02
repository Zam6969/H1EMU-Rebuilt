#include <windows.h>

#include "core/config.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"

using namespace rebuild;

// Runs while Windows resolves H1Z1.exe's imports, i.e. before any game code.
// Hooks are installed right here so even the earliest game functions can be
// replaced. Only loader-lock-safe work belongs in this path.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason != DLL_PROCESS_ATTACH) return TRUE;
  DisableThreadLibraryCalls(module);

  config::Load();
  if (!config::Enabled()) return TRUE;

  if (!game::IsTargetBuild()) {
    // Loaded by something other than the game (e.g. tools/hookcheck): stay inert.
    return TRUE;
  }
  log::Init(config::Console());
  HookResult result = InstallHooks();
  log::Info("%d rebuilt function(s) active, %d failed, %d too small to hook", result.installed, result.failed, result.tooSmall);
  return TRUE;
}

// Used by tools/hookcheck: H1Z1.exe is mapped as an image (not run) at its
// fixed base, then every hook is installed against it. Returns the number of
// hooks that failed to install.
extern "C" __declspec(dllexport) int RebuildSelfTest() {
  log::Init(false);
  HookResult result = InstallHooks();
  log::Info("selftest: %d installed, %d failed, %d too small to hook", result.installed, result.failed, result.tooSmall);
  return result.failed;
}
