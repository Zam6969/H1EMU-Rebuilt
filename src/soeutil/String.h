#pragma once

#include <cstddef>
#include <cstdint>

#include "core/game.h"

// SoeUtil::IString<char> and SoeUtil::StringFixed<N>.
//
// A string is {vtable, data, length, capacity}. The buffer that data points
// into starts 4 bytes earlier with an atomic share count, so copies of a heap
// string share one buffer (copy-on-write). capacity <= 0 means data points at
// static storage (the shared empty string) and must not be released.
// StringFixed<N> appends an inline buffer of N+4 bytes (count + N chars) and
// only allocates once a string outgrows it.
namespace rebuild::soeutil {

struct IString {
  void** vtable;
  char* data;
  int length;
  int capacity;
};
static_assert(sizeof(IString) == 0x18);

template <int N>
struct StringFixed : IString {
  static constexpr int kInlineBytes = N + 4;
  alignas(4) uint8_t inlineBuffer[kInlineBytes];
};
static_assert(offsetof(StringFixed<64>, inlineBuffer) == 0x18);
static_assert(sizeof(StringFixed<64>) == 0x60);
static_assert(sizeof(StringFixed<512>) == 0x220);
static_assert(sizeof(StringFixed<5>) == 0x28);
static_assert(sizeof(StringFixed<24>) == 0x38);

// Starts a StringFixed<N> empty with the given vtable (the inline buffer is
// only used once the allocator hands it out).
template <int N>
inline void InitFixed(StringFixed<N>& string, void** vtable) {
  string.vtable = vtable;
  string.data = reinterpret_cast<char*>(0x143e09641);
  string.length = 0;
  string.capacity = 0;
}

// IString vtable slots.
constexpr size_t kStringSlotAllocate = 1;  // (bytes, &capacity, &isHeap) -> buffer
constexpr size_t kStringSlotFree = 2;      // (buffer)

inline char* EmptyStringData() { return reinterpret_cast<char*>(0x143e09641); }
inline void** IStringVtable() { return reinterpret_cast<void**>(0x142049b50); }

void StringRelease(IString* string);                               // inlined everywhere
void StringReserve(IString* string, int bytes);                    // 0x1402befa0
void StringAssign(IString* string, const char* text);              // 0x1402bd670
void StringAssignString(IString* string, const IString* source);   // 0x1402bd560
void StringAssignN(IString* string, const char* text, int length);  // 0x1402be6c0

inline void StringAssign(void* string, const char* text) { StringAssign(static_cast<IString*>(string), text); }

// 0x1402bd7f0: *string = sprintf(format, ...)
template <class... Args>
void StringFormat(void* string, const char* format, Args... args) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402bd7f0)(string, format, args...);
}

}  // namespace rebuild::soeutil
