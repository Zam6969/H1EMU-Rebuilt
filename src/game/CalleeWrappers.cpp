// Small non-virtual helpers called from already-rebuilt code: varargs
// forwarders (string format, logging), lazy singletons, forwarding thunks.
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::game_callees {
namespace {

template <typename T>
T& At(void* base, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
template <typename R = void, typename... A>
R Virtual(void* self, int slot, A... args) {
  return reinterpret_cast<R (*)(void*, A...)>((*reinterpret_cast<void***>(self))[slot])(self, args...);
}
int64_t TimeNow() {
  int64_t now;
  return *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now);
}
using LogVFn = void (*)(int, void*, const char*, va_list);

}  // namespace

// 0x14032e6c0 / 0x14032e7b0: process / thread id (through the IAT).
uint32_t CurrentProcessId() { return (*reinterpret_cast<uint32_t(__stdcall**)()>(0x14409fe88))(); }
uint32_t CurrentThreadId() { return (*reinterpret_cast<uint32_t(__stdcall**)()>(0x14409fe90))(); }

// 0x1402bab70 / 0x1402baba0 / 0x1402ef740: log at level 4 (info) / 2
// (warning) / 3 to a channel through the va_list logger (0x14032fa90).
void LogInfo(void* channel, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<LogVFn>(0x14032fa90)(4, channel, format, args);
}
void LogWarning(void* channel, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<LogVFn>(0x14032fa90)(2, channel, format, args);
}
void LogLevel3(void* channel, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<LogVFn>(0x14032fa90)(3, channel, format, args);
}

// 0x1402bd7f0 / 0x1402ed6c0: SoeUtil::String Format / AppendFormat.
void StringFormat(void* string, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<void (*)(void*, const char*, va_list)>(0x1402be760)(string, format, args);
}
void StringAppendFormat(void* string, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<void (*)(void*, const char*, va_list)>(0x1402ed6f0)(string, format, args);
}

// 0x140484820: (a, b, c, d, ...) varargs forwarder to 0x140485510.
uint64_t ForwardVarargs140485510(void* a, void* b, void* c, int d, ...) {
  va_list args;
  va_start(args, d);
  return game::Call<uint64_t (*)(void*, void*, void*, int, va_list)>(0x140485510)(a, b, c, d, args);
}

// 0x141381280 / 0x141381260: forward to the wrapped object (+0x10) slots
// 15 / 13 (the latter fills and returns `out`).
uint64_t WrappedSlot15(uint8_t* self) { return Virtual<uint64_t>(At<void*>(self, 0x10), 15); }
void* WrappedSlot13(uint8_t* self, void* out) {
  Virtual(At<void*>(self, 0x10), 13, out);
  return out;
}

// 0x140ab8210: construct through 0x1417d9420, return self.
void* Construct1417d9420(void* self) {
  game::Call<void (*)(void*)>(0x1417d9420)(self);
  return self;
}

// 0x1416044f0: zero a 0x160-byte block.
uint64_t Clear160(void* self) {
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self, 0, 0x160);
  return 0;
}

// 0x141341b90: construct (0x141341820) and publish as the singleton
// (0x143c71aa0).
void ConstructSingleton143c71aa0(void* self) {
  game::Call<void (*)(void*)>(0x141341820)(self);
  *reinterpret_cast<void**>(0x143c71aa0) = self;
}

// 0x1406fb3e0: update (0x1406fa210), then slot 5.
uint64_t Update1406fa210ThenSlot5(void* self) {
  game::Call<void (*)(void*)>(0x1406fa210)(self);
  return Virtual<uint64_t>(self, 5);
}

// 0x14091d9a0: 0x140920360(self), then 0x140c707e0(self + 0x4D4C).
uint64_t Call140920360Then140c707e0(uint8_t* self) {
  game::Call<void (*)(void*)>(0x140920360)(self);
  return game::Call<uint64_t (*)(void*)>(0x140c707e0)(self + 0x4D4C);
}

// 0x140511070: member (+0x20) slot 22 yields an object; call its slot 7.
uint64_t MemberSlot22ThenSlot7(uint8_t* self) {
  void* object = Virtual<void*>(self + 0x20, 22);
  return Virtual<uint64_t>(object, 7);
}

