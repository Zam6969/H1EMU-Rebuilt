// Small non-virtual helpers called from already-rebuilt code: varargs
// forwarders (string format, logging), lazy singletons, forwarding thunks.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <intrin.h>

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


// 0x1403243b0 / 0x140428ce0: log at level 1 / 5.
void LogLevel1(void* channel, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<LogVFn>(0x14032fa90)(1, channel, format, args);
}
void LogLevel5(void* channel, const char* format, ...) {
  va_list args;
  va_start(args, format);
  game::Call<LogVFn>(0x14032fa90)(5, channel, format, args);
}

// 0x140927e90: copy the shared pointer at +0x51A8 into out (bump weak,
// then strong count in its block at object +8).
void** CopySharedAt51a8(uint8_t* self, void** out) {
  uint8_t* object = At<uint8_t*>(self, 0x51A8);
  *out = object;
  if (object) {
    auto* counts = At<volatile long*>(object, 8);
    _InterlockedIncrement(&counts[1]);
    _InterlockedIncrement(&counts[0]);
  }
  return out;
}

// 0x140736260: assign the name (+0x1C0) and its id (+0x1D8).
void SetNameAndId(uint8_t* self, const char* name, int id) {
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self + 0x1C0, name);
  At<int>(self, 0x1D8) = id;
}

// 0x141ec5ffc: strtok on the per-thread CRT context (thread data +0x30).
char* Strtok(char* text, const char* delimiters) {
  uint8_t* thread = game::Call<uint8_t* (*)()>(0x140d5fa78)();
  return game::Call<char* (*)(char*, const char*, void*)>(0x141940adc)(text, delimiters, thread + 0x30);
}

// 0x14071e7a0: read +0x9F0 under the object's own mutex.
uint64_t LockedRead9f0(uint8_t* self) {
  game::Call<void (*)(void*)>(0x14032f270)(self);
  uint64_t value = At<uint64_t>(self, 0x9F0);
  game::Call<void (*)(void*)>(0x14032f360)(self);
  return value;
}

// 0x140332e60: XOR of the hashes (+0xE88) up the parent chain (+0xE80).
uint64_t ChainHash(uint8_t* self) {
  uint64_t hash = At<uint64_t>(self, 0xE88);
  uint8_t* parent = At<uint8_t*>(self, 0xE80);
  return parent ? ChainHash(parent) ^ hash : hash;
}

// 0x14166c3e0: close the owned handle (CloseHandle via the IAT).
bool CloseOwnedHandle(void** handle) {
  if (!*handle) return true;
  if (!(*reinterpret_cast<int(__stdcall**)(void*)>(0x14409fef8))(*handle)) return false;
  *handle = nullptr;
  return true;
}

// 0x1402ef7c0: construct an IString from a C string.
void* ConstructIString(uint8_t* self, const char* text) {
  At<uint64_t>(self, 0) = 0x142049b50;
  At<uint64_t>(self, 8) = 0x143e09641;
  At<uint64_t>(self, 0x10) = 0;
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self, text);
  return self;
}

// 0x141603220: Crypto::Prng destructor.
void PrngDestructor(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1424b8d00;
  game::Call<void (*)(void*)>(0x141603180)(self + 0x70);
  game::Call<void (*)(void*)>(0x141603180)(self + 0x40);
  game::Call<void (*)(void*)>(0x1415fc540)(self + 8);
}

// 0x1416ce3a0: destructor (vtable 0x1424d0360, members +0x48 / +0x28 / +8).
void Destructor1424d0360(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1424d0360;
  game::Call<void (*)(void*)>(0x14064aa50)(self + 0x48);
  game::Call<void (*)(void*)>(0x14064aa50)(self + 0x28);
  game::Call<void (*)(void*)>(0x14064abf0)(self + 8);
}

namespace {
uint64_t LookupInGlobal142b19b38(uint64_t key) { return game::Call<uint64_t (*)(void*, uint64_t*)>(0x14071ee60)(*reinterpret_cast<void**>(0x142b19b38), &key); }
}  // namespace

// 0x14050f300 / 0x14050c160: look up an id in the global table
// (0x142b19b38); the first through the pointer at +0x3920, the second
// skips the invalid id (0x142b249a0).
uint64_t LookupFrom3920(uint8_t* self) {
  uint64_t* source = At<uint64_t*>(self, 0x3920);
  return source ? LookupInGlobal142b19b38(*source) : 0;
}
uint64_t LookupFrom38a8(uint8_t* self) {
  uint64_t id = At<uint64_t>(self, 0x38A8);
  return id == *reinterpret_cast<uint64_t*>(0x142b249a0) ? 0 : LookupInGlobal142b19b38(id);
}

