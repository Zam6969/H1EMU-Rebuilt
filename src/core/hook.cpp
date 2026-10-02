#include "core/hook.h"

#include <MinHook.h>
#include <windows.h>

#include <vector>

#include "core/config.h"
#include "core/log.h"

namespace rebuild {
namespace {

// Function-local so registration works from any translation unit's static
// initializers regardless of their order.
std::vector<HookDef>& Registry() {
  static std::vector<HookDef> hooks;
  return hooks;
}

}  // namespace

bool RegisterHook(const HookDef& def) {
  Registry().push_back(def);
  return true;
}

HookResult InstallHooks() {
  HookResult result;
  if (MH_Initialize() != MH_OK) {
    log::Error("MinHook failed to initialize");
    result.failed = static_cast<int>(Registry().size());
    return result;
  }

  for (const HookDef& hook : Registry()) {
    if (!hook.hookable) {
      ++result.tooSmall;
      continue;
    }
    if (!config::HookEnabled(hook.name)) {
      log::Info("skip    %-40s 0x%llx (disabled in rebuild.ini)", hook.name,
                static_cast<unsigned long long>(hook.address));
      continue;
    }
    auto* target = reinterpret_cast<void*>(hook.address);
    MH_STATUS status = MH_CreateHook(target, hook.replacement, hook.original);
    if (status == MH_OK) status = MH_EnableHook(target);
    if (status != MH_OK) {
      log::Error("failed  %-40s 0x%llx: %s", hook.name,
                 static_cast<unsigned long long>(hook.address), MH_StatusToString(status));
      ++result.failed;
      continue;
    }
    log::Info("rebuilt %-40s 0x%llx", hook.name, static_cast<unsigned long long>(hook.address));
    ++result.installed;
  }
  return result;
}

}  // namespace rebuild
