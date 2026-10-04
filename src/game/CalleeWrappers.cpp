// Small non-virtual helpers called from already-rebuilt code: varargs
// forwarders (string format, logging), lazy singletons, forwarding thunks.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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


// 0x14032fa90: the va_list logger: drop messages above the global level
// (0x1429fa7d0) or without a logger (0x142b06c68); null channels use the
// default (0x1429fa5b8); forward to logger slot 1.
void LogV(int level, void* channel, const char* format, va_list args) {
  if (level > *reinterpret_cast<int*>(0x1429fa7d0)) return;
  void* logger = *reinterpret_cast<void**>(0x142b06c68);
  if (!logger) return;
  if (!channel) channel = *reinterpret_cast<void**>(0x1429fa5b8);
  Virtual(logger, 1, level, channel, format, args);
}

// 0x14049c1d0: accept quality levels 5..30 (+0x3B728 -> +0x48), apply
// through 0x140ab6a20 and flag the change (+0x3B730).
bool SetLevel5To30(uint8_t* self, int level) {
  if (static_cast<uint32_t>(level - 5) > 0x19) return false;
  At<int>(At<void*>(self, 0x3B728), 0x48) = level;
  game::Call<void (*)(void*)>(0x140ab6a20)(*reinterpret_cast<void**>(0x142b199f0));
  At<uint8_t>(self, 0x3B730) = 1;
  return true;
}

// 0x1402f57e0: find the target (owner +8 -> +0x80, slot 43) and pass it
// the float through its slot 23.
uint64_t ForwardFloatToTarget(uint8_t* self, float value) {
  void* holder = At<void*>(At<void*>(self, 8), 0x80);
  void* target = Virtual<void*>(holder, 43);
  if (!target) return 0;
  return reinterpret_cast<uint64_t (*)(void*, float)>((*reinterpret_cast<void***>(target))[23])(target, value);
}

// 0x14184e920: destroy and free the 0x3840-byte member at +0x50.
void FreeMember50(uint8_t* self) {
  uint8_t* member = At<uint8_t*>(self, 0x50);
  if (!member) return;
  game::Call<void (*)(void*)>(0x141884960)(member);
  game::Call<void (*)(void*, size_t)>(0x140d0fb84)(member, 0x3840);
  At<void*>(self, 0x50) = nullptr;
}

// 0x140466000: post audio event 0xF920AF90 with the configured delay
// (settings 0x142b199f0 +0x2E9C).
uint64_t PostAudioEventF920AF90Delayed() {
  float delay = At<float>(*reinterpret_cast<void**>(0x142b199f0), 0x2E9C);
  void* manager = game::Call<void* (*)()>(0x141501840)();
  return game::Call<uint64_t (*)(void*, uint64_t, uint32_t, float)>(0x1415021d0)(manager, *reinterpret_cast<uint64_t*>(0x142ab0aa8), 0xF920AF90, delay);
}

// 0x1416d7a60: destructor (vtable 0x1424d1ac8): delete the owned object at
// +0x50, drop to the base vtable 0x1420dbb38.
void Destructor1424d1ac8(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1424d1ac8;
  if (void* owned = At<void*>(self, 0x50)) Virtual(owned, 0, 1);
  At<void*>(self, 0x50) = nullptr;
  At<uint64_t>(self, 0) = 0x1420dbb38;
}

// 0x1403f6ba0: copy the string at +0x3358 into a new IString `out`.
void* CopyString3358(uint8_t* self, uint8_t* out) {
  At<uint64_t>(out, 0) = 0x142049b50;
  At<uint64_t>(out, 0x10) = 0;
  At<uint64_t>(out, 8) = 0x143e09641;
  game::Call<void (*)(void*, void*)>(0x1402bd560)(out, self + 0x3358);
  return out;
}

// 0x1407218c0: under the object's mutex, flag every entry in the list
// (+0x9F0, next +0x6D0) with bit 2 at +0x8CD.
void FlagAllEntries(uint8_t* self) {
  game::Call<void (*)(void*)>(0x14032f270)(self);
  for (uint8_t* entry = At<uint8_t*>(self, 0x9F0); entry; entry = At<uint8_t*>(entry, 0x6D0)) At<uint8_t>(entry, 0x8CD) |= 2;
  game::Call<void (*)(void*)>(0x14032f360)(self);
}

// 0x14133e6c0: set the entry's byte flag (entry from 0x14133f760(self, 1,
// 2), flag at *(+0x100)) and refresh it when it changed.
bool SetEntryFlag(void* self, uint8_t value) {
  uint8_t* entry = game::Call<uint8_t* (*)(void*, bool, int)>(0x14133f760)(self, true, 2);
  if (!entry) return false;
  uint8_t* flag = At<uint8_t*>(entry, 0x100);
  if (*flag == value) return false;
  *flag = value;
  game::Call<void (*)(void*)>(0x14133db80)(entry);
  return true;
}

// 0x14032e120: fetch a 0x800-character wide path through the IAT
// (0x1440a0078) and convert it into `out` (0x14033a0c0).
bool GetWidePath(void* out) {
  uint16_t path[0x800];
  path[0] = 0;
  (*reinterpret_cast<uint32_t(__stdcall**)(uint16_t*, uint32_t)>(0x1440a0078))(path, 0x800);
  game::Call<void (*)(uint16_t*, void*)>(0x14033a0c0)(path, out);
  return true;
}

// 0x140637490: if the id is in the 32-bucket map at +0xB4F0, handle it
// (0x140443f30).
bool HandleIfKnown(uint8_t* self, const int* id) {
  uint8_t* map = self + 0xB4F0;
  for (uint8_t* node = At<uint8_t*>(map, 0x28 + (*id & 0x1F) * 8); node; node = At<uint8_t*>(node, 0x20)) {
    if (At<int>(node, 0x18) == *id) {
      game::Call<void (*)(void*, void*)>(0x140443f30)(map, node);
      return true;
    }
  }
  return false;
}

// 0x141ec2150: when loaded (+0xC8), look up (a, b) in +0x140 keyed by
// +0x138.
uint64_t LookupLoaded(uint8_t* self, uint64_t a, int b) {
  if (!At<void*>(self, 0xC8)) return 0;
  return game::Call<uint64_t (*)(void*, void*, uint64_t*, int*)>(0x14033b440)(At<void*>(self, 0x140), self + 0x138, &a, &b);
}

namespace {
struct TypedEvent {
  uint64_t vtable;
  int type;
};
}  // namespace

// 0x14079fbc0 / 0x14079c160: dispatch a stack event (type 0xB2 / 0x98) to
// the global dispatcher (0x142b19b98 -> +8).
uint64_t DispatchEventB2() {
  TypedEvent event{0x1421079d8, 0xB2};
  return game::Call<uint64_t (*)(void*, TypedEvent*, int, bool)>(0x14079f620)(At<void*>(*reinterpret_cast<void**>(0x142b19b98), 8), &event, 0, true);
}
uint64_t DispatchEvent98() {
  TypedEvent event{0x1421069b0, 0x98};
  return game::Call<uint64_t (*)(void*, TypedEvent*, int, bool)>(0x14079b5e0)(At<void*>(*reinterpret_cast<void**>(0x142b19b98), 8), &event, 1, true);
}

// 0x140346f80: begin / end the +0x310 section around a counter bump
// (+0x378).
void BumpCounter378(uint8_t* self) {
  game::Call<void (*)(void*)>(0x14034a560)(self + 0x310);
  ++At<int64_t>(self, 0x378);
  game::Call<void (*)(void*)>(0x14034a8b0)(self + 0x310);
}

// 0x14046d7b0: variant set-int: destroy the held object for types 3 / 7 /
// 8 / 9, then store the int and type 1.
void VariantSetInt(uint8_t* self, int value) {
  int type = At<int>(self, 0);
  if (type == 3 || type == 7 || type == 8 || type == 9) Virtual(self + 8, 0, 0);
  At<int>(self, 8) = value;
  At<int>(self, 0) = 1;
}

// 0x140aa9460: stop the sound (if playing) and reset the emitter.
bool StopEmitter(uint8_t* self) {
  void* manager = game::Call<void* (*)()>(0x141501840)();
  if (At<uint8_t>(self, 0)) game::Call<void (*)(void*, void*)>(0x141502400)(manager, manager);
  At<uint8_t>(self, 0) = 0;
  game::Call<void (*)(void*)>(0x140aa94b0)(self);
  game::Call<void (*)(void*)>(0x141502270)(manager);
  return true;
}

// 0x140483490: initialize (name, value) with default settings.
void InitNamedSetting(uint8_t* self, const char* name, const char* value) {
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self, name);
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self + 0x18, value);
  At<int>(self, 0x40) = 1;
  At<uint8_t>(self, 0x44) = 1;
  At<int>(self, 0x34) = 8;
  At<int>(self, 0x48) = 1;
}

// 0x14040d960: on the owning thread (+0x387F0), true when the current
// selection of +0x38AC8 resolves to nothing.
bool IsOwnerThreadAndEmpty(uint8_t* self) {
  uint64_t thread = (*reinterpret_cast<uint64_t(__stdcall**)()>(0x1440a0470))();
  if (thread != At<uint64_t>(self, 0x387F0)) return false;
  int key = game::Call<int (*)(void*)>(0x140cf3dc0)(At<void*>(self, 0x38AC8));
  return game::Call<void* (*)(void*, int)>(0x140cf41c0)(At<void*>(self, 0x38AC8), key) == nullptr;
}

// 0x1403b4810: element `index` (0xA8 bytes each), growing the array to
// index + 1 when it is past the end (the new slot's first int cleared).
uint8_t* ElementAtGrowA8(uint8_t* self, int index) {
  int count = At<int>(self, 0x10);
  if (index >= count && count < index + 1) {
    if (int* slot = game::Call<int* (*)(void*, int)>(0x140418710)(self, index)) *slot = 0;
  }
  return At<uint8_t*>(self, 8) + static_cast<int64_t>(index) * 0xA8;
}

// 0x14050f340: look up +0x3920's id and call the result's slot 83.
uint64_t LookupFrom3920Slot83(uint8_t* self) {
  uint64_t* source = At<uint64_t*>(self, 0x3920);
  if (!source) return 0;
  uint64_t key = *source;
  void* found = game::Call<void* (*)(void*, uint64_t*)>(0x14071ee60)(*reinterpret_cast<void**>(0x142b19b38), &key);
  return found ? Virtual<uint64_t>(found, 83) : 0;
}

// 0x14039a4e0: constructor (type 0xAE, vtable 0x142064398).
void* Construct142064398(uint8_t* self) {
  At<uint64_t>(self, 8) = 0xAE;
  At<uint64_t>(self, 0) = 0x142064398;
  At<uint64_t>(self, 0x10) = *reinterpret_cast<uint64_t*>(0x142b181f8);
  game::Call<void (*)(void*)>(0x14039d5a0)(self + 0x20);
  return self;
}