// 0x140aa5c50: base construct (0x140aa5ce0), set the vtable 0x1421878d0.
void* Construct1421878d0(void* self) {
  game::Call<void (*)(void*)>(0x140aa5ce0)(self);
  At<uint64_t>(self, 0) = 0x1421878d0;
  return self;
}

// 0x1403f83f0: forward a copied 64-bit value to 0x14071ee60 on the
// object at +0x38860.
uint64_t Forward14071ee60(uint8_t* self, const uint64_t* value) {
  uint64_t copy = *value;
  return game::Call<uint64_t (*)(void*, uint64_t*)>(0x14071ee60)(At<void*>(self, 0x38860), &copy);
}

// 0x14098dcb0 / 0x140656700: slot 5 on two members.
uint64_t MembersSlot5_10_18(uint8_t* self) {
  Virtual(At<void*>(self, 0x10), 5);
  return Virtual<uint64_t>(At<void*>(self, 0x18), 5);
}
uint64_t MembersSlot5_418_420(uint8_t* self) {
  Virtual(At<void*>(self, 0x418), 5);
  return Virtual<uint64_t>(At<void*>(self, 0x420), 5);
}

// 0x141669a40: mark finished (+0x59) with the finish time (+0x68).
void MarkFinished(uint8_t* self) {
  At<int64_t>(self, 0x68) = TimeNow();
  At<uint8_t>(self, 0x59) = 1;
}

// 0x14050cc70: slot 49 result's +4 minus the float at +0xF94.
float Slot49Minus(uint8_t* self) {
  auto* value = Virtual<uint8_t*>(self, 49);
  return At<float>(value, 4) - At<float>(self, 0xF94);
}

// 0x14042ae90: post event 0xF920AF90 with the object 0x142ab0aa8 to the
// audio manager (0x141501840 -> 0x1415021d0).
uint64_t PostAudioEventF920AF90() {
  void* manager = game::Call<void* (*)()>(0x141501840)();
  return game::Call<uint64_t (*)(void*, uint64_t, uint32_t, float)>(0x1415021d0)(manager, *reinterpret_cast<uint64_t*>(0x142ab0aa8), 0xF920AF90, 0.0f);
}

// 0x140359330 / 0x140427f00: lazily initialized globals.
void* LazyGlobal142b19e30() {
  if (!*reinterpret_cast<bool*>(0x142b19e3a)) {
    game::Call<void (*)()>(0x140355670)();
    *reinterpret_cast<bool*>(0x142b19e3a) = true;
  }
  return *reinterpret_cast<void**>(0x142b19e30);
}
bool LazyGlobal142b19ad8Active() {
  if (!*reinterpret_cast<bool*>(0x142b19e38)) {
    game::Call<void (*)()>(0x1403557c0)();
    *reinterpret_cast<bool*>(0x142b19e38) = true;
  }
  return At<int>(*reinterpret_cast<void**>(0x142b19ad8), 8) != 0;
}

// 0x1407a17b0: 0x1407a1380(self + 0x80, value + 0x10), then slot 5.
uint64_t Call1407a1380ThenSlot5(uint8_t* self, uint8_t* value) {
  game::Call<void (*)(void*, void*)>(0x1407a1380)(self + 0x80, value + 0x10);
  return Virtual<uint64_t>(self, 5);
}

// 0x1408174c0: unless busy (+0x4F0), run 0x1407f4d10 on +0xE8.
bool RunUnlessBusy(uint8_t* self) {
  if (At<int>(self, 0x4F0) != 0) return false;
  game::Call<void (*)(void*)>(0x1407f4d10)(At<void*>(self, 0xE8));
  return true;
}

// 0x1403236d0: 0x1403203e0(&scratch, a, self[+8], !flag, 0).
uint64_t Call1403203e0(uint8_t* self, void* argument, bool flag) {
  uint64_t scratch;
  return game::Call<uint64_t (*)(void*, void*, void*, int, int)>(0x1403203e0)(&scratch, argument, At<void*>(self, 8), flag ? 0 : 1, 0);
}

