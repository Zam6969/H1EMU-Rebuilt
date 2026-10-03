// SoeUtil high-resolution time.
#include <windows.h>

#include <cstdint>

#include "core/crt.h"
#include "core/hook.h"

namespace rebuild::soeutil {

// 0x14032fd30: milliseconds from QueryPerformanceCounter. The ms-per-tick
// scale (1000.0 / frequency) is a function-local static in the game's .data.
int64_t* TimeNow(int64_t* out) {
  auto* scale = reinterpret_cast<double*>(0x142b06ca8);
  crt::ThreadSafeStatic(reinterpret_cast<int*>(0x142b06cb0), [scale] {
    LARGE_INTEGER frequency;
    if (QueryPerformanceFrequency(&frequency) != TRUE) __debugbreak();
    *scale = *reinterpret_cast<const double*>(0x14204f188) / static_cast<double>(frequency.QuadPart);
  });
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  *out = static_cast<int64_t>(static_cast<double>(counter.QuadPart) * *scale);
  return out;
}

REBUILD_FUNCTION(SoeUtil_TimeNow, 0x14032fd30, TimeNow);

}  // namespace rebuild::soeutil