// 0x140558310: reset the two +0x2050 / +0x2090 buffers twice each.
uint64_t ResetBuffers2050(uint8_t* self) {
  game::Call<void (*)(void*)>(0x140556cc0)(self + 0x2050);
  game::Call<void (*)(void*)>(0x140556cc0)(self + 0x2050);
  game::Call<void (*)(void*)>(0x140556cc0)(self + 0x2090);
  return game::Call<uint64_t (*)(void*)>(0x140556cc0)(self + 0x2090);
}


// 0x141502400: audio manager: if ready (+0x18), run 0x14150bd50 on the
// game object; true when it returns 1.
bool AudioRunOnObject(uint8_t* self, void* gameObject) {
  if (!At<bool>(self, 0x18)) return false;
  return game::Call<int (*)(void*)>(0x14150bd50)(gameObject) == 1;
}

// 0x141501840: the audio manager singleton (0x143c76c60), created on first
// use as a zeroed 0x20-byte block.
void* AudioManagerInstance() {
  void*& instance = *reinterpret_cast<void**>(0x143c76c60);
  if (instance) return instance;
  auto* created = game::Call<uint8_t* (*)(size_t)>(0x1402fc0f0)(0x20);
  if (created) {
    At<uint64_t>(created, 0) = 0;
    At<uint64_t>(created, 8) = 0;
    At<uint64_t>(created, 0x10) = 0;
    At<uint8_t>(created, 0x18) = 0;
  }
  instance = created;
  return created;
}

// 0x14049c180: accept 0 or values 0x180..0xDAC (+0x3B728 -> +0x4C), apply
// through 0x140ab6ab0 and flag the change.
bool SetValue180ToDac(uint8_t* self, int value) {
  if (value != 0 && static_cast<uint32_t>(value - 0x180) > 0xC2C) return false;
  At<int>(At<void*>(self, 0x3B728), 0x4C) = value;
  game::Call<void (*)(void*)>(0x140ab6ab0)(*reinterpret_cast<void**>(0x142b199f0));
  At<uint8_t>(self, 0x3B730) = 1;
  return true;
}

namespace {
// Variant: destroy the held object for types 3 / 7 / 8 / 9 before
// overwriting.
void VariantClearHeld(uint8_t* self) {
  int type = At<int>(self, 0);
  if (type == 3 || type == 7 || type == 8 || type == 9) Virtual(self + 8, 0, 0);
}
}  // namespace

// 0x14046d940 / 0x14046be30 / 0x14046d800: variant set bool (type 6),
// pointer (type 4), double (type 2).
void VariantSetBool(uint8_t* self, uint8_t value) {
  VariantClearHeld(self);
  At<uint8_t>(self, 8) = value;
  At<int>(self, 0) = 6;
}
void VariantSetPointer(uint8_t* self, uint64_t value) {
  VariantClearHeld(self);
  At<uint64_t>(self, 8) = value;
  At<int>(self, 0) = 4;
}
void VariantSetDouble(uint8_t* self, double value) {
  VariantClearHeld(self);
  At<double>(self, 8) = value;
  At<int>(self, 0) = 2;
}

// 0x1406297b0: insert the id into the 32-bucket map at +0xB4F0 unless
// present (0x14061f450).
bool InsertIfMissing(uint8_t* self, const int* id) {
  uint8_t* map = self + 0xB4F0;
  for (uint8_t* node = At<uint8_t*>(map, 0x28 + (*id & 0x1F) * 8); node; node = At<uint8_t*>(node, 0x20))
    if (At<int>(node, 0x18) == *id) return false;
  game::Call<void (*)(void*, const int*, const int*)>(0x14061f450)(map, id, id);
  return true;
}

// 0x1404cf240: start (timestamp +0x70) or clear the timer with a value.
void StartTimer6c(uint8_t* self, int value) {
  if (value == 0) {
    At<uint8_t>(self, 0x78) = 0;
    At<int>(self, 0x6C) = 0;
    return;
  }
  At<int64_t>(self, 0x70) = TimeNow();
  At<uint8_t>(self, 0x78) = 1;
  At<int>(self, 0x6C) = value;
}

// 0x14079c110: reset +0x80 (0x1404539a0), copy value +0x10 into it
// (0x14079b520), then slot 5.
uint64_t ResetCopyThenSlot5(uint8_t* self, uint8_t* value) {
  game::Call<void (*)(void*)>(0x1404539a0)(self + 0x80);
  game::Call<void (*)(void*, void*)>(0x14079b520)(self + 0x80, value + 0x10);
  return Virtual<uint64_t>(self, 5);
}

// 0x140631cd0: find the owner (0x14062e380), its entry (a4, a5) and act on
// it (0x140b3d880).
uint64_t FindEntryAndApply(void* a, void* b, void* c, int key, int sub) {
  uint8_t* owner = game::Call<uint8_t* (*)(void*, void*, void*, int)>(0x14062e380)(a, b, c, key);
  if (!owner) return 0;
  void* entry = game::Call<void* (*)(void*, int, int)>(0x141762420)(owner + 0xA8, key, sub);
  if (!entry) return 0;
  return game::Call<uint64_t (*)(void*, void*)>(0x140b3d880)(owner, entry);
}

// 0x14049b6a0: stamp the times (+0x18 now, +0x20 secondary clock), and
// unless paused (+0x14) the deadline (+0x28 = now + +0x2C).
void StampTimes(uint8_t* self) {
  int64_t scratch;
  At<int64_t>(self, 0x18) = *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&scratch);
  At<int64_t>(self, 0x20) = *game::Call<int64_t* (*)(int64_t*)>(0x14032fe90)(&scratch);
  if (At<uint8_t>(self, 0x14)) return;
  At<int>(self, 0x28) = static_cast<int>(*game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&scratch)) + At<int>(self, 0x2C);
}

// 0x1414585d0: set the global toggle (0x142aae24a) and apply it to every
// registered entry (list 0x142aae268, next +0x18).
void SetGlobalToggle(uint8_t enabled) {
  *reinterpret_cast<uint8_t*>(0x142aae24a) = enabled;
  uint8_t flag = enabled;
  for (uint8_t* entry = *reinterpret_cast<uint8_t**>(0x142aae268); entry; entry = At<uint8_t*>(entry, 0x18)) {
    if (!flag) {
      void* target = At<void*>(At<void*>(entry, 0), 0x48);
      Virtual(target, 7, 0);
    }
    At<uint32_t>(entry, 0x14) |= 0x10000000;
    flag = *reinterpret_cast<uint8_t*>(0x142aae24a);
  }
}

// 0x14050dd30: atan2-style result (0x140d55d44) of the transform's +0x20
// and +0x28 components.
float TransformAngle(uint8_t* self) {
  void* transform = Virtual<void*>(self + 0x20, 22);
  float y = At<float>(Virtual<uint8_t*>(transform, 3), 0x28);
  float x = At<float>(Virtual<uint8_t*>(transform, 3), 0x20);
  return game::Call<float (*)(float, float)>(0x140d55d44)(x, y);
}

// 0x140ab7b20: reload the ini at +0x1D38 from +0x2BF0 and merge +8 in.
uint64_t ReloadIni1d38(uint8_t* self) {
  game::Call<void (*)(void*, bool)>(0x1403322c0)(self + 0x1D38, true);
  game::Call<void (*)(void*, void*)>(0x140334100)(self + 0x1D38, At<void*>(self, 0x2BF0));
  return game::Call<uint64_t (*)(void*, void*, int)>(0x140335530)(self + 0x1D38, self + 8, 0);
}

// 0x14047acd0: two slot-21 calls on +0x3D3C0 with constant string pairs.
uint64_t ApplyStringPairs3d3c0(uint8_t* self) {
  auto str = [](uint64_t address) { return reinterpret_cast<const char*>(address); };
  Virtual(At<void*>(self, 0x3D3C0), 21, str(0x142072be4), str(0x142072be0));
  return Virtual<uint64_t>(At<void*>(self, 0x3D3C0), 21, str(0x142072be8), str(0x142072be4));
}

// 0x141845390: constructor: base (0x141845160), flag +0x8318, three
// vtables (+0, +8, +0x48).
void* Construct14250ce68(uint8_t* self, void* a, void* b, uint8_t flag) {
  game::Call<void (*)(void*, void*, void*, uint8_t)>(0x141845160)(self, a, b, flag);
  At<uint8_t>(self, 0x8318) = flag;
  At<uint64_t>(self, 0) = 0x14250ce68;
  At<uint64_t>(self, 8) = 0x14250ceb0;
  At<uint64_t>(self, 0x48) = 0x14250ced8;
  return self;
}

// 0x14039c860: copy a packed (31-bit value + flag bit) pair; values
// below the flag bit go through 0x1402ee1f0.
uint32_t* CopyPacked31(uint32_t* self, const uint32_t* source) {
  self[1] = 0;
  self[0] = 0x80000000u;
  self[1] = source[1];
  uint32_t raw = source[0];
  int32_t value;
  if (raw >= 0x80000000u)
    value = static_cast<int32_t>(raw << 1) >> 1;
  else
    value = game::Call<int32_t (*)(int32_t)>(0x1402ee1f0)(static_cast<int32_t>(raw << 1) >> 1);
  self[0] = (self[0] & 0x80000000u) | (static_cast<uint32_t>(value) & 0x7FFFFFFFu);
  return self;
}

// 0x14165c630: SoeGems::LoggingHeader destructor.
void LoggingHeaderDestructor(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1424c37d8;
  auto* text = reinterpret_cast<uint8_t*>(self + 8);
  At<uint64_t>(text, 0) = 0x142049b50;
  if (At<int>(text, 0x14) > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(At<uint8_t*>(text, 8) - 4), -1) - 1 <= 0) Virtual(text, 2);
}

// 0x14046d1d0: copy three strings and two settings, mark dirty (+0x50).
void CopyThreeStrings(uint8_t* self, uint8_t* other) {
  using AssignFn = void (*)(void*, void*);
  game::Call<AssignFn>(0x1402bd560)(self, other);
  game::Call<AssignFn>(0x1402bd560)(self + 0x18, other + 0x18);
  game::Call<AssignFn>(0x1402bd560)(self + 0x30, other + 0x30);
  At<int>(self, 0x48) = At<int>(other, 0x48);
  At<uint8_t>(self, 0x4C) = At<uint8_t>(other, 0x4C);
  At<uint8_t>(self, 0x50) = 1;
}

// 0x14039d9f0: constructor (type 0xE3, vtable 0x1420663b0).
void* Construct1420663b0(uint8_t* self) {
  At<int>(self, 8) = 0xE3;
  At<uint64_t>(self, 0) = 0x1420663b0;
  At<int>(self, 0x10) = 5;
  At<int>(self, 0x30) = 0;
  At<uint64_t>(self, 0x20) = 0;
  At<uint64_t>(self, 0x28) = 0;
  At<uint64_t>(self, 0x18) = 0x142066390;
  game::Call<void (*)(void*)>(0x140394720)(self + 0x38);
  return self;
}