// 0x141287680: OR the inner object's slot 7 result into the flag at +8.
void AccumulateFlag(uint8_t* self) {
  uint8_t flag = At<uint8_t>(self, 8);
  At<uint8_t>(self, 8) = Virtual<uint8_t>(At<void*>(self, 0), 7) | flag;
}

// 0x140535390: 0x1405352c0(self, a, b), then 0x140533550(self, b).
uint64_t Call1405352c0Then140533550(void* self, void* a, void* b) {
  game::Call<void (*)(void*, void*, void*)>(0x1405352c0)(self, a, b);
  return game::Call<uint64_t (*)(void*, void*)>(0x140533550)(self, b);
}

// 0x1403f71e0: milliseconds since the stored time (clamped to INT_MAX).
int ElapsedMs(const int64_t* start) {
  int64_t elapsed = TimeNow() - *start;
  return elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
}

}  // namespace rebuild::game_callees

using namespace rebuild::game_callees;
REBUILD_FUNCTION(CurrentProcessId, 0x14032e6c0, CurrentProcessId);
REBUILD_FUNCTION(CurrentThreadId, 0x14032e7b0, CurrentThreadId);
REBUILD_FUNCTION(Log_Info, 0x1402bab70, LogInfo);
REBUILD_FUNCTION(Log_Warning, 0x1402baba0, LogWarning);
REBUILD_FUNCTION(Log_Level3, 0x1402ef740, LogLevel3);
REBUILD_FUNCTION(String_Format, 0x1402bd7f0, StringFormat);
REBUILD_FUNCTION(String_AppendFormat, 0x1402ed6c0, StringAppendFormat);
REBUILD_FUNCTION(ForwardVarargs_140485510, 0x140484820, ForwardVarargs140485510);
REBUILD_FUNCTION(WrappedSlot15, 0x141381280, WrappedSlot15);
REBUILD_FUNCTION(WrappedSlot13, 0x141381260, WrappedSlot13);
REBUILD_FUNCTION(Construct_1417d9420, 0x140ab8210, Construct1417d9420);
REBUILD_FUNCTION(Clear160, 0x1416044f0, Clear160);
REBUILD_FUNCTION(ConstructSingleton_143c71aa0, 0x141341b90, ConstructSingleton143c71aa0);
REBUILD_FUNCTION(Update1406fa210ThenSlot5, 0x1406fb3e0, Update1406fa210ThenSlot5);
REBUILD_FUNCTION(Call140920360Then140c707e0, 0x14091d9a0, Call140920360Then140c707e0);
REBUILD_FUNCTION(MemberSlot22ThenSlot7, 0x140511070, MemberSlot22ThenSlot7);
REBUILD_FUNCTION(Construct_1421878d0, 0x140aa5c50, Construct1421878d0);
REBUILD_FUNCTION(Forward14071ee60, 0x1403f83f0, Forward14071ee60);
REBUILD_FUNCTION(MembersSlot5_10_18, 0x14098dcb0, MembersSlot5_10_18);
REBUILD_FUNCTION(MembersSlot5_418_420, 0x140656700, MembersSlot5_418_420);
REBUILD_FUNCTION(MarkFinished, 0x141669a40, MarkFinished);
REBUILD_FUNCTION(Slot49Minus, 0x14050cc70, Slot49Minus);
REBUILD_FUNCTION(PostAudioEventF920AF90, 0x14042ae90, PostAudioEventF920AF90);
REBUILD_FUNCTION(LazyGlobal_142b19e30, 0x140359330, LazyGlobal142b19e30);
REBUILD_FUNCTION(LazyGlobal_142b19ad8Active, 0x140427f00, LazyGlobal142b19ad8Active);
REBUILD_FUNCTION(Call1407a1380ThenSlot5, 0x1407a17b0, Call1407a1380ThenSlot5);
REBUILD_FUNCTION(RunUnlessBusy, 0x1408174c0, RunUnlessBusy);
REBUILD_FUNCTION(Call1403203e0, 0x1403236d0, Call1403203e0);
REBUILD_FUNCTION(AccumulateFlag, 0x141287680, AccumulateFlag);
REBUILD_FUNCTION(Call1405352c0Then140533550, 0x140535390, Call1405352c0Then140533550);
REBUILD_FUNCTION(ElapsedMs, 0x1403f71e0, ElapsedMs);
