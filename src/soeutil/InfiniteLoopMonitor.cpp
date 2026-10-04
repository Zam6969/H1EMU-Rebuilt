// SoeGems::InfiniteLoopMonitor: watchdog thread (SoeUtil::Thread, 0xF0
// bytes). The main loop must keep "kicking" it; if it stops for longer than
// the timeout, or the trigger file appears, the monitor reports the hang
// (0x14032cf80, writes the crash file) and breaks into the debugger.
#include <intrin.h>

#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::soeutil_monitor {
namespace {

struct InfiniteLoopMonitor {
  uint8_t thread[0xB0];           // SoeUtil::Thread base
  soeutil::IString triggerFile;   // +0xB0 (data +0xB8, length +0xC0)
  int64_t lastKick;               // +0xC8
  int unused0D0[2];
  int timeoutMs;                  // +0xD8 (<= 0: no timeout check)
  volatile char kicked;           // +0xDC set by the main loop
  bool enabled;                   // +0xDD
  uint8_t pad0DE[2];
  int checkIntervalMs;            // +0xE0
  uint8_t pad0E4[0xC];
};
static_assert(offsetof(InfiniteLoopMonitor, triggerFile) == 0xB0 && offsetof(InfiniteLoopMonitor, lastKick) == 0xC8 &&
              offsetof(InfiniteLoopMonitor, timeoutMs) == 0xD8 && offsetof(InfiniteLoopMonitor, checkIntervalMs) == 0xE0 &&
              sizeof(InfiniteLoopMonitor) == 0xF0);

int64_t TimeNow() {
  int64_t now;
  return *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now);
}
bool StopRequested(InfiniteLoopMonitor* self) { return game::Call<bool (*)(void*)>(0x140335aa0)(self); }

[[noreturn]] void ReportHang(InfiniteLoopMonitor* self) {
  game::Call<void (*)(void*)>(0x14032cf80)(self);
  __debugbreak();
  for (;;) {
  }
}

}  // namespace

// 0x14032cec0 (slot 0): scalar deleting destructor.
void* InfiniteLoopMonitorDeletingDestructor(InfiniteLoopMonitor* self, unsigned flags) {
  *reinterpret_cast<uint64_t*>(self) = 0x14204eba0;
  self->triggerFile.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&self->triggerFile);
  game::Call<void (*)(void*)>(0x1403358a0)(self);  // ~Thread
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, 0xF0);
  return self;
}

// 0x14032d010 (slot 2): thread body.
void InfiniteLoopMonitorRun(InfiniteLoopMonitor* self) {
  if (self->timeoutMs > 0) self->lastKick = TimeNow();
  while (!StopRequested(self)) {
    if (self->enabled) {
      if (self->timeoutMs > 0) {
        if (_InterlockedExchange8(&self->kicked, 0)) {
          self->lastKick = TimeNow();
        } else {
          int64_t idle = TimeNow() - self->lastKick;
          int idleMs = idle > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(idle);
          if (idleMs > self->timeoutMs) ReportHang(self);
        }
      }
      if (self->triggerFile.length > 0 && game::Call<bool (*)(const char*, void*)>(0x140336bd0)(self->triggerFile.data, nullptr)) {
        game::Call<void (*)(const char*)>(0x140336540)(self->triggerFile.data);  // delete the trigger file
        ReportHang(self);
      }
    }
    game::Call<void (*)(int)>(0x14032ec60)(self->checkIntervalMs);  // sleep
  }
}

}  // namespace rebuild::soeutil_monitor

using namespace rebuild::soeutil_monitor;
REBUILD_FUNCTION(InfiniteLoopMonitor_DeletingDestructor, 0x14032cec0, InfiniteLoopMonitorDeletingDestructor);
REBUILD_FUNCTION(InfiniteLoopMonitor_Run, 0x14032d010, InfiniteLoopMonitorRun);