// 0x140428fc0: look up the id in +0x38860 and return its +0x1D0 value (or
// the invalid id 0x142b181f8).
uint64_t* LookupValue1d0(uint8_t* self, uint64_t* out, const int* id) {
  int key = *id;
  uint8_t* found = game::Call<uint8_t* (*)(void*, int*)>(0x14071f100)(At<void*>(self, 0x38860), &key);
  *out = found ? At<uint64_t>(found, 0x1D0) : *reinterpret_cast<uint64_t*>(0x142b181f8);
  return out;
}

// 0x14062bbd0: update +0x1F8, slot 17 on +0xAD70 and +0xAD78, then
// 0x140b8d900(+0xAFA0, +0x2D0).
uint64_t UpdateViews(uint8_t* self) {
  game::Call<void (*)(void*)>(0x14062bee0)(self + 0x1F8);
  Virtual(At<void*>(self, 0xAD70), 17);
  Virtual(At<void*>(self, 0xAD78), 17);
  return game::Call<uint64_t (*)(void*, int)>(0x140b8d900)(self + 0xAFA0, At<int>(self, 0x2D0));
}

// 0x1404685a0: store up to 8 bytes (zero padded) and their count (+8).
void StoreSmallBytes(uint8_t* self, const uint8_t* source, uint32_t count) {
  At<uint32_t>(self, 8) = count;
  if (count >= 8) {
    At<uint64_t>(self, 0) = *reinterpret_cast<const uint64_t*>(source);
    return;
  }
  int64_t length = static_cast<int32_t>(count);
  game::Call<void* (*)(void*, const void*, size_t)>(0x140d11e20)(self, source, length);
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + length, 0, 8 - length);
}


// 0x14032fe90: wall-clock seconds cache: refresh the cached time()
// (0x142b06c98) when the tick (IAT 0x1440a0090) moved by more than 10 or
// wrapped, then add the configured offset (0x142b06c70).
int64_t* WallClockNow(int64_t* out) {
  uint32_t tick = (*reinterpret_cast<uint32_t(__stdcall**)()>(0x1440a0090))();
  uint32_t& last = *reinterpret_cast<uint32_t*>(0x142b06ca0);
  if (tick - last > 10 || tick < last) {
    last = tick;
    game::Call<int64_t (*)(int64_t*)>(0x140d43940)(reinterpret_cast<int64_t*>(0x142b06c98));
  }
  *out = static_cast<int64_t>(*reinterpret_cast<int*>(0x142b06c70)) + *reinterpret_cast<int64_t*>(0x142b06c98);
  return out;
}

// 0x1403160f0: create the 0x93A0-byte object, publish it (0x142b06928) and
// initialize it with the argument.
uint64_t CreateAndInit93a0(void** out, void* argument) {
  void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x93A0);
  *out = memory ? game::Call<void* (*)(void*)>(0x1403152b0)(memory) : nullptr;
  *reinterpret_cast<void**>(0x142b06928) = *out;
  return game::Call<uint64_t (*)(void*, void**, void*)>(0x140316140)(*out, out, argument);
}

// 0x14150bd50: queue a command (0xC) for the game object on the audio
// command queue (0x143c76d30); 2 for a null object.
int AudioQueueObjectCommand(void* gameObject) {
  if (!gameObject) return 2;
  uint16_t id = game::Call<uint16_t (*)()>(0x141540460)();
  uint8_t* entry = game::Call<uint8_t* (*)(void*, int, uint16_t)>(0x14153f910)(*reinterpret_cast<void**>(0x143c76d30), 0xC, id);
  At<void*>(entry, 8) = gameObject;
  _InterlockedDecrement(&At<volatile long>(*reinterpret_cast<void**>(0x143c76d30), 0xAC));
  return 1;
}

// 0x140cff570: append self to its owner's list (head +0x28, tail +0x30,
// count +0x38; links prev +0x50 / next +0x58) unless already linked.
void AppendToOwnerList(uint8_t* self) {
  uint8_t* list = game::Call<uint8_t* (*)(void*)>(0x1403f62a0)(self);
  if (At<void*>(self, 0x50) || At<uint8_t*>(list, 0x28) == self) return;
  uint8_t* tail = At<uint8_t*>(list, 0x30);
  At<uint8_t*>(self, 0x50) = tail;
  if (!tail) {
    ++At<int>(list, 0x38);
    At<uint8_t*>(list, 0x28) = self;
    At<uint8_t*>(list, 0x30) = self;
    return;
  }
  At<uint8_t*>(tail, 0x58) = self;
  ++At<int>(list, 0x38);
  At<uint8_t*>(list, 0x30) = self;
}

// 0x1403c2100: scalar deleting destructor (0x370 bytes).
void* DeletingDestructor370(uint8_t* self, unsigned flags) {
  game::Call<void (*)(void*)>(0x1403e32f0)(self);
  game::Call<void (*)(void*)>(0x1416da170)(self + 0x160);
  At<uint64_t>(self, 0x148) = 0x142064ad8;
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, 0x370);
  return self;
}

// 0x141e62cb0: SpeedTree file pool: release slot `index` (0x124-byte slots
// at 0x143e03e60); logs an error for a free slot.
bool SpeedTreeReleasePoolSlot(int index) {
  uint8_t* slot = reinterpret_cast<uint8_t*>(0x143e03e60) + static_cast<int64_t>(index) * 0x124;
  if (!At<uint8_t>(slot, 0x120)) {
    game::Call<void (*)(const char*)>(0x141e626e0)(reinterpret_cast<const char*>(0x1425a1628));
    return false;
  }
  At<uint8_t>(slot, 0x120) = 0;
  At<uint64_t>(slot, 0x18) = 0;
  At<uint8_t>(slot, 0x20) = 0;
  return true;
}

// 0x140344910: set the flag at +0x190 inside the +0x330 section.
void SetFlag190Locked(uint8_t* self, uint8_t value) {
  game::Call<void (*)(void*)>(0x14034a560)(self + 0x330);
  At<uint8_t>(self, 0x190) = value;
  game::Call<void (*)(void*)>(0x14034a8b0)(self + 0x330);
}

// 0x1403531d0: destructor (vtable 0x142046860) deleting the owned object
// at +8 (the check is repeated, as in the original).
void Destructor142046860(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x142046860;
  for (int pass = 0; pass < 2; ++pass) {
    if (void* owned = At<void*>(self, 8)) {
      Virtual(owned, 0, 1);
      At<void*>(self, 8) = nullptr;
    }
  }
}

// 0x1402f3140: release the two owned objects (+8 through slot 2, +0x18
// whose interface sits at +8), then 0x1402f6d80.
uint64_t ReleaseOwned8And18(uint8_t* self) {
  if (void* owned = At<void*>(self, 8)) {
    Virtual(owned, 2, 1);
    At<void*>(self, 8) = nullptr;
  }
  if (uint8_t* other = At<uint8_t*>(self, 0x18)) {
    Virtual(other + 8, 0, 1);
    At<void*>(self, 0x18) = nullptr;
  }
  return game::Call<uint64_t (*)(void*)>(0x1402f6d80)(self);
}

// 0x140474ab0: build an IString `out` from `source` (0x1402eea50).
void* MakeStringFrom(void* source, uint8_t* out) {
  At<uint64_t>(out, 0) = 0x142049b50;
  At<uint64_t>(out, 8) = 0x143e09641;
  At<uint64_t>(out, 0x10) = 0;
  game::Call<void (*)(void*, void*)>(0x1402eea50)(source, out);
  return out;
}

// 0x14032d190: SoeGems::InfiniteLoopMonitor Kick(enable): bind to the
// first calling thread (+0xE8); on that thread set kicked (+0xDC) and swap
// the enabled flag (+0xDD), returning the previous one.
uint8_t InfiniteLoopMonitorKick(uint8_t* self, uint8_t enable) {
  uint64_t thread = game::Call<uint32_t (*)()>(0x14032e7b0)();
  if (At<uint64_t>(self, 0xE8) == 0) At<uint64_t>(self, 0xE8) = thread;
  if (thread != At<uint64_t>(self, 0xE8)) return enable;
  _InterlockedExchange8(&At<volatile char>(self, 0xDC), 1);
  return static_cast<uint8_t>(_InterlockedExchange8(&At<volatile char>(self, 0xDD), static_cast<char>(enable)));
}

// 0x140b8d900: apply the value to every entry (+0x78, next +8) and the
// main target (+0x90).
uint64_t ApplyToAll78(uint8_t* self, int value) {
  for (void** node = At<void**>(self, 0x78); node; node = static_cast<void**>(node[1])) game::Call<void (*)(void*, int)>(0x140cab520)(node[0], value);
  return game::Call<uint64_t (*)(void*, int)>(0x140cab500)(At<void*>(self, 0x90), value);
}

namespace {
bool ElapsedAtMost(int64_t start, float seconds) {
  int64_t elapsed = TimeNow() - start;
  int clamped = elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
  return seconds >= static_cast<float>(clamped) * *reinterpret_cast<float*>(0x142047918);
}
}  // namespace

// 0x140428200 / 0x140428130: has less than `seconds` passed since the
// stamp at +0xD8 / +0xD0 (ms scaled by 0x142047918)?
bool WithinSecondsD8(uint8_t* self, float seconds) { return ElapsedAtMost(At<int64_t>(self, 0xD8), seconds); }
bool WithinSecondsD0(uint8_t* self, float seconds) { return ElapsedAtMost(At<int64_t>(self, 0xD0), seconds); }

// 0x141884960: destructor (vtables 0x142512ab8 -> 0x142512aa0).
void Destructor142512ab8(uint8_t* self) {
  game::Call<void (*)(void*)>(0x141884b50)(self);
  At<uint64_t>(self, 0) = 0x142512ab8;
  game::Call<void (*)(void*)>(0x141885270)(self);
  game::Call<void (*)(void*)>(0x1403a77f0)(self + 0x818);
  At<uint64_t>(self, 0) = 0x142512aa0;
  game::Call<void (*)(void*)>(0x141885270)(self);
}

// 0x140611910: resolve the key through +0x12418 (fallback: the key) and
// return the entry's byte at +0x30 from +0x290 (0 when missing).
uint64_t ResolveEntryByte(uint8_t* self, void* key) {
  void* resolved = game::Call<void* (*)(void*, void*)>(0x14060b850)(self + 0x12418, key);
  if (!resolved) resolved = key;
  uint8_t* entry = game::Call<uint8_t* (*)(void*, void*)>(0x14060b9f0)(self + 0x290, resolved);
  return entry ? At<uint8_t>(entry, 0x30) : 0;
}

