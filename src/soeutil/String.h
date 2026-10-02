#pragma once

#include "core/game.h"

// SoeUtil string helpers that are not rebuilt yet. The string object's layout
// is still unknown, so it is passed as an opaque pointer.
namespace rebuild::soeutil {

// 0x1402bd670: *string = text
inline void StringAssign(void* string, const char* text) {
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(string, text);
}

// 0x1402bd7f0: *string = sprintf(format, ...)
template <class... Args>
void StringFormat(void* string, const char* format, Args... args) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402bd7f0)(string, format, args...);
}

}  // namespace rebuild::soeutil
