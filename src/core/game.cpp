#include "core/game.h"

#include <windows.h>

namespace rebuild::game {

bool IsTargetBuild() {
  auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
  if (reinterpret_cast<uintptr_t>(base) != kImageBase) return false;
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  return nt->FileHeader.TimeDateStamp == kTimestamp &&
         nt->OptionalHeader.SizeOfImage == kImageSize;
}

}  // namespace rebuild::game