// Destructors that release one IString member and drop to the base
// vtable 0x1420633d0 (0x1403ae270 / 0x1403ae490 / 0x1403ae580 /
// 0x1403b0fb0 / 0x1403b0ed0 / 0x1403b0540).
template <size_t Offset>
void StringMemberDestructor1420633d0(uint8_t* self) {
  uint8_t* text = self + Offset;
  At<uint64_t>(text, 0) = 0x142049b50;
  if (At<int>(text, 0x14) > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(At<uint8_t*>(text, 8) - 4), -1) - 1 <= 0) Virtual(text, 2);
  At<uint64_t>(self, 0) = 0x1420633d0;
}


namespace {
void FreeStorage(void* data, int threadFlag) {
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    game::Call<void (*)(void*)>(0x1402fc170)(data);
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, threadFlag);
}
void InitIString(uint8_t* text) {
  At<uint64_t>(text, 0) = 0x142049b50;
  At<uint64_t>(text, 8) = 0x143e09641;
  At<uint64_t>(text, 0x10) = 0;
}
}  // namespace

// 0x1403322c0: ini file Clear(markModified): when it has sections, clear
// them and update the modified flag (+0xE90).
void IniClear(uint8_t* self, bool markModified) {
  if (At<int>(self, 0x18) <= 0) return;
  game::Call<void (*)(void*)>(0x140334b80)(self);
  At<uint8_t>(self, 0xE90) = (At<uint8_t>(self, 0xE90) || markModified) ? 1 : 0;
}

// 0x14070b5d0: look up the key (0x14070c4f0) and forward to the entry
// (0x140706660).
uint64_t LookupAndForward(void* self, const uint64_t* key, void* argument, uint8_t flag) {
  uint64_t copy = *key;
  void* entry = game::Call<void* (*)(void*, uint64_t*)>(0x14070c4f0)(self, &copy);
  if (!entry) return 0;
  return game::Call<uint64_t (*)(void*, void*, void*, uint8_t)>(0x140706660)(entry, self, argument, flag);
}

// 0x140309e00: printf to stdout (__stdio_common_vfprintf).
int PrintToStdout(const char* format, ...) {
  va_list args;
  va_start(args, format);
  void* stream = game::Call<void* (*)(int)>(0x140d3ef24)(1);
  uint64_t options = *game::Call<uint64_t* (*)()>(0x1402f1040)();
  return game::Call<int (*)(uint64_t, void*, const char*, void*, va_list)>(0x140d3cb50)(options, stream, format, nullptr, args);
}

// 0x1414d92a0: constructor (vtable 0x142486f80, 0x80-byte table cleared).
void* Construct142486f80(uint8_t* self) {
  At<int>(self, 0x20) = 0;
  At<uint64_t>(self, 0x10) = 0;
  At<uint64_t>(self, 0x18) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x28, 0, 0x80);
  At<int>(self, 8) = 0;
  At<uint64_t>(self, 0) = 0x142486f80;
  At<int>(self, 0xC) = 0x7FFFFFFF;
  At<int>(self, 0xA8) = 0;
  return self;
}

// 0x14164a5c0: 7-Zip CLZInWindow::MoveBlock: slide the kept window back to
// the start of the buffer (32-bit pointer arithmetic, as compiled).
uint64_t LzInWindowMoveBlock(uint8_t* self) {
  Virtual(self, 0);
  uint8_t* base = At<uint8_t*>(self, 8);
  uint32_t base32 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base));
  uint32_t buffer32 = At<uint32_t>(self, 0x20);
  uint32_t offset = At<uint32_t>(self, 0x2C) - At<uint32_t>(self, 0x30) - base32 + buffer32;
  uint32_t remaining = At<uint32_t>(self, 0x3C) - base32 - offset;
  game::Call<void* (*)(void*, const void*, size_t)>(0x140d11e20)(base, base + offset, buffer32 + remaining);
  At<uint64_t>(self, 0x20) -= offset;
  return Virtual<uint64_t>(self, 1);
}

// Destructors of classes holding one SoeUtil::Array member at Offset
// (vtable, data +8, count +0x10), dropping to the base vtable 0x1420633d0
// (0x1403ae2d0 / 0x1403ae5f0 / 0x1403b0e70).
template <size_t Offset, uint64_t ArrayVtable, int ThreadFlag>
void ArrayMemberDestructor1420633d0(uint8_t* self) {
  At<int>(self, Offset + 0x10) = 0;
  At<uint64_t>(self, Offset) = ArrayVtable;
  FreeStorage(At<void*>(self, Offset + 8), ThreadFlag);
  At<void*>(self, Offset + 8) = nullptr;
  At<uint64_t>(self, 0) = 0x1420633d0;
}

// 0x140ce9630 / 0x140484160: build an IString result through a helper.
void* MakeStringVia140ce9690(uint8_t* out, void* source) {
  InitIString(out);
  game::Call<void (*)(void*, void*)>(0x140ce9690)(source, out);
  return out;
}
void* MakeStringVia1404841c0(void* source, uint8_t* out, int value) {
  InitIString(out);
  game::Call<void (*)(void*, int, void*)>(0x1404841c0)(source, value, out);
  return out;
}

// 0x1406565f0: reset (+8), restamp both frame ids (+0x408 / +0x40C from
// 0x142b33c8c), slot 5 on +0x418 and +0x440, then 0x140bf1690(+0x430).
uint64_t ResetAndRestamp(uint8_t* self) {
  game::Call<void (*)(void*)>(0x140653140)(self + 8);
  At<int>(self, 0x408) = *reinterpret_cast<int*>(0x142b33c8c);
  At<int>(self, 0x40C) = *reinterpret_cast<int*>(0x142b33c8c);
  Virtual(At<void*>(self, 0x418), 5);
  Virtual(At<void*>(self, 0x440), 5);
  return game::Call<uint64_t (*)(void*)>(0x140bf1690)(At<void*>(self, 0x430));
}

// 0x14184e8b0: set the name (+0x58) and create the 0x3840-byte member
// (+0x50) initialized from +0x60.
uint64_t CreateMember50(uint8_t* self, const char* name) {
  if (name) game::Call<void (*)(void*, const char*)>(0x1402bd670)(self + 0x58, name);
  void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x3840);
  At<void*>(self, 0x50) = memory ? game::Call<void* (*)(void*)>(0x1418847a0)(memory) : nullptr;
  game::Call<void (*)(void*, void*)>(0x141884ce0)(At<void*>(self, 0x50), At<void*>(self, 0x60));
  return 0;
}

// 0x14039fca0: SoeUtil::Array destructor (vtable 0x14206e9c8, 8-aligned).
void ArrayDestructor14206e9c8(uint8_t* self) {
  At<int>(self, 0x10) = 0;
  At<uint64_t>(self, 0) = 0x14206e9c8;
  FreeStorage(At<void*>(self, 8), 8);
  At<void*>(self, 8) = nullptr;
}

// 0x1416038d0: Crypto::Prng reset: make sure both 16-byte buffers (+0x40,
// +0x70) are terminated, clear the position and block counter.
void PrngReset(uint8_t* self) {
  for (size_t offset : {size_t{0x40}, size_t{0x70}}) {
    if (At<int>(self, offset + 0x10) < 0x10) {
      if (char* end = game::Call<char* (*)(void*, int)>(0x141603400)(self + offset, 0xF)) *end = 0;
    }
  }
  At<int>(self, 0xA0) = 0;
  At<uint64_t>(self, 0xA8) = 0;
}

// 0x14032e590: "major.minor" Windows version into `out` (GetVersionEx via
// the IAT).
const char* WindowsVersionString(uint8_t* out) {
  struct VersionInfo {
    uint32_t size;
    uint32_t major;
    uint32_t minor;
    uint8_t rest[0x108];
  } info{};
  static_assert(sizeof(VersionInfo) == 0x114);
  info.size = 0x114;
  (*reinterpret_cast<int(__stdcall**)(VersionInfo*)>(0x1440a0050))(&info);
  game::Call<void (*)(void*, const char*, ...)>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x14204efe8), info.major, info.minor);
  return At<const char*>(out, 8);
}

// 0x140313b40: SoeUtil::String AppendChar (copy-on-write aware).
void StringAppendChar(uint8_t* self, char c) {
  int capacity = At<int>(self, 0x14);
  int needed = At<int>(self, 0x10) + 2;
  if (needed > capacity || (capacity > 0 && reinterpret_cast<int*>(At<char*>(self, 8))[-1] > 1))
    game::Call<void (*)(void*, int)>(0x1402befa0)(self, needed);
  At<char*>(self, 8)[At<int>(self, 0x10)] = c;
  ++At<int>(self, 0x10);
  At<char*>(self, 8)[At<int>(self, 0x10)] = 0;
}

// 0x14076bd70: constructor (vtables 0x142101d00 / 0x142101cd0, 0x800-byte
// table cleared).
void* Construct142101d00(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x142101d00;
  At<int>(self, 0x28) = 0;
  At<uint64_t>(self, 0x18) = 0;
  At<uint64_t>(self, 0x20) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x30, 0, 0x800);
  At<int>(self, 0x10) = 0;
  At<uint64_t>(self, 8) = 0x142101cd0;
  At<int>(self, 0x14) = 0x7FFFFFFF;
  return self;
}

// 0x14034ea30: GameCore InputThread constructor (64 KB stack, "Input
// Thread" name, owner at +0xB8).
void* InputThreadConstruct(uint8_t* self, int mode, void* owner) {
  game::Call<void (*)(void*, int, int, const char*)>(0x1403357d0)(self, 0x10000, 2, reinterpret_cast<const char*>(0x142053030));
  At<uint64_t>(self, 0) = 0x142052fc0;
  At<int>(self, 0xB0) = mode;
  At<void*>(self, 0xB8) = owner;
  return self;
}

// 0x1415f9be0: SoeUtil::ArraySecure destructor (inline storage at +0x18).
uint64_t ArraySecureDestructor(uint8_t* self) {
  At<int>(self, 0x10) = 0;
  At<uint64_t>(self, 0) = 0x1424b3258;
  void* data = At<void*>(self, 8);
  if (data != self + 0x18) FreeStorage(data, 1);
  At<void*>(self, 8) = nullptr;
  return game::Call<uint64_t (*)(void*)>(0x1415f9ae0)(self);
}

// 0x14049d110: create the 0x1D8-byte singleton (0x142b1d5c0).
void CreateSingleton142b1d5c0() {
  auto* memory = game::Call<uint8_t* (*)(size_t)>(0x1402fc0f0)(0x1D8);
  if (!memory) {
    *reinterpret_cast<void**>(0x142b1d5c0) = nullptr;
    return;
  }
  game::Call<void (*)(void*, const char*)>(0x14187b7f0)(memory, reinterpret_cast<const char*>(0x1420b3748));
  At<uint64_t>(memory, 0) = 0x1420b3710;
  *reinterpret_cast<void**>(0x142b1d5c0) = memory;
}