// 0x140629770: wrap (data, length) in a byte reader and pass it on.
uint64_t ReadFromBuffer(void* a, void* b, uint8_t* data, int length) {
  struct Reader {
    uint8_t* data;
    int length;
    uint8_t* cursor;
    uint8_t* end;
    uint16_t flags;
  } reader{data, length, data, data + length, 0};
  static_assert(offsetof(Reader, cursor) == 0x10 && offsetof(Reader, flags) == 0x20);
  return game::Call<uint64_t (*)(void*, void*, Reader*)>(0x140629640)(a, b, &reader);
}

// 0x1403f88b0: now (low 32 bits) plus the offset at +0x3B9DC.
int* NowPlusOffset(uint8_t* self, int* out) {
  *out = static_cast<int>(TimeNow()) + At<int>(self, 0x3B9DC);
  return out;
}

// 0x140aa6360: constructor (vtable 0x14206b290).
void* Construct14206b290(uint8_t* self) {
  At<uint64_t>(self, 8) = 0;
  At<uint64_t>(self, 0x10) = 0;
  At<uint64_t>(self, 0) = 0x14206b290;
  At<uint64_t>(self, 0x80) = 0;
  At<uint64_t>(self, 0x88) = 0;
  game::Call<void (*)(void*)>(0x140aa67d0)(self);
  return self;
}

// 0x140520c80: slot 198 with (0x142b241e8, 0.0), then slot 196 with
// 0x142b241e0.
uint64_t ApplyGlobalSettings520c80(void* self) {
  Virtual(self, 198, *reinterpret_cast<uint64_t*>(0x142b241e8), 0.0f);
  return Virtual<uint64_t>(self, 196, *reinterpret_cast<uint64_t*>(0x142b241e0));
}

// 0x140638aa0: set the mode (+0x180), enable the sub-system (+0xADA0)
// when non-zero, refresh +0xAD58.
uint64_t SetMode180(uint8_t* self, int mode) {
  At<int>(self, 0x180) = mode;
  game::Call<void (*)(void*, bool)>(0x140bc2de0)(self + 0xADA0, mode != 0);
  return game::Call<uint64_t (*)(void*)>(0x140bdc640)(At<void*>(self, 0xAD58));
}

// 0x14063d680: refresh the owner (+8) and mark its current entry state 5.
void RefreshAndMarkState5(uint8_t* self) {
  game::Call<void (*)(void*)>(0x14063b9a0)(At<void*>(self, 8));
  if (game::Call<uint8_t* (*)(void*)>(0x14063bcb0)(At<void*>(self, 8)))
    At<int>(game::Call<uint8_t* (*)(void*)>(0x14063bcb0)(At<void*>(self, 8)), 0x54) = 5;
}

// 0x14071e880: read object +0x6D0 under the lock.
uint64_t LockedRead6d0(void* lock, uint8_t* object) {
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  uint64_t value = At<uint64_t>(object, 0x6D0);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  return value;
}

// 0x140aae950: reset the ini at +0xEA0 and reload it from +0x2BD8.
uint64_t ReloadIniEa0(uint8_t* self) {
  game::Call<void (*)(void*, bool)>(0x1403322c0)(self + 0xEA0, true);
  return game::Call<uint64_t (*)(void*, void*)>(0x140334100)(self + 0xEA0, At<void*>(self, 0x2BD8));
}

// 0x140cf3e60: get (optionally create) the cached object at +0x70.
void* GetOrCreate70(uint8_t* self, bool create) {
  if (At<void*>(self, 0x70) || !create) return At<void*>(self, 0x70);
  void* created = game::Call<void* (*)(void*, void*)>(0x140cfa5a0)(*reinterpret_cast<void**>(0x142b19c28), At<void*>(self, 8));
  At<void*>(self, 0x70) = created;
  return created;
}

// 0x141ec21a0: when loaded (+0xC8), fetch the row (+0x140) and stamp it
// with +0x138.
uint8_t* FetchRowStamped(uint8_t* self) {
  if (!At<void*>(self, 0xC8)) return nullptr;
  uint8_t* row = game::Call<uint8_t* (*)(void*)>(0x141ec20b0)(At<void*>(self, 0x140));
  At<uint64_t>(row, 0x28) = At<uint64_t>(self, 0x138);
  return row;
}

// 0x141604510: key-size dispatch: sizes 0x10 / 0x18 / 0x20 clear the flag
// (+0x158), anything else (or 0x28) sets it to -2; then 0x1416002b0.
uint64_t SetKeySize(void* a, int bytes, uint8_t* state) {
  At<int>(state, 0x158) = 0;
  if (((bytes - 0x10) & ~0x18) != 0 || bytes == 0x28) At<int>(state, 0x158) = -2;
  game::Call<void (*)(void*, int, void*)>(0x1416002b0)(a, bytes, state + 0x30);
  return 0;
}