// Inlined ~StringFixed<N> / ~IString variants: release through the
// interface vtable, then reset to the empty sentinel and the plain vtable.
// (0x1402bace0 / 0x1402baa30 / 0x140305cf0 / 0x1402ef190 / 0x140305d50 /
// 0x14030c360, and the wide-string 0x1402ed380.)
template <uint64_t InterfaceVtable, uint64_t EmptyData, uint64_t PlainVtable>
void FixedStringDestructor(uint8_t* self) {
  At<uint64_t>(self, 0) = InterfaceVtable;
  if (At<int>(self, 0x14) > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(At<uint8_t*>(self, 8) - 4), -1) - 1 <= 0) Virtual(self, 2);
  At<uint64_t>(self, 8) = EmptyData;
  At<uint64_t>(self, 0x10) = 0;
  At<uint64_t>(self, 0) = PlainVtable;
}

// Class vtable, release the IString member at Offset, base vtable
// 0x1420633d0 (0x1403b04e0 / 0x1403b0980).
template <uint64_t Vtable, size_t Offset>
void VtableStringMemberDestructor(uint8_t* self) {
  At<uint64_t>(self, 0) = Vtable;
  uint8_t* text = self + Offset;
  At<uint64_t>(text, 0) = 0x142049b50;
  if (At<int>(text, 0x14) > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(At<uint8_t*>(text, 8) - 4), -1) - 1 <= 0) Virtual(text, 2);
  At<uint64_t>(self, 0) = 0x1420633d0;
}

// 0x1418847a0: constructor (vtables 0x142512ab8 -> 0x142512ad0, 0x800-byte
// table cleared, member at +0x818).
void* Construct142512ad0(uint8_t* self) {
  At<int>(self, 0x10) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x18, 0, 0x800);
  At<int>(self, 8) = 0;
  At<uint64_t>(self, 0) = 0x142512ab8;
  At<int>(self, 0xC) = 0x7FFFFFFF;
  game::Call<void (*)(void*)>(0x1403945d0)(self + 0x818);
  At<uint64_t>(self, 0) = 0x142512ad0;
  return self;
}

// 0x14039acb0: constructor (type 0x79, vtable 0x142064070).
void* Construct142064070(uint8_t* self) {
  At<int>(self, 8) = 0x79;
  At<uint64_t>(self, 0) = 0x142064070;
  game::Call<void (*)(void*)>(0x1417f2640)(self + 0x10);
  At<uint8_t*>(self, 0x1B0) = self + 0x10;
  At<int>(self, 0x1B8) = *reinterpret_cast<int*>(0x142b186ac);
  return self;
}

// 0x14034a5c0: dotted-quad IPv4 text ("%d.%d.%d.%d") into buffer.
char* FormatIpv4(const uint8_t* address, char* buffer, int size) {
  if (size < 0x10) {
    buffer[0] = 0;
    return buffer;
  }
  game::Call<int (*)(char*, int, const char*, ...)>(0x140310920)(buffer, size, reinterpret_cast<const char*>(0x142052570), address[0], address[1], address[2],
                                                                 address[3]);
  return buffer;
}

// 0x14032e0c0: fetch a 0x400-character wide name through the IAT
// (0x1440a0070) and convert it into out on success.
bool GetWideName(void* out) {
  uint16_t name[0x400];
  uint32_t size = 0x400;
  name[0] = 0;
  bool ok = (*reinterpret_cast<int(__stdcall**)(uint16_t*, uint32_t*)>(0x1440a0070))(name, &size) != 0;
  if (ok) game::Call<void (*)(uint16_t*, void*)>(0x14033a0c0)(name, out);
  return ok;
}

// 0x141603120: Crypto::Prng constructor.
void* PrngConstruct(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1424b8d00;
  game::Call<void (*)(void*)>(0x1415fc490)(self + 8);
  At<uint64_t>(self, 0x40) = 0x1424b8cd8;
  At<uint64_t>(self, 0x48) = 0;
  At<uint64_t>(self, 0x50) = 0;
  At<uint64_t>(self, 0x70) = 0x1424b8cd8;
  At<uint64_t>(self, 0x78) = 0;
  At<uint64_t>(self, 0x80) = 0;
  At<int>(self, 0xA0) = 0;
  At<uint64_t>(self, 0xA8) = 0;
  At<uint8_t>(self, 0xB0) = 0;
  return self;
}

// 0x140325740: pop the head of the embedded list (+0x10: head +8, tail
// +0x10, count +0x18; node next +0x10 / prev +0x18) and release it
// through the list's slot 3.
bool PopListHead(uint8_t* self) {
  uint8_t* list = self + 0x10;
  uint8_t* node = At<uint8_t*>(list, 8);
  if (!node) return true;
  uint8_t* prev = At<uint8_t*>(node, 0x18);
  if (!prev)
    At<uint8_t*>(list, 8) = At<uint8_t*>(node, 0x10);
  else
    At<uint8_t*>(prev, 0x10) = At<uint8_t*>(node, 0x10);
  uint8_t* next = At<uint8_t*>(node, 0x10);
  if (!next) {
    --At<int>(list, 0x18);
    At<uint8_t*>(list, 0x10) = At<uint8_t*>(node, 0x18);
  } else {
    At<uint8_t*>(next, 0x18) = At<uint8_t*>(node, 0x18);
    --At<int>(list, 0x18);
  }
  Virtual(list, 3, static_cast<void*>(node));
  return true;
}

// 0x140931e10: for a known entry (0x140565740 on world +0xB618) apply it
// (0x1406a8d10) when slot 159 allows and state 0xF is not set.
void ApplyKnownEntry(void** self, void* key) {
  auto* entry = game::Call<int* (*)(void*, void*)>(0x140565740)(*reinterpret_cast<uint8_t**>(0x142b19ba0) + 0xB618, key);
  if (!entry) return;
  if (!Virtual<bool>(self, 159)) return;
  if (game::Call<bool (*)(void*, int)>(0x140514940)(self, 0xF)) return;
  game::Call<void (*)(void*, int)>(0x1406a8d10)(self, *entry);
}

// 0x1409f1310: copy the global id (0x143be26d0) to +0x10 and dispatch the
// (9, 0x16) event.
uint64_t DispatchEvent9_16(uint8_t* self) {
  At<uint64_t>(self, 0x10) = *reinterpret_cast<uint64_t*>(0x143be26d0);
  struct Event {
    uint64_t vtable;
    int category;
    int code;
    uint64_t data;
  } event{0x142162290, 9, 0x16, 0};
  return game::Call<uint64_t (*)(void*, Event*, int, bool)>(0x140978600)(At<void*>(*reinterpret_cast<void**>(0x142b19b98), 8), &event, 0, true);
}

// 0x1415f8850: constructor (base 0x1415f9ac0, two ints, init from source).
void* Construct1415f8850(uint8_t* self, void* source, int first, int second) {
  game::Call<void (*)(void*)>(0x1415f9ac0)(self);
  At<int>(self, 0x60) = first;
  At<int>(self, 0x64) = second;
  game::Call<void (*)(void*, void*)>(0x1415f9fe0)(self, source);
  return self;
}


// 0x140310920: snprintf-style formatter (0x14032ed90 takes the va_list).
int FormatToBuffer(char* buffer, int size, const char* format, ...) {
  va_list args;
  va_start(args, format);
  return game::Call<int (*)(char*, int, const char*, va_list)>(0x14032ed90)(buffer, size, format, args);
}

// 0x1403ae330: destructor of a class holding an intrusive list at +0x18
// (head +0x28): unlink everything, drop to the base vtable 0x1420633d0.
void ListMemberDestructor1420633d0(uint8_t* self) {
  At<uint64_t>(self, 0x18) = 0x142065520;
  if (At<void*>(self, 0x28)) {
    do {
      if (void* head = At<void*>(self, 0x28)) game::Call<void (*)(void*, void*)>(0x140445b10)(self + 0x18, head);
    } while (At<void*>(self, 0x28));
  }
  At<uint64_t>(self, 0) = 0x1420633d0;
}

// 0x1403334f0: ini GetString(section, key, default, out, ...) through an
// 8 KB stack buffer (0x1403331e0), then assign into out.
void IniGetString(void* ini, const char* section, const char* key, const char* fallback, void* out, uint8_t expand, int a7, int a8) {
  char buffer[0x2000];
  game::Call<void (*)(void*, const char*, const char*, const char*, char*, int, uint8_t, int, int)>(0x1403331e0)(ini, section, key, fallback, buffer, 0x2000,
                                                                                                                  expand, a7, a8);
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(out, buffer);
}

// 0x1403f6080: parse the value stored under the fixed key 0x72279F7F (or
// the empty string) as an int (0x1402ecf30).
int ParseFixedKeyValue(uint8_t* self) {
  const char* text = reinterpret_cast<const char*>(0x142046fcb);
  if (int count = At<int>(self, 0x20)) {
    uint32_t index = static_cast<uint32_t>(count - 1) & 0x72279F7Fu;
    for (uint8_t* node = At<uint8_t**>(self, 0x40)[index]; node; node = At<uint8_t*>(node, 0x30)) {
      if (At<uint32_t>(node, 0x28) == 0x72279F7Fu) {
        text = At<const char*>(node, 8);
        break;
      }
    }
  }
  int scratch;
  return *game::Call<int* (*)(const char*, int*)>(0x1402ecf30)(text, &scratch);
}

// 0x14033e7e0: allocate from the pool under its lock (+0x518).
void* PoolAllocateLocked(uint8_t* self, int size) {
  uint8_t* lock = self + 0x518;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  void* block = game::Call<void* (*)(void*, int)>(0x14033e480)(self, size);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  return block;
}

// 0x1403b0f30: destructor (member +0x18 vtables 0x142066390 / 0x142066370,
// member +0x38, base vtable 0x1420633d0).
void Destructor1403b0f30(uint8_t* self) {
  At<uint64_t>(self, 0x18) = 0x142066390;
  game::Call<void (*)(void*)>(0x1404534c0)(self + 0x18);
  game::Call<void (*)(void*)>(0x1403a79d0)(self + 0x38);
  At<uint64_t>(self, 0x18) = 0x142066370;
  game::Call<void (*)(void*)>(0x1404534c0)(self + 0x18);
  At<uint64_t>(self, 0) = 0x1420633d0;
}

// 0x1406a8d10: queue a small id request (0x30 bytes, vtable 0x1420eac40)
// on the object (0x140515fc0).
uint64_t QueueIdRequest(void* self, int id) {
  auto* request = game::Call<uint8_t* (*)(size_t)>(0x1402fc0f0)(0x30);
  if (request) {
    At<uint64_t>(request, 8) = 0;
    At<uint64_t>(request, 0x10) = 0;
    At<uint64_t>(request, 0) = 0x1420eac40;
    At<uint64_t>(request, 0x18) = 0xF;
    At<uint8_t>(request, 0x20) = 0;
    At<int>(request, 0x28) = id;
  }
  return game::Call<uint64_t (*)(void*, void*)>(0x140515fc0)(self, request);
}

// 0x140a885e0: when the world exists and the check passes without the
// blocking flag, send action 6 with the world's +0x2D0 value.
void SendAction6(void* self) {
  uint8_t* world = *reinterpret_cast<uint8_t**>(0x142b19ba0);
  if (!world) return;
  uint8_t blocked = 0;
  int value = 0;
  if (!game::Call<bool (*)(void*, uint8_t*, int*)>(0x140a87de0)(self, &blocked, &value) || blocked) return;
  game::Call<void (*)(void*, int, int, int)>(0x140a88730)(self, 6, 0, At<int>(world, 0x2D0));
}

// 0x14165b570: reset the reader: buffered mode (+0xA8 > 0) clears the
// +0x1B8 buffer and its cursors, else resets +0x68; position cleared.
void ResetReader(uint8_t* self) {
  if (At<int>(self, 0xA8) > 0) {
    game::Call<void (*)(void*, int)>(0x140337f50)(self + 0x1B8, 0);
    At<uint64_t>(self, 0x208) = 0;
    At<uint64_t>(self, 0x210) = 0;
  } else {
    game::Call<void (*)(void*)>(0x14165b450)(self + 0x68);
  }
  At<uint64_t>(self, 0x88) = 0;
  At<int>(self, 0x80) = 0;
}

// 0x141ec2a90: destructor (vtable 0x1425ad130) shutting down and freeing
// the 0x190-byte member at +8.
void Destructor1425ad130(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1425ad130;
  if (!At<void*>(self, 8)) return;
  game::Call<void (*)(void*)>(0x14166a8a0)(At<void*>(self, 8));
  if (void* member = At<void*>(self, 8)) {
    game::Call<void (*)(void*)>(0x141668bc0)(member);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(member, 0x190);
  }
  At<void*>(self, 8) = nullptr;
}

// 0x1402ee3a0: resolve the context (0x1402ee000) and forward everything
// to 0x1402ed840.
uint64_t ForwardWithContext(void* source, int a2, uint8_t a3, int a4, void* a5, void* a6) {
  void* context = game::Call<void* (*)(void*)>(0x1402ee000)(source);
  return game::Call<uint64_t (*)(void*, void*, int, uint8_t, int, void*, void*)>(0x1402ed840)(context, source, a2, a3, a4, a5, a6);
}

// 0x1414d91b0: query the object's handler (+0x80, slot 48) with a request
// built from the object and the default parameters (0x142b06ac0).
void** QueryHandler(void** out, uint8_t* object) {
  struct Request {
    void* object;
    uint64_t pad;
    uint8_t parameters[16];
    uint8_t flags;
  } request{};
  static_assert(offsetof(Request, parameters) == 0x10 && offsetof(Request, flags) == 0x20);
  request.object = object;
  for (int i = 0; i < 16; ++i) request.parameters[i] = reinterpret_cast<const uint8_t*>(0x142b06ac0)[i];
  request.flags = 0x20;
  void* handler = At<void*>(object, 0x80);
  *out = Virtual<void*>(handler, 48, &request);
  return out;
}

// 0x1403a97f0 / 0x1403a95c0: reset an intrusive shared pointer (count
// block at object +8: strong, weak).
void ResetSharedPointer(void** holder) {
  uint8_t* object = static_cast<uint8_t*>(*holder);
  if (!object) return;
  *holder = nullptr;
  auto* counts = At<volatile long*>(object, 8);
  bool lastStrong = _InterlockedDecrement(&counts[0]) == 0;
  if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(const_cast<long*>(counts), 0x10);
  if (lastStrong) Virtual(object, 1);
}

// 0x1417d9420: constructor (vtables 0x1425017b0 / 0x1425017e0 /
// 0x142501780, 0x100-byte table cleared).
void* Construct1425017b0(uint8_t* self) {
  At<uint64_t>(self, 0) = 0x1425017b0;
  At<int>(self, 0x18) = 0;
  At<uint64_t>(self, 0x10) = 0x1425017e0;
  At<int>(self, 0x40) = 0;
  At<uint64_t>(self, 0x30) = 0;
  At<uint64_t>(self, 0x38) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x48, 0, 0x100);
  At<int>(self, 0x28) = 0;
  At<uint64_t>(self, 0x20) = 0x142501780;
  At<int>(self, 0x2C) = 0x7FFFFFFF;
  return self;
}


namespace {
struct ByteReader {
  const uint8_t* data;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint8_t failed;
  uint8_t pad;
};
static_assert(offsetof(ByteReader, cursor) == 0x10 && offsetof(ByteReader, failed) == 0x20);
}  // namespace

// Packet / record parsers over a byte buffer (0x1403895e0, 0x14038c0e0,
// 0x14038bf90, 0x14038e630, 0x14038b0e0, 0x14038c150, 0x14038c070): parse
// with Parser(target, reader); fail on a reader error, or on trailing bytes
// unless allowed.
template <uint64_t Parser>
bool ParseFromBuffer(void* target, const uint8_t* data, int length, bool allowTrailing) {
  if (!target || !data) return false;
  ByteReader reader{data, length, data, data + length, 0, 0};
  game::Call<void (*)(void*, ByteReader*)>(Parser)(target, &reader);
  if (reader.failed) return false;
  if (!allowTrailing && static_cast<int>(reinterpret_cast<uintptr_t>(reader.end)) - static_cast<int>(reinterpret_cast<uintptr_t>(reader.cursor)) > 0)
    return false;
  return true;
}

// 0x1402ee000: lazily initialized global (0x142ae8598), guarded by an
// atomic once flag (0x142ae85dc).
void* LazyGlobal142ae8598() {
  auto* once = reinterpret_cast<volatile char*>(0x142ae85dc);
  if (*once == 0 && _InterlockedCompareExchange8(once, 1, 0) == 0) game::Call<void (*)()>(0x1402eeb30)();
  return *reinterpret_cast<void**>(0x142ae8598);
}

// 0x14032ed90: vsnprintf (__stdio_common_vsprintf), -1 on error.
int FormatToBufferV(char* buffer, int size, const char* format, va_list args) {
  uint64_t options = *game::Call<uint64_t* (*)()>(0x1402f1040)();
  int result = game::Call<int (*)(uint64_t, char*, int64_t, const char*, void*, va_list)>(0x140d3d5c8)(options, buffer, size, format, nullptr, args);
  return result < 0 ? -1 : result;
}

// 0x140a85750: register the seven fixed audio state hashes.
uint64_t RegisterAudioStateHashes(void* self) {
  using SetFn = void (*)(void*, uint32_t);
  game::Call<SetFn>(0x140a80fa0)(self, 0x86274BAD);
  game::Call<SetFn>(0x140a81030)(self, 0x833509AA);
  game::Call<SetFn>(0x140a811e0)(self, 0x3C5DCCE4);
  game::Call<SetFn>(0x140a810c0)(self, 0x4A5F6ABB);
  game::Call<SetFn>(0x140a81300)(self, 0x7495C48A);
  game::Call<SetFn>(0x140a81150)(self, 0x3C518C07);
  return game::Call<uint64_t (*)(void*, uint32_t)>(0x140a81270)(self, 0xE9185BAE);
}

// 0x14039b600: constructor (type 0x11 / 0x31, vtables 0x142065580 /
// 0x142065550, 0x400-byte table cleared).
void* Construct142065580(uint8_t* self) {
  At<int>(self, 8) = 0x11;
  At<uint64_t>(self, 0) = 0x142065580;
  At<int>(self, 0x10) = 0x31;
  At<int>(self, 0x38) = 0;
  At<uint64_t>(self, 0x28) = 0;
  At<uint64_t>(self, 0x30) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x40, 0, 0x400);
  At<int>(self, 0x20) = 0;
  At<uint64_t>(self, 0x18) = 0x142065550;
  At<int>(self, 0x24) = 0x7FFFFFFF;
  return self;
}

// 0x14030d4b0: format the current wall-clock time of day into +0xDFA0.
void FormatTimeOfDay(uint8_t* self) {
  struct TimeParts {
    uint8_t head[16];
    int part0;
    int part1;
    int part2;
  } parts;
  static_assert(offsetof(TimeParts, part0) == 0x10);
  for (int i = 0; i < 16; ++i) parts.head[i] = reinterpret_cast<const uint8_t*>(0x14204b110)[i];
  parts.part0 = 0;
  parts.part1 = 0;
  parts.part2 = 0;
  int64_t now;
  int64_t* time = game::Call<int64_t* (*)(int64_t*)>(0x14032fe90)(&now);
  game::Call<void (*)(int64_t*, TimeParts*, bool)>(0x14032fbe0)(time, &parts, true);
  game::Call<void (*)(void*, const char*, ...)>(0x1402bd7f0)(self + 0xDFA0, reinterpret_cast<const char*>(0x14204b088), At<int>(&parts, 0xC), parts.part0,
                                                           parts.part1);
}

// 0x1403f5ff0: name of the currently selected entry (+0x38AC8) into out.
bool CurrentSelectionName(uint8_t* self, void* out) {
  int key = game::Call<int (*)(void*)>(0x140cf3dc0)(At<void*>(self, 0x38AC8));
  uint8_t* entry = game::Call<uint8_t* (*)(void*, int)>(0x140cf41c0)(At<void*>(self, 0x38AC8), key);
  if (!entry || !game::Call<void* (*)(void*, bool)>(0x140cf3e60)(entry, true)) return false;
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(out, At<const char*>(entry, 0x20));
  return true;
}

// 0x140839910: query the lazily created global (0x142b19e60) under its
// lock (+0x20).
uint64_t QueryLazyGlobal142b19e60() {
  if (!*reinterpret_cast<bool*>(0x143bcf3b0)) {
    game::Call<void (*)()>(0x1408395e0)();
    *reinterpret_cast<bool*>(0x143bcf3b0) = true;
  }
  uint8_t* object = *reinterpret_cast<uint8_t**>(0x142b19e60);
  uint8_t* lock = object + 0x20;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  uint64_t result = game::Call<uint64_t (*)(void*)>(0x140839c00)(object);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  return result;
}


// 0x1406fe940: constructor (vtable 0x1420f4fb8, name string +8, member
// +0x38 built from two constant tables).
void* Construct1420f4fb8(uint8_t* self) {
  At<uint64_t>(self, 0x10) = 0x143e09641;
  At<uint64_t>(self, 0x18) = 0;
  At<uint64_t>(self, 8) = 0x1424bc0a0;
  At<uint64_t>(self, 0x28) = 0;
  At<uint64_t>(self, 0x30) = 0;
  At<uint64_t>(self, 0x20) = 0;
  At<uint64_t>(self, 0) = 0x1420f4fb8;
  game::Call<void (*)(void*, const void*, const void*)>(0x1406fe7b0)(self + 0x38, reinterpret_cast<const void*>(0x1420f4ed8),
                                                                    reinterpret_cast<const void*>(0x1420f5068));
  return self;
}