// 0x14039d910: constructor (type 0xB1, vtable 0x142063e10).
void* Construct142063e10(uint8_t* self) {
  At<int>(self, 8) = 0xB1;
  At<uint64_t>(self, 0) = 0x142063e10;
  game::Call<void (*)(void*)>(0x1416cb020)(self + 0x10);
  return self;
}

// 0x140487980: allocate and construct the 0x280808-byte object.
void** CreateLarge280808(void** out) {
  void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x280808);
  *out = memory ? game::Call<void* (*)(void*)>(0x1404879c0)(memory) : nullptr;
  return out;
}

// 0x1415021d0: Audio manager PostEvent(gameObject, eventId, delay).
bool AudioPostEvent(uint8_t* self, uint64_t gameObject, uint32_t eventId, float delay) {
  if (!At<bool>(self, 0x18)) return false;
  return game::Call<int (*)(uint32_t, float, uint64_t, uint64_t, int, bool)>(0x14150a8a0)(eventId, delay, gameObject, 0, 4, false) == 1;
}

// 0x141868440: stop the active slot (index +0x48 of 5 at +8) if running.
void StopActiveSlot(uint8_t* self) {
  if (!At<bool>(self, 0x4C)) return;
  int index = At<int>(self, 0x48);
  if (index < 5) {
    if (void* slot = At<void*>(self, 8 + static_cast<int64_t>(index) * 8)) {
      Virtual(slot, 1);
      At<bool>(self, 0x4C) = false;
    }
  }
  At<int>(self, 0x48) = 0x7FFFFFFF;
  At<bool>(self, 0x4C) = false;
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
REBUILD_FUNCTION(Log_Level1, 0x1403243b0, LogLevel1);
REBUILD_FUNCTION(Log_Level5, 0x140428ce0, LogLevel5);
REBUILD_FUNCTION(CopySharedAt51a8, 0x140927e90, CopySharedAt51a8);
REBUILD_FUNCTION(SetNameAndId, 0x140736260, SetNameAndId);
REBUILD_FUNCTION(Strtok, 0x141ec5ffc, Strtok);
REBUILD_FUNCTION(LockedRead9f0, 0x14071e7a0, LockedRead9f0);
REBUILD_FUNCTION(ChainHash, 0x140332e60, ChainHash);
REBUILD_FUNCTION(CloseOwnedHandle, 0x14166c3e0, CloseOwnedHandle);
REBUILD_FUNCTION(ConstructIString, 0x1402ef7c0, ConstructIString);
REBUILD_FUNCTION(Crypto_Prng_Destructor, 0x141603220, PrngDestructor);
REBUILD_FUNCTION(Destructor_1424d0360, 0x1416ce3a0, Destructor1424d0360);
REBUILD_FUNCTION(LookupFrom3920, 0x14050f300, LookupFrom3920);
REBUILD_FUNCTION(LookupFrom38a8, 0x14050c160, LookupFrom38a8);
REBUILD_FUNCTION(ReadFromBuffer, 0x140629770, ReadFromBuffer);
REBUILD_FUNCTION(NowPlusOffset, 0x1403f88b0, NowPlusOffset);
REBUILD_FUNCTION(Construct_14206b290, 0x140aa6360, Construct14206b290);
REBUILD_FUNCTION(ApplyGlobalSettings520c80, 0x140520c80, ApplyGlobalSettings520c80);
REBUILD_FUNCTION(SetMode180, 0x140638aa0, SetMode180);
REBUILD_FUNCTION(RefreshAndMarkState5, 0x14063d680, RefreshAndMarkState5);
REBUILD_FUNCTION(LockedRead6d0, 0x14071e880, LockedRead6d0);
REBUILD_FUNCTION(ReloadIniEa0, 0x140aae950, ReloadIniEa0);
REBUILD_FUNCTION(GetOrCreate70, 0x140cf3e60, GetOrCreate70);
REBUILD_FUNCTION(FetchRowStamped, 0x141ec21a0, FetchRowStamped);
REBUILD_FUNCTION(SetKeySize, 0x141604510, SetKeySize);
REBUILD_FUNCTION(Construct_142063e10, 0x14039d910, Construct142063e10);
REBUILD_FUNCTION(CreateLarge280808, 0x140487980, CreateLarge280808);
REBUILD_FUNCTION(Audio_PostEvent, 0x1415021d0, AudioPostEvent);
REBUILD_FUNCTION(StopActiveSlot, 0x141868440, StopActiveSlot);