// 0x1407e8370: constructor (vtables 0x142111aa0 / 0x142111ad0, 0x320-byte
// table cleared).
void* Construct142111aa0(uint8_t* self) {
  At<int>(self, 0x20) = 0;
  At<uint64_t>(self, 0x10) = 0;
  At<uint64_t>(self, 0x18) = 0;
  game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(self + 0x28, 0, 0x320);
  At<int>(self, 8) = 0;
  At<uint64_t>(self, 0) = 0x142111aa0;
  At<int>(self, 0xC) = 0x7FFFFFFF;
  At<uint64_t>(self, 0x348) = 0x142111ad0;
  At<uint64_t>(self, 0x350) = 0;
  At<uint64_t>(self, 0x358) = 0;
  return self;
}

// 0x140954630: under the lock (+0x138), call the member (+0x38) slot 15
// (enable) or 16 (disable).
void ToggleMemberLocked(uint8_t* self, uint8_t enable) {
  uint8_t* lock = self + 0x138;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  void* member = At<void*>(self, 0x38);
  if (enable)
    Virtual(member, 15);
  else
    Virtual(member, 16);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

// 0x14078ab10: set the four strings once (+0x58 guards).
void SetFourStringsOnce(uint8_t* self, const char* a, const char* b, const char* c, const char* d) {
  if (At<uint8_t>(self, 0x58)) return;
  using AssignFn = void (*)(void*, const char*);
  game::Call<AssignFn>(0x1402bd670)(self + 0x8C8, a);
  game::Call<AssignFn>(0x1402bd670)(self + 0x880, b);
  game::Call<AssignFn>(0x1402bd670)(self + 0x8B0, c);
  game::Call<AssignFn>(0x1402bd670)(self + 0x898, d);
  At<uint8_t>(self, 0x58) = 1;
}

// 0x140340c60: return a block to the pool under its lock (+0x518).
void PoolFreeLocked(uint8_t* self, void* block, unsigned flags) {
  uint8_t* lock = self + 0x518;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  game::Call<void (*)(void*, void*, unsigned)>(0x140340970)(self, block, flags);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

// 0x14165af80: set the owner (+0x90) and path (+0x98); in buffered mode
// open the file (+0x1B8), warning on failure.
bool OpenReaderFile(uint8_t* self, void* owner, const char* path) {
  At<void*>(self, 0x90) = owner;
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self + 0x98, path);
  if (At<int>(self, 0xA8) <= 0) return true;
  if (game::Call<bool (*)(void*, const char*, int, int)>(0x140337ae0)(self + 0x1B8, At<const char*>(self, 0xA0), 3, 3)) return true;
  game::Call<void (*)(void*, const char*, ...)>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x1424c2060), At<const char*>(self, 0xA0));
  return false;
}

// 0x1403d4c60: drain both lists (+0xC0 head +0xD0, +0x500 head +0x510)
// around the base cleanup (0x1403e3360).
void DrainBothLists(uint8_t* self) {
  while (void* head = At<void*>(self, 0xD0)) game::Call<void (*)(void*, void*)>(0x140443d00)(self + 0xC0, head);
  game::Call<void (*)(void*)>(0x1403e3360)(self);
  while (void* head = At<void*>(self, 0x510)) game::Call<void (*)(void*, void*)>(0x140446180)(self + 0x500, head);
}

// 0x140396ad0: constructor (vtable 0x1424bb930) with a name.
void* Construct1424bb930(uint8_t* self, const char* name) {
  At<uint64_t>(self, 0x10) = 0x143e09641;
  At<uint64_t>(self, 0x18) = 0;
  At<uint64_t>(self, 8) = 0x142049de8;
  At<uint64_t>(self, 0) = 0x1424bb930;
  game::Call<void (*)(void*)>(0x1403d4a10)(self);
  game::Call<void (*)(void*, const char*)>(0x1402bd670)(self + 8, name);
  return self;
}

// 0x14077ff60: drop the global shared object (0x143bc51b0).
void ReleaseGlobal143bc51b0() {
  ResetSharedPointer(reinterpret_cast<void**>(0x143bc51b0));
}

// 0x1403f8380: the entry for the id if its owner list contains the
// current object (0x1404735e0).
void* EntryIfOwned(uint8_t* self, const int* id) {
  int key = *id;
  uint8_t* entry = game::Call<uint8_t* (*)(void*, int*)>(0x14071f100)(At<void*>(self, 0x38860), &key);
  if (!entry) return nullptr;
  void* current = game::Call<void* (*)()>(0x1404735e0)();
  for (void** node = Virtual<void**>(entry + 0x20, 0); node; node = static_cast<void**>(*node))
    if (node == current) return entry;
  return nullptr;
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
REBUILD_FUNCTION(StopActiveSlot, 0x141868440, StopActiveSlot);
REBUILD_FUNCTION(Log_V, 0x14032fa90, LogV);
REBUILD_FUNCTION(SetLevel5To30, 0x14049c1d0, SetLevel5To30);
REBUILD_FUNCTION(ForwardFloatToTarget, 0x1402f57e0, ForwardFloatToTarget);
REBUILD_FUNCTION(FreeMember50, 0x14184e920, FreeMember50);
REBUILD_FUNCTION(PostAudioEventF920AF90Delayed, 0x140466000, PostAudioEventF920AF90Delayed);
REBUILD_FUNCTION(Destructor_1424d1ac8, 0x1416d7a60, Destructor1424d1ac8);
REBUILD_FUNCTION(CopyString3358, 0x1403f6ba0, CopyString3358);
REBUILD_FUNCTION(FlagAllEntries, 0x1407218c0, FlagAllEntries);
REBUILD_FUNCTION(SetEntryFlag, 0x14133e6c0, SetEntryFlag);
REBUILD_FUNCTION(GetWidePath, 0x14032e120, GetWidePath);
REBUILD_FUNCTION(HandleIfKnown, 0x140637490, HandleIfKnown);
REBUILD_FUNCTION(LookupLoaded, 0x141ec2150, LookupLoaded);
REBUILD_FUNCTION(DispatchEventB2, 0x14079fbc0, DispatchEventB2);
REBUILD_FUNCTION(DispatchEvent98, 0x14079c160, DispatchEvent98);
REBUILD_FUNCTION(BumpCounter378, 0x140346f80, BumpCounter378);
REBUILD_FUNCTION(VariantSetInt, 0x14046d7b0, VariantSetInt);
REBUILD_FUNCTION(StopEmitter, 0x140aa9460, StopEmitter);
REBUILD_FUNCTION(InitNamedSetting, 0x140483490, InitNamedSetting);
REBUILD_FUNCTION(IsOwnerThreadAndEmpty, 0x14040d960, IsOwnerThreadAndEmpty);
REBUILD_FUNCTION(ElementAtGrowA8, 0x1403b4810, ElementAtGrowA8);
REBUILD_FUNCTION(LookupFrom3920Slot83, 0x14050f340, LookupFrom3920Slot83);
REBUILD_FUNCTION(Construct_142064398, 0x14039a4e0, Construct142064398);
REBUILD_FUNCTION(ResetBuffers2050, 0x140558310, ResetBuffers2050);
REBUILD_FUNCTION(Audio_RunOnObject, 0x141502400, AudioRunOnObject);
REBUILD_FUNCTION(Audio_ManagerInstance, 0x141501840, AudioManagerInstance);
REBUILD_FUNCTION(SetValue180ToDac, 0x14049c180, SetValue180ToDac);
REBUILD_FUNCTION(Variant_SetBool, 0x14046d940, VariantSetBool);
REBUILD_FUNCTION(Variant_SetPointer, 0x14046be30, VariantSetPointer);
REBUILD_FUNCTION(Variant_SetDouble, 0x14046d800, VariantSetDouble);
REBUILD_FUNCTION(InsertIfMissing, 0x1406297b0, InsertIfMissing);
REBUILD_FUNCTION(StartTimer6c, 0x1404cf240, StartTimer6c);
REBUILD_FUNCTION(ResetCopyThenSlot5, 0x14079c110, ResetCopyThenSlot5);
REBUILD_FUNCTION(FindEntryAndApply, 0x140631cd0, FindEntryAndApply);
REBUILD_FUNCTION(StampTimes, 0x14049b6a0, StampTimes);
REBUILD_FUNCTION(SetGlobalToggle, 0x1414585d0, SetGlobalToggle);
REBUILD_FUNCTION(TransformAngle, 0x14050dd30, TransformAngle);
REBUILD_FUNCTION(ReloadIni1d38, 0x140ab7b20, ReloadIni1d38);
REBUILD_FUNCTION(ApplyStringPairs3d3c0, 0x14047acd0, ApplyStringPairs3d3c0);
REBUILD_FUNCTION(Construct_14250ce68, 0x141845390, Construct14250ce68);
REBUILD_FUNCTION(CopyPacked31, 0x14039c860, CopyPacked31);
REBUILD_FUNCTION(LoggingHeader_Destructor, 0x14165c630, LoggingHeaderDestructor);
REBUILD_FUNCTION(CopyThreeStrings, 0x14046d1d0, CopyThreeStrings);
REBUILD_FUNCTION(Construct_1420663b0, 0x14039d9f0, Construct1420663b0);
REBUILD_FUNCTION(LookupValue1d0, 0x140428fc0, LookupValue1d0);
REBUILD_FUNCTION(UpdateViews, 0x14062bbd0, UpdateViews);
REBUILD_FUNCTION(StoreSmallBytes, 0x1404685a0, StoreSmallBytes);
REBUILD_FUNCTION(WallClockNow, 0x14032fe90, WallClockNow);
REBUILD_FUNCTION(CreateAndInit93a0, 0x1403160f0, CreateAndInit93a0);
REBUILD_FUNCTION(Audio_QueueObjectCommand, 0x14150bd50, AudioQueueObjectCommand);
REBUILD_FUNCTION(AppendToOwnerList, 0x140cff570, AppendToOwnerList);
REBUILD_FUNCTION(DeletingDestructor370, 0x1403c2100, DeletingDestructor370);
REBUILD_FUNCTION(SpeedTree_ReleasePoolSlot, 0x141e62cb0, SpeedTreeReleasePoolSlot);
REBUILD_FUNCTION(SetFlag190Locked, 0x140344910, SetFlag190Locked);
REBUILD_FUNCTION(Destructor_142046860, 0x1403531d0, Destructor142046860);
REBUILD_FUNCTION(ReleaseOwned8And18, 0x1402f3140, ReleaseOwned8And18);
REBUILD_FUNCTION(MakeStringFrom, 0x140474ab0, MakeStringFrom);
REBUILD_FUNCTION(InfiniteLoopMonitor_Kick, 0x14032d190, InfiniteLoopMonitorKick);
REBUILD_FUNCTION(ApplyToAll78, 0x140b8d900, ApplyToAll78);
REBUILD_FUNCTION(WithinSecondsD8, 0x140428200, WithinSecondsD8);
REBUILD_FUNCTION(WithinSecondsD0, 0x140428130, WithinSecondsD0);
REBUILD_FUNCTION(Destructor_142512ab8, 0x141884960, Destructor142512ab8);
REBUILD_FUNCTION(ResolveEntryByte, 0x140611910, ResolveEntryByte);
REBUILD_FUNCTION(StringMemberDestructor_1403ae270, 0x1403ae270, StringMemberDestructor1420633d0<0x18>);
REBUILD_FUNCTION(StringMemberDestructor_1403ae490, 0x1403ae490, StringMemberDestructor1420633d0<0x20>);
REBUILD_FUNCTION(StringMemberDestructor_1403ae580, 0x1403ae580, StringMemberDestructor1420633d0<0x18>);
REBUILD_FUNCTION(StringMemberDestructor_1403b0fb0, 0x1403b0fb0, StringMemberDestructor1420633d0<0x18>);
REBUILD_FUNCTION(StringMemberDestructor_1403b0ed0, 0x1403b0ed0, StringMemberDestructor1420633d0<0x18>);
REBUILD_FUNCTION(StringMemberDestructor_1403b0540, 0x1403b0540, StringMemberDestructor1420633d0<0x10>);
REBUILD_FUNCTION(Ini_Clear, 0x1403322c0, IniClear);
REBUILD_FUNCTION(LookupAndForward, 0x14070b5d0, LookupAndForward);
REBUILD_FUNCTION(PrintToStdout, 0x140309e00, PrintToStdout);
REBUILD_FUNCTION(Construct_142486f80, 0x1414d92a0, Construct142486f80);
REBUILD_FUNCTION(CLZInWindow_MoveBlock, 0x14164a5c0, LzInWindowMoveBlock);
REBUILD_FUNCTION(ArrayMemberDestructor_1403ae2d0, 0x1403ae2d0, (ArrayMemberDestructor1420633d0<0x18, 0x142065440, 4>));
REBUILD_FUNCTION(ArrayMemberDestructor_1403ae5f0, 0x1403ae5f0, (ArrayMemberDestructor1420633d0<0x18, 0x142063770, 4>));
REBUILD_FUNCTION(ArrayMemberDestructor_1403b0e70, 0x1403b0e70, (ArrayMemberDestructor1420633d0<0x10, 0x1420642e8, 8>));
REBUILD_FUNCTION(MakeStringVia140ce9690, 0x140ce9630, MakeStringVia140ce9690);
REBUILD_FUNCTION(MakeStringVia1404841c0, 0x140484160, MakeStringVia1404841c0);
REBUILD_FUNCTION(ResetAndRestamp, 0x1406565f0, ResetAndRestamp);
REBUILD_FUNCTION(CreateMember50, 0x14184e8b0, CreateMember50);
REBUILD_FUNCTION(ArrayDestructor_14206e9c8, 0x14039fca0, ArrayDestructor14206e9c8);
REBUILD_FUNCTION(Crypto_Prng_Reset, 0x1416038d0, PrngReset);
REBUILD_FUNCTION(WindowsVersionString, 0x14032e590, WindowsVersionString);
REBUILD_FUNCTION(String_AppendChar, 0x140313b40, StringAppendChar);
REBUILD_FUNCTION(Construct_142101d00, 0x14076bd70, Construct142101d00);
REBUILD_FUNCTION(InputThread_Construct, 0x14034ea30, InputThreadConstruct);
REBUILD_FUNCTION(ArraySecure_Destructor, 0x1415f9be0, ArraySecureDestructor);
REBUILD_FUNCTION(CreateSingleton_142b1d5c0, 0x14049d110, CreateSingleton142b1d5c0);
REBUILD_FUNCTION(FixedStringDestructor_1402bace0, 0x1402bace0, (FixedStringDestructor<0x142049da8, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_1402baa30, 0x1402baa30, (FixedStringDestructor<0x142049ce0, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_140305cf0, 0x140305cf0, (FixedStringDestructor<0x142049de8, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_1402ef190, 0x1402ef190, (FixedStringDestructor<0x14204b2d0, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_140305d50, 0x140305d50, (FixedStringDestructor<0x142049e28, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_14030c360, 0x14030c360, (FixedStringDestructor<0x14204a358, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(WideFixedStringDestructor_1402ed380, 0x1402ed380, (FixedStringDestructor<0x142046d90, 0x142ae85c8, 0x14204efd0>));
REBUILD_FUNCTION(VtableStringMemberDestructor_1403b04e0, 0x1403b04e0, (VtableStringMemberDestructor<0x142063c90, 0x18>));
REBUILD_FUNCTION(VtableStringMemberDestructor_1403b0980, 0x1403b0980, (VtableStringMemberDestructor<0x142063c80, 0x18>));
REBUILD_FUNCTION(Construct_142512ad0, 0x1418847a0, Construct142512ad0);
REBUILD_FUNCTION(Construct_142064070, 0x14039acb0, Construct142064070);
REBUILD_FUNCTION(FormatIpv4, 0x14034a5c0, FormatIpv4);
REBUILD_FUNCTION(GetWideName, 0x14032e0c0, GetWideName);
REBUILD_FUNCTION(Crypto_Prng_Construct, 0x141603120, PrngConstruct);
REBUILD_FUNCTION(PopListHead, 0x140325740, PopListHead);
REBUILD_FUNCTION(ApplyKnownEntry, 0x140931e10, ApplyKnownEntry);
REBUILD_FUNCTION(DispatchEvent9_16, 0x1409f1310, DispatchEvent9_16);
REBUILD_FUNCTION(Construct_1415f8850, 0x1415f8850, Construct1415f8850);
REBUILD_FUNCTION(FormatToBuffer, 0x140310920, FormatToBuffer);
REBUILD_FUNCTION(FixedStringDestructor_1403aad80, 0x1403aad80, (FixedStringDestructor<0x14204ba88, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(FixedStringDestructor_14030c3c0, 0x14030c3c0, (FixedStringDestructor<0x14204ae80, 0x143e09641, 0x142049b50>));
REBUILD_FUNCTION(ListMemberDestructor_1403ae330, 0x1403ae330, ListMemberDestructor1420633d0);
REBUILD_FUNCTION(Ini_GetString, 0x1403334f0, IniGetString);
REBUILD_FUNCTION(ParseFixedKeyValue, 0x1403f6080, ParseFixedKeyValue);
REBUILD_FUNCTION(PoolAllocateLocked, 0x14033e7e0, PoolAllocateLocked);
REBUILD_FUNCTION(Destructor_1403b0f30, 0x1403b0f30, Destructor1403b0f30);
REBUILD_FUNCTION(QueueIdRequest, 0x1406a8d10, QueueIdRequest);
REBUILD_FUNCTION(SendAction6, 0x140a885e0, SendAction6);
REBUILD_FUNCTION(ResetReader, 0x14165b570, ResetReader);
REBUILD_FUNCTION(Destructor_1425ad130, 0x141ec2a90, Destructor1425ad130);
REBUILD_FUNCTION(ForwardWithContext, 0x1402ee3a0, ForwardWithContext);
REBUILD_FUNCTION(QueryHandler, 0x1414d91b0, QueryHandler);
REBUILD_FUNCTION(ResetSharedPointer_1403a97f0, 0x1403a97f0, ResetSharedPointer);
REBUILD_FUNCTION(ResetSharedPointer_1403a95c0, 0x1403a95c0, ResetSharedPointer);
REBUILD_FUNCTION(Construct_1425017b0, 0x1417d9420, Construct1425017b0);
REBUILD_FUNCTION(ParseFromBuffer_1403895e0, 0x1403895e0, ParseFromBuffer<0x14036af00>);
REBUILD_FUNCTION(ParseFromBuffer_14038c0e0, 0x14038c0e0, ParseFromBuffer<0x140373470>);
REBUILD_FUNCTION(ParseFromBuffer_14038bf90, 0x14038bf90, ParseFromBuffer<0x140373200>);
REBUILD_FUNCTION(ParseFromBuffer_14038e630, 0x14038e630, ParseFromBuffer<0x140377320>);
REBUILD_FUNCTION(ParseFromBuffer_14038b0e0, 0x14038b0e0, ParseFromBuffer<0x1403729b0>);
REBUILD_FUNCTION(ParseFromBuffer_14038c150, 0x14038c150, ParseFromBuffer<0x140373560>);
REBUILD_FUNCTION(ParseFromBuffer_14038c070, 0x14038c070, ParseFromBuffer<0x1403733a0>);
REBUILD_FUNCTION(LazyGlobal_142ae8598, 0x1402ee000, LazyGlobal142ae8598);
REBUILD_FUNCTION(FormatToBufferV, 0x14032ed90, FormatToBufferV);
REBUILD_FUNCTION(RegisterAudioStateHashes, 0x140a85750, RegisterAudioStateHashes);
REBUILD_FUNCTION(Construct_142065580, 0x14039b600, Construct142065580);
REBUILD_FUNCTION(FormatTimeOfDay, 0x14030d4b0, FormatTimeOfDay);
REBUILD_FUNCTION(CurrentSelectionName, 0x1403f5ff0, CurrentSelectionName);
REBUILD_FUNCTION(QueryLazyGlobal_142b19e60, 0x140839910, QueryLazyGlobal142b19e60);
REBUILD_FUNCTION(Construct_1420f4fb8, 0x1406fe940, Construct1420f4fb8);
REBUILD_FUNCTION(Construct_142111aa0, 0x1407e8370, Construct142111aa0);
REBUILD_FUNCTION(ToggleMemberLocked, 0x140954630, ToggleMemberLocked);
REBUILD_FUNCTION(SetFourStringsOnce, 0x14078ab10, SetFourStringsOnce);
REBUILD_FUNCTION(PoolFreeLocked, 0x140340c60, PoolFreeLocked);
REBUILD_FUNCTION(OpenReaderFile, 0x14165af80, OpenReaderFile);
REBUILD_FUNCTION(DrainBothLists, 0x1403d4c60, DrainBothLists);
REBUILD_FUNCTION(Construct_1424bb930, 0x140396ad0, Construct1424bb930);
REBUILD_FUNCTION(ReleaseGlobal_143bc51b0, 0x14077ff60, ReleaseGlobal143bc51b0);
REBUILD_FUNCTION(EntryIfOwned, 0x1403f8380, EntryIfOwned);
