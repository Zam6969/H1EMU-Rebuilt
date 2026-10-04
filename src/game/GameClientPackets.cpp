// The game client (singleton at 0x142b19780, vtable 0x1420646e0): entry
// point for zone packets coming out of the gateway tunnel. Packets are
// sequenced and timestamped; each is either dispatched now (0x1403fe210 - the
// zone opcode dispatcher) or queued in a time-ordered delay list.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <intrin.h>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Mutex.h"
#include "soeutil/String.h"

namespace rebuild::game_net {
namespace {

constexpr size_t kClientState = 0x314A8;   // pointer to the large client state block
constexpr size_t kLastPackets = 0x96D28;   // per-channel {u64 first bytes, int length}, in the state block
constexpr size_t kClockMs = 0x3B9D8;       // int
constexpr size_t kDelayedPackets = 0x3BA48;  // time-ordered list {.., head at +0x10, nodes: +8 time, +0x38 next}

struct LastPacket {
  uint8_t firstBytes[8];
  int length;
};
static_assert(sizeof(LastPacket) == 12);

uint64_t& PacketSequence() { return *reinterpret_cast<uint64_t*>(0x142b19ce8); }

// The client clock: base ms minus a scaled float offset (0x1404e3940).
int PacketTimestamp(uint8_t* game) {
  float offset = game::Call<float (*)()>(0x1404e3940)();
  float scale = *reinterpret_cast<float*>(0x1420728e0);
  return game::Field<int>(game, kClockMs) - static_cast<int>(-offset * scale);
}

}  // namespace

// 0x140430a20 (game client slot 42): HandleZonePacket(channel, header, data, length)
bool GameClientHandleZonePacket(uint8_t* game, int channel, void* header, const uint8_t* data, int length) {
  auto* last = reinterpret_cast<LastPacket*>(game::Field<uint8_t*>(game, kClientState) + kLastPackets) + channel;
  int timestamp = PacketTimestamp(game);
  uint64_t sequence = ++PacketSequence();
  bool mayQueue = true;
  using ReadyFn = bool (*)(uint8_t*, void*, const uint8_t*, int, uint64_t, int*, bool*, bool);
  bool handled;
  if (game::Call<ReadyFn>(0x14046eac0)(game, header, data, length, sequence, &timestamp, &mayQueue, true)) {
    using DispatchFn = bool (*)(uint8_t*, void*, const uint8_t*, int, int);
    handled = game::Call<DispatchFn>(0x1403fe210)(game, header, data, length, channel);  // dispatch now
  } else {
    if (mayQueue) {
      using DelayFn = int* (*)(uint8_t*, int*, void*, const uint8_t*, int);
      int when = *game::Call<DelayFn>(0x1403f7360)(game, &timestamp, header, data, length);
      // Insert before the first queued packet that is due no later than `when`.
      uint8_t* list = game + kDelayedPackets;
      uint8_t* before = game::Field<uint8_t*>(list, 0x10);
      while (before && game::Field<int>(before, 8) - when > 0) before = game::Field<uint8_t*>(before, 0x38);
      uint8_t* node = game::Call<uint8_t* (*)(uint8_t*, uint8_t*)>(0x140430d20)(list, before);
      if (node) {
        timestamp = when;
        using InitFn = void (*)(uint8_t*, uint64_t, int*, const uint8_t*, int, int);
        game::Call<InitFn>(0x14039d870)(node, sequence, &timestamp, data, length, channel);
      }
    }
    handled = false;
  }
  last->length = length;
  if (static_cast<unsigned>(length) >= 8) {
    std::memcpy(last->firstBytes, data, 8);
  } else {
    std::memcpy(last->firstBytes, data, static_cast<size_t>(length));
    std::memset(last->firstBytes + length, 0, 8 - static_cast<size_t>(length));
  }
  return handled;
}

// 0x14040b750 (game client slot 73): channel-2 packets (opcode 0x79) built by
// the zone client: forward {value, body} to slot 72.
bool GameClientHandleChannel2Packet(uint8_t* game, uint8_t* packet) {
  uint8_t* body = game::Field<uint8_t*>(packet, 0x1B0);
  if (!body) return false;
  int value = game::Field<int>(packet, 0x1B8);
  using Slot72Fn = bool (*)(uint8_t*, int*, uint8_t*, void*);
  return reinterpret_cast<Slot72Fn>((*reinterpret_cast<void***>(game))[0x240 / 8])(game, &value, body, nullptr);
}

// A numeric setting in the global settings list (*(0x142b197a0)+0x3E90).
struct NumericSetting {
  double value;
  uint64_t unknown08;
  uint64_t unknown10;
  uint32_t id;            // +0x18 (hashed name)
  uint32_t padding;
  NumericSetting* next;   // +0x20
};
static_assert(offsetof(NumericSetting, id) == 0x18 && offsetof(NumericSetting, next) == 0x20);

// 0x140430490: the zone connection came up. Unless setting 0x36713FC6 is
// non-zero (or slot 18 says otherwise) run 0x1403d5f60, then notify three
// subsystems (true = connected).
void GameClientOnZoneConnected(uint8_t* game) {
  auto* settings = *reinterpret_cast<uint8_t**>(0x142b197a0);
  bool forced = false;
  for (auto* setting = game::Field<NumericSetting*>(settings, 0x3E90); setting; setting = setting->next) {
    if (setting->id == 0x36713FC6) {
      forced = setting->value < 0.0 || setting->value > 0.0;  // ucomisd: NaN counts as zero
      break;
    }
  }
  if (forced || !reinterpret_cast<bool (*)(uint8_t*)>((*reinterpret_cast<void***>(game))[0x90 / 8])(game))
    game::Call<void (*)(uint8_t*)>(0x1403d5f60)(game);
  if (void* global = *reinterpret_cast<void**>(0x142b19d20)) game::Call<void (*)(void*, bool)>(0x1407660b0)(global, true);
  if (void* subsystem = game::Field<void*>(game, 0x389E0)) game::Call<void (*)(void*, bool)>(0x1408185f0)(subsystem, true);
  if (void* subsystem = game::Field<void*>(game, 0x38948)) game::Call<void (*)(void*, bool)>(0x140640b50)(subsystem, true);
}

// Simple game client vtable slots.
void* GameClientSlot5(uint8_t* game) { return game::Field<void*>(game, 0x312F8); }   // 0x1403f51c0
void* GameClientSlot6(uint8_t*) { return *reinterpret_cast<void**>(0x142b19d88); }  // 0x1403f5270
void* GameClientSlot7(uint8_t* game) { return game::Field<void*>(game, 0x313B8); }   // 0x1403f5320
void* GameClientSlot8(uint8_t* game) { return game::Field<void*>(game, 0x3D548); }   // 0x1403f54a0
void* GameClientSlot15(uint8_t* game) { return game::Field<void*>(game, 0x3D4D0); }  // 0x1402ecd00
void* GameClientSlot16(uint8_t* game) { return game::Field<void*>(game, 0x3D4D8); }  // 0x1402ecd60
// Slots 22 / 23: the two floats opcode 0x61 stores at +0x38DC0 / +0x38DC4.
float GameClientSlot22(uint8_t* game) { return game::Field<float>(game, 0x38DC0); }  // 0x1402ecce0
float GameClientSlot23(uint8_t* game) { return game::Field<float>(game, 0x38DC4); }  // 0x1402eccd0
// Slot 12: pure-virtual stub - deliberately writes 0xBADC0DE to address 0.
void GameClientPureVirtual() { *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(0)) = 0xBADC0DE; }  // 0x140309030
// Slot 53: stores its 3rd/4th arguments into its own home slots and returns (no effect).
void GameClientSlot53(uint8_t*, void*, void*, void*) {}  // 0x1402ece90
bool GameClientSlot14(uint8_t*) { return true; }          // 0x14046ea60

void* UiRoot() { return *reinterpret_cast<void**>(0x143c45470); }
uint64_t Now() {
  uint64_t slot;
  return *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&slot);
}

// 0x1403c1290 (slot 0): scalar deleting destructor (dtor 0x1403abd80, 0x43550 bytes).
uint8_t* GameClientDeletingDestructor(uint8_t* game, unsigned flags) {
  game::Call<void (*)(uint8_t*)>(0x1403abd80)(game);
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(game, 0x43550);
  return game;
}

// 0x14045a4f0 (slot 3): window close - slot 27 shutdown with the reason text.
void GameClientOnCloseButton(uint8_t* game) {
  using ShutdownFn = void (*)(uint8_t*, bool, int, const char*, void*);
  reinterpret_cast<ShutdownFn>((*reinterpret_cast<void***>(game))[0xD8 / 8])(
      game, false, 0, reinterpret_cast<const char*>(0x14206d2e8), nullptr);  // "Regular Shutdown likely from the close button"
}

// 0x140472d40 (slot 26): UI root 0x14048b330; always true.
bool GameClientSlot26(uint8_t*) {
  game::Call<void (*)(void*)>(0x14048b330)(UiRoot());
  return true;
}

// 0x1402ecb90 (slot 54): slot 55 with an empty request object {vt 0x142046860, 0}.
void GameClientSlot54(uint8_t* game) {
  struct Request {
    void** vtable;
    void* value;
  } request{reinterpret_cast<void**>(0x142046860), nullptr};
  reinterpret_cast<void (*)(uint8_t*, Request*, int)>((*reinterpret_cast<void***>(game))[0x1B8 / 8])(game, &request, 0);
  game::Call<void (*)(Request*)>(0x1403531d0)(&request);
}

// 0x1403fd2f0 (slot 62): UI "OnUpdate", UI tick 0x140ce8600, then 0x140cfdf30(0x1403f62a0()).
void GameClientSlot62(uint8_t*) {
  game::Call<void (*)(void*, const char*, void*, void*)>(0x140488cc0)(
      UiRoot(), *reinterpret_cast<const char**>(0x142a002f0), nullptr, nullptr);  // "OnUpdate"
  game::Call<void (*)(void*)>(0x140ce8600)(UiRoot());
  void* target = game::Call<void* (*)()>(0x1403f62a0)();
  game::Call<void (*)(void*)>(0x140cfdf30)(target);
}

// Slots 122-124: a timer pair at +0x58..+0x68 with a helper at +0x78.
void GameClientSlot122(uint8_t* self) {  // 0x141669a00: reset
  game::Call<void (*)(uint8_t*)>(0x141668150)(self + 0x78);
  game::Field<uint64_t>(self, 0x60) = *reinterpret_cast<uint64_t*>(0x143dcb000);
  game::Field<uint64_t>(self, 0x68) = *reinterpret_cast<uint64_t*>(0x143dcb000);
  game::Field<bool>(self, 0x58) = false;
  game::Field<bool>(self, 0x59) = false;
}
void GameClientSlot123(uint8_t* self) {  // 0x141669bb0: mark the first time
  game::Field<uint64_t>(self, 0x60) = Now();
  game::Field<bool>(self, 0x58) = true;
}
void GameClientSlot124(uint8_t* self) {  // 0x141669b80: mark the second time, then 0x141668520(+0x78, time)
  uint64_t now = Now();
  game::Field<uint64_t>(self, 0x68) = now;
  game::Field<bool>(self, 0x59) = true;
  game::Call<void (*)(uint8_t*, uint64_t)>(0x141668520)(self + 0x78, now);
}

void* GameAllocate(size_t size) { return game::Call<void* (*)(size_t)>(0x1402fc0f0)(size); }
void DeleteVirtual(void* object) {
  if (object) reinterpret_cast<void (*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}

// 0x14040e9f0 (slot 33): create the 0x30-byte object at +0x38DC8 (ctor 0x14083af70).
void GameClientSlot33(uint8_t* game) {
  void* memory = GameAllocate(0x30);
  game::Field<void*>(game, 0x38DC8) = memory ? game::Call<void* (*)(void*)>(0x14083af70)(memory) : nullptr;
}

// 0x140410a00 (slot 25): create the 8-byte UI hook object (ctor 0x140487980),
// store it at +0x38DD8 and on the UI root (+0x08, clearing +0x10), then 0x140ce8440(ui, 1.0).
void GameClientSlot25(uint8_t* game) {
  void* memory = GameAllocate(8);
  void* hook = memory ? game::Call<void* (*)(void*)>(0x140487980)(memory) : nullptr;
  game::Field<void*>(game, 0x38DD8) = hook;
  auto* ui = static_cast<uint8_t*>(UiRoot());
  game::Field<void*>(ui, 0x10) = nullptr;
  game::Field<void*>(ui, 8) = hook;
  game::Call<void (*)(void*, float)>(0x140ce8440)(UiRoot(), *reinterpret_cast<float*>(0x1425ba090));
}

// 0x1404690c0 (slot 47): build a string with 0x140ce9630 and pass its text to slot 48.
void GameClientSlot47(uint8_t* game) {
  soeutil::IString text;
  game::Call<void (*)(soeutil::IString*)>(0x140ce9630)(&text);
  reinterpret_cast<void (*)(uint8_t*, const char*)>((*reinterpret_cast<void***>(game))[0x180 / 8])(game, text.data);
  text.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&text);
}

// 0x1403e6aa0 (slot 79): 0x14077ff60, then delete the objects at +0x3D3C8 /
// +0x3D3C0 and clear the globals that mirror them.
void GameClientSlot79(uint8_t* game) {
  game::Call<void (*)(uint8_t*)>(0x14077ff60)(game);
  DeleteVirtual(game::Field<void*>(game, 0x3D3C8));
  game::Field<void*>(game, 0x3D3C8) = nullptr;
  *reinterpret_cast<void**>(0x142ae89a8) = nullptr;
  *reinterpret_cast<void**>(0x142b19af8) = nullptr;
  DeleteVirtual(game::Field<void*>(game, 0x3D3C0));
  game::Field<void*>(game, 0x3D3C0) = nullptr;
  *reinterpret_cast<void**>(0x142b19af0) = nullptr;
}

// 0x1403fd270 (slot 61): shutdown hooks - 0x14078b910 on *0x142b195c8, UI
// "GuiOnShutdown" if the UI handles it, then slot 5 of state+0x96A10.
void GameClientSlot61(uint8_t* game) {
  if (void* system = *reinterpret_cast<void**>(0x142b195c8)) game::Call<void (*)(void*)>(0x14078b910)(system);
  if (void* ui = UiRoot()) {
    const char* command = *reinterpret_cast<const char**>(0x142a002f8);  // "GuiOnShutdown"
    if (game::Call<bool (*)(void*, const char*)>(0x14048b2d0)(ui, command))
      game::Call<void (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), *reinterpret_cast<const char**>(0x142a002f8),
                                                                         nullptr, nullptr);
  }
  if (void* target = game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), 0x96A10))
    reinterpret_cast<void (*)(void*)>((*static_cast<void***>(target))[0x28 / 8])(target);
}

// 0x140467880 (slot 51): request {9, 0x1C, 0, player +0x2D0, +0x2D4} on
// *(0x142b19b98)+8 via 0x14035dc40 (local player not null-checked).
void GameClientSlot51(uint8_t* game) {
  struct Request {
    void** vtable;
    int a;
    int padding;
    int b;
    int padding2;
    uint64_t c;
    int d;
    int e;
  };
  static_assert(offsetof(Request, d) == 0x20 && sizeof(Request) == 0x28);
  uint8_t* player = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80);
  Request request{reinterpret_cast<void**>(0x142066420), 9, 0, 0x1C, 0, 0, game::Field<int>(player, 0x2D0),
                  game::Field<int>(player, 0x2D4)};
  void* target = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19b98), 8);
  game::Call<void (*)(void*, Request*, int, bool)>(0x14035dc40)(target, &request, 0, true);
}

// 0x140433600 (slot 84): stamp +0x38B70 with the time; unless slot 18 says
// otherwise set state 0x63 (and 0x140467900 with *0x142b176c8 when set);
// then the mover-state-0x26 follow-up.
bool GameClientSlot84(uint8_t* game) {
  game::Field<uint64_t>(game, 0x38B70) = Now();
  if (!reinterpret_cast<bool (*)(uint8_t*)>((*reinterpret_cast<void***>(game))[0x90 / 8])(game)) {
    game::Call<void (*)(uint8_t*, int)>(0x14046be80)(game, 0x63);
    if (int value = *reinterpret_cast<int*>(0x142b176c8)) game::Call<void (*)(uint8_t*, int)>(0x140467900)(game, value);
  }
  if (void* mover = game::Field<void*>(game, 0x388A8)) {
    if (reinterpret_cast<int (*)(void*)>((*static_cast<void***>(mover))[0])(mover) == 0x26 &&
        game::Field<int>(game, 0x38F48) != 4)
      game::Call<void (*)(uint8_t*)>(0x14046ab00)(game);
  }
  return true;
}

// 0x1403c1020 (slot 119): scalar deleting destructor of a 0xB0-byte object
// (helper at +0x78, shared ref-counted block at +0x08).
uint8_t* RefHolderDeletingDestructor(uint8_t* self, unsigned flags) {
  game::Call<void (*)(uint8_t*)>(0x141667f50)(self + 0x78);
  uint8_t* shared = game::Field<uint8_t*>(self, 8);
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x14204f640);
  if (shared) {
    if (_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(shared + 4), -1) == 1 && shared)
      game::Call<void (*)(void*, size_t)>(0x140d0fb84)(shared, 0x10);
    game::Field<void*>(self, 8) = nullptr;
  }
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, 0xB0);
  return self;
}

// 0x140473f60 (slot 27): fatal error / shutdown(showUi, code, message, extra).
// Logs "%s" with the message ("Unknown error" if null) - through the error UI
// path (0x1403243b0 then 0x1403e1d80) when showUi, else to the plain log.
void GameClientShutdown(uint8_t* game, bool showUi, int code, const char* message, void* extra) {
  if (void* timer = game::Field<void*>(game, 0x312D8)) game::Call<void (*)(void*, int)>(0x14032d190)(timer, 0);
  const char* text = message ? message : reinterpret_cast<const char*>(0x14206d318);  // "Unknown error"
  const char* format = reinterpret_cast<const char*>(0x142046fb8);                  // "%s"
  if (showUi) {
    game::Call<void (*)(void*, const char*, const char*)>(0x1403243b0)(nullptr, format, text);
    game::Call<void (*)(int, const char*, void*, void*)>(0x1403e1d80)(code, message, nullptr, extra);
  } else {
    game::Call<void (*)(void*, const char*, const char*)>(0x1402bab70)(nullptr, format, text);
  }
  game::Field<bool>(game, 0x38839) = false;
}

// 0x1403dd800 (slot 86): create the zone client (0x2B8 bytes) for this
// character (+0x38BF0) and gateway ticket (+0x316B0).
uint8_t* GameClientCreateZoneClient(uint8_t* game, bool threaded, bool compression) {
  void* memory = GameAllocate(0x2B8);
  if (!memory) return nullptr;
  uint64_t characterId = game::Field<uint64_t>(game, 0x38BF0);
  using ConstructFn = uint8_t* (*)(void*, uint64_t*, const char*, bool, bool, int, void*, int);
  return game::Call<ConstructFn>(0x14063cc10)(memory, &characterId, game::Field<const char*>(game, 0x316B0), threaded,
                                              compression, 0, nullptr, 1);
}

// 0x1404677e0 (slot 67): SendMountRequest(&target, seat, flag) - logged to
// "#ClientMountLog.txt", then request 0x71 through *(0x142b19b98)+8.
void GameClientSendMountRequest(uint8_t* game, const uint64_t* target, int seat, bool flag) {
  game::Call<void (*)(const char*, const char*, uint64_t, uint64_t)>(0x1402bab70)(
      reinterpret_cast<const char*>(0x142070228), reinterpret_cast<const char*>(0x1420701f0),
      game::Field<uint64_t>(game, 0x38BF0), *target);
  struct MountRequest {
    void** vtable;
    int type;     // 0x71
    int padding;
    int version;  // 1
    int padding2;
    uint64_t target;
    int seat;
    bool unknown;
    bool flag;
  } request{reinterpret_cast<void**>(0x142065c58), 0x71, 0, 1, 0, *target, seat, false, flag};
  static_assert(offsetof(MountRequest, seat) == 0x20 && offsetof(MountRequest, flag) == 0x25);
  void* sender = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19b98), 8);
  game::Call<void (*)(void*, MountRequest*, int, bool)>(0x14035f730)(sender, &request, 0, true);
}

// 0x1403e9c70 (slot 121): inside a profiler scope "Asset Handler"
// (*0x142b06d58 slots 1/2), poll *0x142ae89a8 slot 4 with (+0xA8, 10, true).
void AssetHandlerUpdate(uint8_t* self) {
  void* profiler = *reinterpret_cast<void**>(0x142b06d58);
  bool scoped = false;
  if (profiler) {
    using BeginFn = void (*)(void*, const char*, uint32_t, void*);
    reinterpret_cast<BeginFn>((*static_cast<void***>(profiler))[1])(profiler, reinterpret_cast<const char*>(0x142064ac8),
                                                                    0x5192A6FA, nullptr);  // "Asset Handler"
    scoped = true;
  }
  if (void* assets = *reinterpret_cast<void**>(0x142ae89a8)) {
    using PollFn = void (*)(void*, int, int, bool);
    reinterpret_cast<PollFn>((*static_cast<void***>(assets))[0x20 / 8])(assets, game::Field<int>(self, 0xA8), 10, true);
  }
  profiler = *reinterpret_cast<void**>(0x142b06d58);
  if (scoped && profiler) reinterpret_cast<void (*)(void*)>((*static_cast<void***>(profiler))[2])(profiler);
}

// 0x140410940 (slot 32): switch the localized string table to `locale`.
// Recreates the 0x28-byte table (ctor 0x14047fe50 with state+0x1C8) and
// publishes it at +0x3B6B8, *0x142b19c38 and *0x142b19798 (the table every
// string lookup uses). Returns false when that locale is already loaded.
bool GameClientSetLocale(uint8_t* game, int locale) {
  if (game::Field<void*>(game, 0x3B6B8) && locale == game::Field<int>(game, 0x38E40)) return false;
  game::Field<int>(game, 0x38E40) = locale;
  auto& current = *reinterpret_cast<void**>(0x142b19c38);
  DeleteVirtual(current);
  current = nullptr;
  void* table = nullptr;
  if (void* memory = GameAllocate(0x28))
    table = game::Call<void* (*)(void*, int, uint8_t*)>(0x14047fe50)(memory, locale,
                                                                      game::Field<uint8_t*>(game, 0x314A8) + 0x1C8);
  game::Field<void*>(game, 0x3B6B8) = table;
  current = table;
  *reinterpret_cast<void**>(0x142b19798) = game::Field<void*>(game, 0x3B6B8);
  return true;
}

// 0x14034e6f0 (slot 13): create the 0x8220-byte object at +0x31418 (ctor
// 0x14034e8b0 with +0x3142C, +0x31428, count, data), build +0x31410 from it
// (0x1403519e0 with slots 5 and 8), hand that back (its slot 16), then 0x140351920.
bool GameClientSlot13(uint8_t* game, void* /*unused*/, void* data, int count) {
  void* created = nullptr;
  if (void* memory = GameAllocate(0x8220)) {
    using CtorFn = void* (*)(void*, uint8_t, int, int, void*);
    created = game::Call<CtorFn>(0x14034e8b0)(memory, game::Field<uint8_t>(game, 0x3142C), game::Field<int>(game, 0x31428),
                                              count, data);
  }
  game::Field<void*>(game, 0x31418) = created;
  void** vtable = *reinterpret_cast<void***>(game);
  void* a = reinterpret_cast<void* (*)(uint8_t*)>(vtable[0x28 / 8])(game);
  void* b = reinterpret_cast<void* (*)(uint8_t*)>(vtable[0x40 / 8])(game);
  using BuildFn = void* (*)(uint8_t*, uint8_t*, void*, void*, void*);
  void* built = game::Call<BuildFn>(0x1403519e0)(game + 0x31480, game + 0x31420, game::Field<void*>(game, 0x31418), b, a);
  game::Field<void*>(game, 0x31410) = built;
  void* object = game::Field<void*>(game, 0x31418);
  reinterpret_cast<void (*)(void*, void*)>((*static_cast<void***>(object))[0x80 / 8])(object, built);
  game::Call<void (*)(void*, uint8_t*)>(0x140351920)(game::Field<void*>(game, 0x31418), game);
  return true;
}

// Request 0xE1 {guid, id} for an entity the client does not have yet.
struct EntityRequest {
  void** vtable;
  int type;  // 0xE1
  int padding;
  uint64_t guid;  // +0x10
  int id;         // +0x18
  int padding2;
};
static_assert(offsetof(EntityRequest, id) == 0x18);

void SendEntityRequest(uint64_t guid, int id) {
  EntityRequest request{reinterpret_cast<void**>(0x142063e38), 0xE1, 0, guid, id, 0};
  void* sender = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19b98), 8);
  game::Call<void (*)(void*, EntityRequest*, int, bool)>(0x14035fe20)(sender, &request, 0, true);
}

// 0x1404089c0 (slot 72): act on entity *id via 0x1404193a0 when it exists;
// otherwise, if 0x14071f880 accepts the id, send request 0xE1 for it.
bool GameClientSlot72(uint8_t* game, const int* id, void* data, bool flag) {
  void* entities = game::Field<void*>(game, 0x38860);
  int key = *id;
  if (void* entity = game::Call<void* (*)(void*, int*)>(0x14071f100)(entities, &key))
    return game::Call<bool (*)(uint8_t*, void*, void*, bool)>(0x1404193a0)(game, entity, data, flag);
  key = *id;
  if (game::Call<bool (*)(void*, int*)>(0x14071f880)(game::Field<void*>(game, 0x38860), &key))
    SendEntityRequest(*reinterpret_cast<uint64_t*>(0x142b181f8), *id);
  return true;
}

// 0x140408a90 (slot 71): slot 72 for a 64-bit id (lookup 0x14071f150 /
// 0x14071f950; the request carries the id as the guid and *0x142b186ac).
bool GameClientSlot71(uint8_t* game, const uint64_t* id, void* data, bool flag) {
  uint64_t key = *id;
  if (void* entity = game::Call<void* (*)(void*, uint64_t*)>(0x14071f150)(game::Field<void*>(game, 0x38860), &key))
    return game::Call<bool (*)(uint8_t*, void*, void*, bool)>(0x1404193a0)(game, entity, data, flag);
  key = *id;
  if (game::Call<bool (*)(void*, uint64_t*)>(0x14071f950)(game::Field<void*>(game, 0x38860), &key))
    SendEntityRequest(*id, *reinterpret_cast<int*>(0x142b186ac));
  return true;
}

// 0x1403dc8c0 (slot 87): create the 0x2468-byte object at +0x388C8 named
// `name` (ctor 0x1409bc270 takes a temporary IString).
void GameClientSlot87(uint8_t* game, const char* name) {
  void* created = nullptr;
  if (void* memory = GameAllocate(0x2468)) {
    soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    soeutil::StringAssign(&text, name);
    created = game::Call<void* (*)(void*, soeutil::IString*)>(0x1409bc270)(memory, &text);
    game::Field<void*>(game, 0x388C8) = created;
    text.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&text);
    return;
  }
  game::Field<void*>(game, 0x388C8) = created;
}

// 0x14040ec00 (slot 28): store the login details - character id, three
// strings (null -> ""; the third, at +0x316A8, is the gateway ticket), a
// fresh Crypto::ArraySecure session key (0x60 bytes) and an int.
void GameClientSetLoginInfo(uint8_t* game, void* /*unused*/, const uint64_t* characterId, const char* name,
                            const char* server, const char* ticket, const void* key, int keyType) {
  const char* empty = reinterpret_cast<const char*>(0x142046fcb);
  game::Field<uint64_t>(game, 0x38BF0) = *characterId;
  soeutil::StringAssign(game + 0x38C00, name ? name : empty);
  soeutil::StringAssign(game + 0x31608, server ? server : empty);
  soeutil::StringAssign(game + 0x316A8, ticket ? ticket : empty);
  if (void* old = game::Field<void*>(game, 0x38BE0)) {
    game::Call<void (*)(void*)>(0x1415f9be0)(old);  // ~ArraySecure
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(old, 0x60);
  }
  void* memory = GameAllocate(0x60);
  void* secure = memory ? game::Call<void* (*)(void*)>(0x1415f9ac0)(memory) : nullptr;
  game::Field<void*>(game, 0x38BE0) = secure;
  game::Call<void (*)(void*, const void*)>(0x1415f9e40)(secure, key);  // ArraySecure::Assign (not null-checked)
  game::Field<int>(game, 0x38BE8) = keyType;
}

// Slots 45 / 46: take the next console line from the console object at
// +0x388C8 (two different queues), echo it as "EVENT_PRINT_CONSOLE_INPUT" to
// the console UI (*0x143bd4830), then run it (slot 47).
void EchoAndRunConsoleLine(uint8_t* game, uintptr_t take) {
  auto* item = game::Call<uint8_t* (*)(void*)>(take)(game::Field<void*>(game, 0x388C8));
  if (!item) return;
  if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
    void* line = game::Field<void*>(item, 8);
    const char* event = reinterpret_cast<const char*>(0x14206e5e0);  // "EVENT_PRINT_CONSOLE_INPUT"
    soeutil::IString name{soeutil::IStringVtable(), const_cast<char*>(event), static_cast<int>(std::strlen(event)), -1};
    game::Call<void (*)(void*, soeutil::IString*, void**)>(0x140358900)(console, &name, &line);
    name.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&name);  // literal (capacity -1): nothing to free
  }
  reinterpret_cast<void (*)(uint8_t*, void*)>((*reinterpret_cast<void***>(game))[0x178 / 8])(game, game::Field<void*>(item, 8));
}
void GameClientSlot45(uint8_t* game) { EchoAndRunConsoleLine(game, 0x1409d6f20); }  // 0x14046ceb0
void GameClientSlot46(uint8_t* game) { EchoAndRunConsoleLine(game, 0x1409d6bc0); }  // 0x14046c250

int SettingInt(uintptr_t listOffset, uint32_t id, int fallback) {
  auto* settings = *reinterpret_cast<uint8_t**>(0x142b197a0);
  for (auto* setting = game::Field<NumericSetting*>(settings, listOffset); setting; setting = setting->next)
    if (setting->id == id) return static_cast<int>(setting->value);
  return fallback;
}

// 0x1403d33f0 (slot 114): idle check. Idle time since +0x3B7E8 (kept at
// +0x3B7F4) against setting 0x14842C99 (unlimited when *0x142b176cc), and
// since +0x3B7E0 against setting 0xA5DEE4E9; exceeding either calls
// 0x14047b7b0(game, first?).
void GameClientCheckIdle(uint8_t* game) {
  uint64_t slot;
  uint32_t lastA = static_cast<uint32_t>(game::Field<uint64_t>(game, 0x3B7E8));
  game::Field<int>(game, 0x3B7F4) = static_cast<int>(*reinterpret_cast<uint32_t*>(game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&slot)) - lastA);
  uint32_t lastB = static_cast<uint32_t>(game::Field<uint64_t>(game, 0x3B7E0));
  int idleB = static_cast<int>(*reinterpret_cast<uint32_t*>(game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&slot)) - lastB);
  int limitA = *reinterpret_cast<bool*>(0x142b176cc) ? 0x7FFFFFFF : SettingInt(0x2528, 0x14842C99, 0);
  int limitB = SettingInt(0x27A8, 0xA5DEE4E9, 0);
  using IdleFn = void (*)(uint8_t*, bool);
  if (limitA != 0 && game::Field<int>(game, 0x3B7F4) > limitA) {
    game::Call<IdleFn>(0x14047b7b0)(game, true);
    return;
  }
  if (limitB != 0 && idleB > limitB) game::Call<IdleFn>(0x14047b7b0)(game, false);
}

// 0x1403fd180 (slot 60): client init - post "EVENT_CLIENT_INIT" to the
// console UI, then run the UI's "GuiOnInit" if it has one, else open "Main".
void GameClientOnInit(uint8_t* game) {
  if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
    const char* event = reinterpret_cast<const char*>(0x14206ebb8);  // "EVENT_CLIENT_INIT"
    soeutil::IString name{soeutil::IStringVtable(), const_cast<char*>(event), static_cast<int>(std::strlen(event)), -1};
    game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(console, &name, nullptr, nullptr);
    name.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&name);
  }
  const char* onInit = reinterpret_cast<const char*>(0x14206ba40);  // "GuiOnInit"
  if (!game::Call<bool (*)(void*, const char*)>(0x14048b2d0)(UiRoot(), onInit))
    game::Call<void (*)(uint8_t*, const char*)>(0x1403fcfc0)(game, reinterpret_cast<const char*>(0x14206ebcc));  // "Main"
  else
    game::Call<void (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), onInit, nullptr, nullptr);
}

// 0x140408340 (slot 98): a server-side event packet (opcode 0x14) - stores
// its value at +0x3B7FC; type 0x7D runs 0x14046fde0, type 0x7E hides the
// respawn window, anything else goes to the console object (+0x388C8, slot 5).
void GameClientHandleEvent(uint8_t* game, const uint8_t* data, int length) {
  struct EventPacket {
    void** vtable;
    int opcode;
    int padding;
    int type;   // +0x10
    int padding2;
    int value;  // +0x18
    int padding3;
  } packet{reinterpret_cast<void**>(0x1420682a8), 0x14, 0, 0, 0, 0, 0};
  static_assert(offsetof(EventPacket, value) == 0x18);
  if (!data) return;
  struct Reader {
    const uint8_t* start;
    int length;
    const uint8_t* cursor;
    const uint8_t* end;
    uint16_t failed;
  } reader{data, length, data, data + length, 0};
  game::Call<void (*)(EventPacket*, Reader*)>(0x1403658b0)(&packet, &reader);
  if (static_cast<uint8_t>(reader.failed)) return;
  game::Field<int>(game, 0x3B7FC) = packet.value;
  if (packet.type == 0x7D) {
    game::Call<void (*)(uint8_t*)>(0x14046fde0)(game);
  } else if (packet.type == 0x7E) {
    game::Call<void (*)(void*, const char*, void*, void*)>(0x140488cc0)(
        UiRoot(), reinterpret_cast<const char*>(0x14206f3b0), nullptr, nullptr);  // "RespawnWindow:Hide"
    game::Call<void (*)(uint8_t*)>(0x14047bc30)(game);
    if (void* respawn = game::Field<void*>(game, 0x388A0)) game::Call<void (*)(void*, bool)>(0x1406145e0)(respawn, true);
  } else {
    void* console = game::Field<void*>(game, 0x388C8);
    reinterpret_cast<void (*)(void*, const uint8_t*, int)>((*static_cast<void***>(console))[0x28 / 8])(console, data, length);
  }
}

// 0x14040b790 (slot 92): packet opcode 0x93 for an object by id - read
// {id, i8, u8, u8, u64, int} and pass it to the object's slot-83 component
// via 0x1406c8990. False if unreadable or the object/component is missing.
bool GameClientHandlePacket93(uint8_t* game, const uint8_t* data, int length) {
  struct Packet93 {
    void** vtable;
    int opcode;
    int padding;
    int id;               // +0x10
    int8_t a;             // +0x14
    uint8_t b;            // +0x15
    uint8_t c;            // +0x16
    uint8_t padding2;
    uint64_t d;           // +0x18
    int e;                // +0x20
    int padding3;
  } packet{reinterpret_cast<void**>(0x142064320), 0x93, 0, *reinterpret_cast<int*>(0x142b186ac), 0, 0, 0, 0, 0, 0, 0};
  static_assert(offsetof(Packet93, d) == 0x18 && sizeof(Packet93) == 0x28);
  if (!data) return false;
  struct Reader {
    const uint8_t* start;
    int length;
    const uint8_t* cursor;
    const uint8_t* end;
    uint16_t failed;
  } reader{data, length, data, data + length, 0};
  game::Call<void (*)(Packet93*, Reader*)>(0x140367dd0)(&packet, &reader);
  if (static_cast<uint8_t>(reader.failed) || static_cast<int>(reader.end - reader.cursor) > 0) return false;
  int id = packet.id;
  auto* object = game::Call<uint8_t* (*)(uint8_t*, int*)>(0x1403f8380)(game, &id);
  if (!object) return false;
  void* component = reinterpret_cast<void* (*)(uint8_t*)>((*reinterpret_cast<void***>(object))[0x298 / 8])(object);
  if (!component) return false;
  game::Call<void (*)(void*, int, uint8_t, uint8_t, uint64_t*)>(0x1406c8990)(component, packet.a, packet.b, packet.c, &packet.d);
  return true;
}

// 0x140470b70 (slot 34): close the UI modules ("App.UI.Modules",
// "App.UI.EditModuleContents"), delete the object at +0x38DC8, clear the
// state block's +0x1C0 entry and destroy the extension root (*0x142b19cc0).
void GameClientShutdownUi(uint8_t* game) {
  auto closeModule = [](const char* name) {
    using HashFn = uint32_t (*)(const char*, int, void*, void*, void*, void*);
    uint32_t hash = game::Call<HashFn>(0x1402ee3a0)(name, -1, nullptr, nullptr, nullptr, nullptr) & 0x7FFFFFFF;
    game::Call<void (*)(void*, uint64_t, void*)>(0x140ce84b0)(UiRoot(), hash, nullptr);
  };
  if (UiRoot()) {
    closeModule(reinterpret_cast<const char*>(0x14206d2b8));  // "App.UI.Modules"
    closeModule(reinterpret_cast<const char*>(0x14206d2c8));  // "App.UI.EditModuleContents"
  }
  if (void* object = game::Field<void*>(game, 0x38DC8)) {
    game::Call<void (*)(void*)>(0x1403b3390)(object);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, 0x30);
  }
  game::Field<void*>(game, 0x38DC8) = nullptr;
  game::Call<void (*)(uint8_t*)>(0x1403d4610)(game::Field<uint8_t*>(game, 0x314A8) + 0x1C0);
  auto& root = *reinterpret_cast<void**>(0x142b19cc0);
  if (root) {
    game::Call<void (*)(void*)>(0x1407b4b00)(root);
    DeleteVirtual(root);
    root = nullptr;
  }
}

// 0x14043d480 (slot 10): input event. Updates the cursor-mode state from
// event 0x21, stamps the last-input time (+0x3B7E8), then (when input is
// enabled, +0x38DED) offers the event to *0x142b19c90, *0x142b19b00 and
// finally *0x142b1f8f8; event 0x14 also feeds the recorder at +0x3B668.
bool GameClientHandleInput(uint8_t* game, uint8_t* event) {
  bool focus = game::Call<bool (*)(void*)>(0x140615d60)(game::Field<void*>(game, 0x388A0));
  game::Call<void (*)(void*, bool)>(0x140cf6220)(game::Field<void*>(game, 0x38AC8), focus);
  if (game::Field<int>(event, 0) == 0x21) {
    game::Field<int>(game, 0x3D538) = game::Field<int>(event, 0x28);
    game::Field<int>(game, 0x3D53C) = game::Field<int>(event, 0x2C);
    uint8_t mode = game::Field<uint8_t>(game, 0x3D540);
    if (mode == 0 && game::Field<int>(event, 0x38) != 0) {
      game::Field<uint16_t>(game, 0x3D540) = 0x101;
    } else if (mode == 1 && game::Field<int>(event, 0x38) == 0) {
      game::Field<uint8_t>(game, 0x3D540) = 0;
      game::Field<uint8_t>(game, 0x3D542) = 1;
    }
  }
  uint64_t slot;
  game::Field<uint64_t>(game, 0x3B7E8) = *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&slot);
  if (game::Field<bool>(game, 0x38DED)) {
    bool skip = false;
    if (void* first = *reinterpret_cast<void**>(0x142b19c90)) {
      int context = game::Field<int>(game::Field<uint8_t*>(game, 0x388A0), 0x124C0);
      skip = !game::Call<bool (*)(void*, uint8_t*, int)>(0x1413937a0)(first, event, context);
    }
    if (!skip) {
      void* second = *reinterpret_cast<void**>(0x142b19b00);
      if (!(second && game::Call<bool (*)(void*, uint8_t*)>(0x140915980)(second, event)))
        game::Call<void (*)(void*, uint8_t*, bool)>(0x1404a8730)(*reinterpret_cast<void**>(0x142b1f8f8), event, true);
    }
    if (game::Field<int>(event, 0) == 0x14 && game::Field<bool>(game, 0x3B65E) && game::Field<uint8_t>(game, 0x3B65F) >= 0x80)
      game::Call<void (*)(uint8_t*, uint8_t*)>(0x140359dc0)(game + 0x3B668, event + 0x20);
  }
  return true;
}

// 0x14040b600 (slot 74): an entity update for an entity that belongs to us
// (0x1404735e0 in its owner chain) and is not in the hash at +0x3B838:
// store the byte at +0x5B0 and run its slot-53 builder + 0x1404fd1c0.
bool GameClientSlot74(uint8_t* game, uint8_t* update) {
  int key = game::Field<int>(update, 0x10);
  auto* entity = game::Call<uint8_t* (*)(void*, int*)>(0x14071f100)(game::Field<void*>(game, 0x38860), &key);
  uint8_t* owned = nullptr;
  if (entity) {
    void* self = game::Call<void* (*)()>(0x1404735e0)();
    uint8_t* chain = entity + 0x20;
    for (void* node = reinterpret_cast<void* (*)(uint8_t*)>((*reinterpret_cast<void***>(chain))[0])(chain); node;
         node = *static_cast<void**>(node)) {
      if (node == self) {
        owned = entity;
        break;
      }
    }
  }
  if (!owned) return true;
  uint8_t* identity = owned + 0x630;
  uint64_t idSlot[2];
  uint64_t id = *reinterpret_cast<uint64_t* (*)(uint8_t*, uint64_t*)>((*reinterpret_cast<void***>(identity))[0x68 / 8])(identity, idSlot);
  for (auto* node = game::Field<uint8_t*>(game, 0x3B838 + ((static_cast<uint32_t>(id >> 32) ^ static_cast<uint32_t>(id)) & 0x1F) * 8);
       node; node = game::Field<uint8_t*>(node, 0x28)) {
    if (game::Field<uint64_t>(node, 0x20) == id) return false;
  }
  game::Field<uint8_t>(owned, 0x5B0) = update[0x14];
  alignas(16) uint8_t body[0x1A0];
  game::Call<void (*)(uint8_t*)>(0x1417f2640)(body);
  if (!reinterpret_cast<bool (*)(uint8_t*, uint8_t*)>((*reinterpret_cast<void***>(owned))[0x1A8 / 8])(owned, body)) {
    game::Call<void (*)(uint8_t*)>(0x1417f2750)(body);
    return false;
  }
  game::Call<void (*)(uint8_t*, uint8_t)>(0x1417f52b0)(body, update[0x14]);
  game::Call<void (*)(uint8_t*, uint8_t*)>(0x1404fd1c0)(owned, body);
  game::Call<void (*)(uint8_t*)>(0x1417f2750)(body);
  return true;
}

// 0x140408b70 (slot 102): packet 0x6F - server/world name (+0x10) and a
// second string (+0x70 -> game+0x3D558). Publishes the name, reloads the
// image resources and creates the 0xF8-byte object at +0x3B948.
void GameClientHandlePacket6F(uint8_t* game, const uint8_t* data, int length) {
  struct Packet6F {
    void** vtable;
    int opcode;
    int padding;
    soeutil::IString name;    // +0x10 (vtable 0x142049d00)
    uint8_t body[0x48];       // filled by the reader
    soeutil::IString second;  // +0x70
    uint8_t* target;          // +0x88 = game+0x3D570
  };
  static_assert(offsetof(Packet6F, second) == 0x70 && offsetof(Packet6F, target) == 0x88);
  Packet6F packet;
  packet.vtable = reinterpret_cast<void**>(0x142063db0);
  packet.opcode = 0x6F;
  packet.name = {reinterpret_cast<void**>(0x142049d00), soeutil::EmptyStringData(), 0, 0};
  packet.second = {soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  packet.target = game + 0x3D570;
  if (game::Call<bool (*)(Packet6F*, const uint8_t*, int, bool)>(0x14038b620)(&packet, data, length, true)) {
    void* listener = *reinterpret_cast<void**>(0x142b19ca0);
    reinterpret_cast<void (*)(void*, const char*)>((*static_cast<void***>(listener))[1])(listener, packet.name.data);
    soeutil::StringAssignString(reinterpret_cast<soeutil::IString*>(game + 0x3D558), &packet.second);
    game::Call<void (*)(uint8_t*, bool)>(0x1403d7290)(game, true);
    game::Call<void (*)(void*, const char*)>(0x1407c8580)(game::Field<void*>(game, 0x38AF0),
                                                          reinterpret_cast<const char*>(0x142070190));  // "Resources/Images"
    game::Call<void (*)(void*, const char*)>(0x1418689a0)(game::Field<void*>(game, 0x3B6F8), packet.name.data);
    void* memory = *reinterpret_cast<uint64_t*>(0x143e09638) == 0
                       ? game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(0xF8, reinterpret_cast<const void*>(0x143c46658))
                       : soeutil::MemoryAllocate(0xF8, 0);
    void* created = memory ? game::Call<void* (*)(void*)>(0x1406fe940)(memory) : nullptr;
    game::Field<void*>(game, 0x3B948) = created;
    reinterpret_cast<void (*)(void*, void*, void*)>((*static_cast<void***>(created))[0x30 / 8])(
        created, game::Field<void*>(game, 0x38830), nullptr);
  }
  game::Call<void (*)(Packet6F*)>(0x1403b0a30)(&packet);
}

// Bounds-checked reader used by the game client's packet handlers.
struct ClientReader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(ClientReader, failed) == 0x20);

// 0x140409d50 (slot 100): packet 0x42 sub-type 2 - a blob that is parsed
// into five sections of the object at +0x388F0 (between its begin
// 0x141701fa0 and end 0x141704320 calls).
void GameClientHandlePacket42(uint8_t* game, const uint8_t* data, int length) {
  if (!data) return;
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 1;
  bool failed = false;
  if (cursor > end) {
    failed = true;
    cursor = end;
  }
  if (cursor + 2 > end) return;
  int subtype = *reinterpret_cast<const int16_t*>(cursor);
  if (failed || subtype != 2) return;
  struct Packet42 {
    void** vtable;
    int opcode;
    int padding;
    int subtype;          // +0x10
    int padding2;
    const uint8_t* blob;  // +0x18
    int blobLength;       // +0x20
    int padding3;
  } packet{reinterpret_cast<void**>(0x142068588), 0x42, 0, subtype, 0, nullptr, 0, 0};
  static_assert(offsetof(Packet42, blobLength) == 0x20);
  ClientReader reader{data, length, data, end, 0};
  game::Call<void (*)(Packet42*, ClientReader*)>(0x140371260)(&packet, &reader);
  if (static_cast<uint8_t>(reader.failed) || static_cast<int>(reader.end - reader.cursor) > 0) return;
  game::Call<void (*)(void*)>(0x141701fa0)(game::Field<void*>(game, 0x388F0));
  ClientReader blob{packet.blob, packet.blobLength, packet.blob, packet.blob + packet.blobLength, 0};
  auto* target = game::Field<uint8_t*>(game, 0x388F0);
  using SectionFn = void (*)(ClientReader*, uint8_t*);
  game::Call<SectionFn>(0x140385b20)(&blob, target + 0x238);
  game::Call<SectionFn>(0x14037f580)(&blob, target + 0x78);
  game::Call<SectionFn>(0x14037f910)(&blob, target + 0xE8);
  game::Call<SectionFn>(0x14037f700)(&blob, target + 0x158);
  game::Call<SectionFn>(0x14037f3f0)(&blob, target + 0x1C8);
  game::Call<void (*)(void*)>(0x141704320)(game::Field<void*>(game, 0x388F0));
}

// 0x14040bee0 (slot 94): packet 0xE2 {opcode, i16 sub-type, int}. Sub-type 1
// carries a 0xC0-byte record (three strings) for 0x1406443a0, sub-type 2 a
// small one for 0x1406445c0; both go to the system at *0x142b19c78.
void GameClientHandlePacketE2(uint8_t* /*game*/, const uint8_t* data, int length) {
  if (!data) return;
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 1;
  bool failed = false;
  if (cursor > end) {
    failed = true;
    cursor = end;
  }
  int subtype = 0;
  if (cursor + 2 > end) {
    failed = true;
    cursor = end;
  } else {
    subtype = *reinterpret_cast<const int16_t*>(cursor);
    cursor += 2;
  }
  if (cursor + 4 > end || failed) return;
  if (subtype == 2) {
    struct Small {
      void** vtable;
      int opcode;
      int padding;
      uint64_t subtype;  // +0x10
      uint64_t value;    // +0x18
    } packet{reinterpret_cast<void**>(0x14206b710), 0xE2, 0, 2, 0};
    if (game::Call<bool (*)(Small*, const uint8_t*, int, bool)>(0x14038e540)(&packet, data, length, false))
      game::Call<void (*)(void*, Small*)>(0x1406445c0)(*reinterpret_cast<void**>(0x142b19c78), &packet);
  } else if (subtype == 1) {
    alignas(8) uint8_t packet[0xC0] = {};
    game::Field<void*>(packet, 0) = reinterpret_cast<void*>(0x14206b708);
    game::Field<int>(packet, 8) = 0xE2;
    game::Field<uint64_t>(packet, 0x10) = 1;
    static const size_t kStrings[] = {0x38, 0x58, 0x78};
    for (size_t at : kStrings)
      *reinterpret_cast<soeutil::IString*>(packet + at) = {soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    game::Field<int>(packet, 0x94) = -1;
    ClientReader reader{data, length, data, end, 0};
    game::Call<void (*)(uint8_t*, ClientReader*)>(0x140376380)(packet, &reader);
    if (!static_cast<uint8_t>(reader.failed) && static_cast<int>(reader.end - reader.cursor) <= 0)
      game::Call<void (*)(void*, uint8_t*)>(0x1406443a0)(*reinterpret_cast<void**>(0x142b19c78), packet);
    game::Call<void (*)(uint8_t*)>(0x1403b21b0)(packet);
  }
}

// 0x1404307d0 (slot 91): log(level, channel, format, va_list). Channels
// starting with '#' or "Tcp" are also sent to the server as packet 0x48
// {StringFixed<64> channel, StringFixed<2048> text}; "#name|local" sends
// as "name" and then logs locally as "local" (an empty name uses "local"
// for both). Local logging goes to the console (+0x10, when its level is
// high enough) and the log at +0x31E50. Serialized by the lock at +0x31E08.
void GameClientLog(uint8_t* game, int level, const char* channel, const char* format, void* args) {
  auto* mutex = reinterpret_cast<CRITICAL_SECTION*>(game + 0x31E08);
  soeutil::MutexLock(mutex);
  void* recorder = *reinterpret_cast<void**>(0x142b19b98);
  bool logLocally = true;
  if (channel && (channel[0] == '#' || std::strncmp(channel, reinterpret_cast<const char*>(0x14206c33c), 3) == 0)) {  // "Tcp"
    const char* rest = nullptr;
    if (channel[0] == '#') ++channel;
    int length = static_cast<int>(std::strlen(channel));
    const char* bar = channel;
    while (*bar && *bar != '|') ++bar;
    if (*bar == '|') {
      rest = bar + 1;
      length = static_cast<int>(bar - channel);
      if (length == 0) {
        channel = rest;
        length = static_cast<int>(std::strlen(rest));
      }
    }
    if (recorder) {
      struct LogPacket {
        void** vtable;
        int opcode;  // 0x48
        int padding;
        soeutil::StringFixed<64> channel;  // +0x10
        soeutil::StringFixed<2048> text;   // +0x70
      };
      static_assert(offsetof(LogPacket, text) == 0x70);
      LogPacket packet;
      packet.vtable = reinterpret_cast<void**>(0x142063c50);
      packet.opcode = 0x48;
      packet.channel.vtable = reinterpret_cast<void**>(0x142049d00);
      packet.channel.data = soeutil::EmptyStringData();
      packet.channel.length = 0;
      packet.channel.capacity = 0;
      packet.text.vtable = reinterpret_cast<void**>(0x142049e48);
      packet.text.data = soeutil::EmptyStringData();
      packet.text.length = 0;
      packet.text.capacity = 0;
      soeutil::StringAssignN(&packet.channel, channel, length);
      game::Call<void (*)(soeutil::IString*, const char*, void*)>(0x1402be760)(&packet.text, format, args);  // FormatV
      game::Call<void (*)(void*, LogPacket*, int, bool)>(0x14035f010)(game::Field<void*>(static_cast<uint8_t*>(recorder), 8),
                                                                      &packet, 0, true);
      game::Call<void (*)(LogPacket*)>(0x1403b0780)(&packet);
    }
    if (!rest) {
      logLocally = false;
    } else {
      channel = *rest ? rest : nullptr;
    }
  }
  if (logLocally) {
    if (void* console = game::Field<void*>(game, 0x10)) {
      if (game::Call<int (*)(void*)>(0x14030e1e0)(console) >= level)
        game::Call<void (*)(void*, int, const char*, const char*, void*)>(0x14030f070)(console, level, channel, format, args);
    }
    game::Call<void (*)(uint8_t*, const char*, const char*, void*, int)>(0x1416ceda0)(game + 0x31E50, channel, format, args,
                                                                                      level);
  }
  soeutil::MutexUnlock(mutex);
}

// 0x14040e7a0 (slot 81): read the client options from the ini at +0x38E30
// (global section ""): Country ("US"), LoadingScreenId, SpecialLoadingScreenId,
// LiveGamer (true), Steam_Enabled, UseNewUI, ShowSplashScreens (true).
void GameClientLoadOptions(uint8_t* game) {
  const char* section = reinterpret_cast<const char*>(0x142046fcb);  // ""
  auto ini = [&] { return game::Field<void*>(game, 0x38E30); };
  soeutil::StringFixed<8> country;
  country.vtable = reinterpret_cast<void**>(0x1424b9f98);
  country.data = soeutil::EmptyStringData();
  country.length = 0;
  country.capacity = 0;
  using GetStringFn = void (*)(void*, const char*, const char*, const char*, soeutil::IString*, bool, int, int);
  game::Call<GetStringFn>(0x1403334f0)(ini(), section, reinterpret_cast<const char*>(0x14206ced8),
                                       reinterpret_cast<const char*>(0x14206bad8), &country, false, -1, -1);  // "Country", "US"
  using GetIntFn = int (*)(void*, const char*, const char*, int, bool, int, int);
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  int loadingScreen = game::Call<GetIntFn>(0x1403050e0)(ini(), section, reinterpret_cast<const char*>(0x14206cee0), -1, false, -1, -1);
  int specialScreen = game::Call<GetIntFn>(0x1403050e0)(ini(), section, reinterpret_cast<const char*>(0x14206cef0), -1, false, -1, -1);
  bool liveGamer = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206cf08), true, false, -1, -1);
  bool steam = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206cf18), false, false, -1, -1);
  bool newUi = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206cf28), false, false, -1, -1);
  bool splash = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206cf38), true, false, -1, -1);
  game::Field<int>(game, 0x38E80) = loadingScreen;
  game::Field<int>(game, 0x38E84) = specialScreen;
  soeutil::StringAssign(game + 0x38E88, country.data);
  game::Field<bool>(game, 0x38EB0) = liveGamer;
  game::Field<bool>(game, 0x38EB1) = steam;
  game::Field<bool>(game, 0x38EB2) = newUi;
  game::Field<bool>(game, 0x38EB4) = splash;
  country.vtable = reinterpret_cast<void**>(0x1424b9ee0);  // IStringFixed<char,8>
  soeutil::StringRelease(&country);
}

// 0x1404106d0 (slot 80): start logging - open "H1Z1.log" in the log folder
// (default "./Logs", optionally wiped first), apply the log settings, and
// write the startup lines (version, command line, launcher).
void GameClientStartLogging(uint8_t* game) {
  void* console = game::Field<void*>(game, 0x10);
  game::Call<void (*)(void*, const char*)>(0x140310480)(console, reinterpret_cast<const char*>(0x142054700));  // "H1Z1.log"
  auto* baseVtable = reinterpret_cast<void**>(0x142049da8);
  auto* fixedVtable = reinterpret_cast<void**>(0x142049dc8);
  // StringFixed<128>: the IStringFixed vtable (0x142049da8) while assigning, then 0x142049dc8.
  soeutil::StringFixed<128> folder;
  soeutil::InitFixed(folder, baseVtable);
  soeutil::StringAssign(&folder, reinterpret_cast<const char*>(0x14206ce58));  // "./Logs"
  folder.vtable = fixedVtable;
  auto* settings = static_cast<uint8_t*>(*reinterpret_cast<void**>(0x142b199f0));
  soeutil::IString configured;
  soeutil::IString* path = game::Call<soeutil::IString* (*)(void*, soeutil::IString*)>(0x1403f6ba0)(settings, &configured);
  soeutil::StringAssignString(&folder, path);
  configured.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&configured);
  settings = static_cast<uint8_t*>(*reinterpret_cast<void**>(0x142b199f0));
  game::Call<void (*)(void*, int)>(0x140310570)(console, game::Field<int>(settings, 0x334C));
  game::Call<void (*)(void*, int)>(0x140310840)(console, game::Field<int>(settings, 0x3348));
  game::Call<void (*)(void*, int)>(0x140310470)(console, game::Field<int>(settings, 0x3350));
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  if (game::Call<GetBoolFn>(0x1403051c0)(game::Field<void*>(game, 0x38E30), reinterpret_cast<const char*>(0x142046fcb),
                                         reinterpret_cast<const char*>(0x14206ce60), true, false, -1, -1))  // "DeleteExistingLogs"
    game::Call<void (*)(const char*, bool)>(0x1403382e0)(folder.data, true);
  game::Call<void (*)(void*, const char*)>(0x140310580)(console, folder.data);
  using LogFn = void (*)(const char*, const char*, ...);
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206ce78));  // "Starting the game client."
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206ce98),
                                 *reinterpret_cast<const char**>(0x1429fbd88));  // "Client version: %s"
  auto getCommandLine = *reinterpret_cast<const char* (**)()>(0x1440a0140);
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206ceb0), getCommandLine());  // "Command line: %s"
  soeutil::StringFixed<256> launcher;
  soeutil::InitFixed(launcher, fixedVtable);
  int unused = 0;
  game::Call<void (*)(soeutil::IString*, int*)>(0x1413432d0)(&launcher, &unused);
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206cec8), launcher.data);  // "Launched by: %s"
  launcher.vtable = baseVtable;
  soeutil::StringRelease(&launcher);
  folder.vtable = baseVtable;
  soeutil::StringRelease(&folder);
}

// 0x1403d64a0 (slot 39): after the gateway login, pump the connection until
// the server sends our character (or 120 s pass / the connection drops /
// the game is closing), then tell the UI and console. Returns false (with an
// error log) if the login did not complete.
bool GameClientWaitForCharacterLogin(uint8_t* game) {
  auto recorder = [] { return *reinterpret_cast<uint8_t**>(0x142b19b98); };
  auto connected = [&] { return game::Call<bool (*)(void*)>(0x14063bdb0)(game::Field<void*>(recorder(), 8)); };
  using ErrorFn = void (*)(const char*, const char*);
  if (!connected()) {
    game::Call<ErrorFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206d4f8));  // "Connection to gateway lost before authenticating"
    return false;
  }
  soeutil::StringFixed<32> unused;
  soeutil::InitFixed(unused, reinterpret_cast<void**>(0x14204a378));
  game::Call<void (*)(void*)>(0x14063be10)(game::Field<void*>(recorder(), 8));  // give the gateway API time
  uint64_t start = 0;
  game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&start);
  constexpr int kLoginTimeoutMs = 120000;
  int elapsed = 0;
  auto localPlayer = [&] { return game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  while (!game[0x38838] && !localPlayer() && connected()) {
    uint64_t now = 0;
    int64_t delta = static_cast<int64_t>(*game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&now) - start);
    elapsed = static_cast<int>(delta > 0x7fffffff ? 0x7fffffff : delta);
    if (elapsed >= kLoginTimeoutMs) break;
    game::Call<void (*)(void*, int)>(0x14063d8e0)(recorder(), 1000);
    if (game[0x38839]) game::Call<void (*)(uint8_t*, bool, bool, bool, int)>(0x140474860)(game, false, true, true, 10);
    game::Call<void (*)(unsigned)>(0x14032ec60)(25);  // Sleep(25)
  }
  game::Call<void (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(0x14206d580), nullptr,
                                                                      nullptr);  // "CharacterSelectHandler:OnCharacterLoginComplete"
  if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
    const char* event = reinterpret_cast<const char*>(0x14206d5b0);  // "EVENT_LOGIN_COMPLETE"
    soeutil::IString name{soeutil::IStringVtable(), const_cast<char*>(event), static_cast<int>(std::strlen(event)), -1};
    game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(console, &name, nullptr, nullptr);
    name.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&name);
  }
  bool ok = true;
  if (game[0x38838]) {
    game::Call<ErrorFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206d530));  // "While connecting to the server the client was ..."
    ok = false;
  } else if (elapsed >= kLoginTimeoutMs && !localPlayer()) {
    game::Call<ErrorFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206dc20));  // "Connected to the server but failed to receive ..."
    ok = false;
  }
  unused.vtable = reinterpret_cast<void**>(0x14204a358);
  soeutil::StringRelease(&unused);
  return ok;
}

// 0x14043b860 (slot 43): forward an item to the object at +0x388C8
// (0x1409de240). The original first converts the item's name through
// 0x1403309b0 into a local string that is never used; kept for fidelity.
void GameClientForwardToHandler388C8(uint8_t* game, void* item) {
  auto* fixedVtable = reinterpret_cast<void**>(0x142049dc8);
  soeutil::StringFixed<256> name;
  soeutil::InitFixed(name, fixedVtable);
  soeutil::StringFixed<256> converted;
  soeutil::InitFixed(converted, fixedVtable);
  game::Call<void (*)(void*, soeutil::IString*)>(0x140ce9690)(item, &name);
  char buffer[0x800];
  buffer[0] = 0;
  game::Call<void (*)(const char*, char*, int, char, bool, bool)>(0x1403309b0)(name.data, buffer, 0x800, ' ', true, true);
  if (buffer[0]) soeutil::StringAssign(&converted, buffer);
  game::Call<void (*)(void*, void*)>(0x1409de240)(game::Field<void*>(game, 0x388C8), item);
  converted.vtable = reinterpret_cast<void**>(0x142049da8);
  soeutil::StringRelease(&converted);
  name.vtable = reinterpret_cast<void**>(0x142049da8);
  soeutil::StringRelease(&name);
}

// Opcode-0x17 sub-packets handled by slot 99: {vtable, opcode, subType, payload}.
struct Packet17 {
  void** vtable;
  int opcode;   // 0x17
  int pad0C;
  int subType;
  int pad14;
  uint64_t payload[4];
};
static_assert(offsetof(Packet17, subType) == 0x10);
static_assert(offsetof(Packet17, payload) == 0x18);
static_assert(sizeof(Packet17) == 0x38);

// 0x14040bba0 (slot 99): packet 0x17 - peek the sub-type byte and route.
// 1/2/6 are read into a local packet (whose read call applies it), 3 goes
// to the object at +0x38A30 and refreshes the local player's two UI hooks
// (creating the +0x38A40 helper once), 4/5 go to two global managers.
bool GameClientHandlePacket17(uint8_t* game, const uint8_t* data, int length) {
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 1 > end ? end : data + 1;
  int subType = cursor + 1 > end ? 0 : static_cast<int8_t>(*cursor);
  using ReadFn = void (*)(const uint8_t*, int, Packet17*);
  Packet17 packet{};
  packet.opcode = 0x17;
  packet.subType = subType;
  switch (subType) {
    case 1:
      packet.vtable = reinterpret_cast<void**>(0x142068530);
      packet.payload[0] = game::Field<uint64_t>(game, 0x389C0);
      game::Call<ReadFn>(0x140387de0)(data, length, &packet);
      return true;
    case 6:
      packet.vtable = reinterpret_cast<void**>(0x142068538);
      game::Call<void (*)(uint8_t*)>(0x1403d4c60)(game + 0x390F8);
      packet.payload[1] = reinterpret_cast<uint64_t>(game + 0x390F8);
      packet.payload[2] = reinterpret_cast<uint64_t>(game + 0x391B8);
      packet.payload[3] = reinterpret_cast<uint64_t>(game + 0x395F8);
      game::Call<ReadFn>(0x140387cc0)(data, length, &packet);
      return true;
    case 2: {
      packet.vtable = reinterpret_cast<void**>(0x142068540);
      packet.payload[0] = *reinterpret_cast<uint64_t*>(0x142b19b40);
      game::Call<ReadFn>(0x140387d60)(data, length, &packet);
      auto* root = *reinterpret_cast<uint8_t**>(0x142b19cc0);
      if (void* first = game::Field<void*>(root, 0x40)) game::Call<void (*)(void*)>(0x1406fb3e0)(first);
      if (void* second = game::Field<void*>(root, 0x48)) game::Call<void (*)(void*)>(0x1406fdc50)(second);
      return true;
    }
    case 3: {
      game::Call<void (*)(void*, const uint8_t*, int)>(0x140999a50)(game::Field<void*>(game, 0x38A30), data, length);
      static const int kPlayerHooks[] = {0xAD70, 0xAD78};
      for (int hook : kPlayerHooks) {
        auto* player = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80);
        if (!player) continue;
        if (void* object = game::Field<void*>(player, hook)) (*reinterpret_cast<void (***)(void*)>(object))[5](object);
      }
      if (game[0x38A38] && !game::Field<void*>(game, 0x38A40)) {
        void* helper = GameAllocate(0x68);
        if (helper)
          helper = game::Call<void* (*)(void*, void*, void*, void*, void*, void*)>(0x140999f20)(
              helper, game::Field<void*>(game, 0x3D3C8), game::Field<void*>(game, 0x38A30), game::Field<void*>(game, 0x38908),
              *reinterpret_cast<void**>(0x142b19838), game::Field<void*>(game, 0x389C8));
        game::Field<void*>(game, 0x38A40) = helper;
      }
      return true;
    }
    case 4:
      game::Call<void (*)(void*, const uint8_t*, int)>(0x140a19ea0)(*reinterpret_cast<void**>(0x142b19c00), data, length);
      return true;
    case 5:
      game::Call<void (*)(void*, const uint8_t*, int)>(0x140a0fca0)(*reinterpret_cast<void**>(0x142b19bf0), data, length);
      return true;
    default:
      return false;
  }
}

// UI script argument list {vtable, begin, end}.
struct ScriptArgs {
  void** vtable;
  void* begin;
  void* end;
};
static_assert(sizeof(ScriptArgs) == 0x18);

// 0x140468e00 (slot 48): call "<ChatHandler>:SetChatText"(text) in the UI;
// if no such script function exists, report it through the logger at +0x38DD0.
void GameClientSetChatText(uint8_t* game, void* text) {
  soeutil::IString handler{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  using FindHandlerFn = bool (*)(void*, const char*, soeutil::IString*);
  if (game::Call<FindHandlerFn>(0x14048a5c0)(UiRoot(), reinterpret_cast<const char*>(0x14206e460), &handler)) {  // "ChatHandler"
    game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(&handler, reinterpret_cast<const char*>(0x14206e600));  // ":SetChatText"
    ScriptArgs args{reinterpret_cast<void**>(0x14206c548), nullptr, nullptr};
    if (auto* slot = game::Call<int* (*)(ScriptArgs*, int)>(0x140418710)(&args, 0)) *slot = 0;
    game::Call<void (*)(void*, void*)>(0x14046d850)(args.begin, text);
    if (!game::Call<bool (*)(void*, const char*, ScriptArgs*, void*)>(0x140488cc0)(UiRoot(), handler.data, &args, nullptr)) {
      void* logger = game::Field<void*>(game, 0x38DD0);
      int line = game::Call<int (*)()>(0x1416dfd20)();
      int file = game::Call<int (*)()>(0x1416dfe00)();
      using ReportFn = void (*)(void*, const char*, int, int, int, bool, void*, bool);
      (*reinterpret_cast<ReportFn**>(logger))[5](logger, reinterpret_cast<const char*>(0x14206e610), 0, file, line, false, nullptr,
                                                 true);  // "Problem with chat handler.  No function matching ..."
    }
    game::Call<void (*)(ScriptArgs*)>(0x1403a06c0)(&args);
  }
  handler.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&handler);
}

// Small outgoing packet {vtable, opcode, two payload words}.
struct SmallPacket {
  void** vtable;
  int opcode;
  int pad0C;
  uint64_t first;
  uint64_t second;
};
static_assert(offsetof(SmallPacket, first) == 0x10);
static_assert(sizeof(SmallPacket) == 0x20);

// 0x1403e7a50 (slot 40): DisconnectFromServer(reason) - log, tell the server
// (0x5F if flagged, 0x71/3 if mounted, then 0x07 and a 2 s flush unless
// suppressed), destroy the local player and per-session managers, and save
// user options if they changed.
void GameClientDisconnectFromServer(uint8_t* game, const char* reason) {
  using LogFn = void (*)(const char*, const char*, ...);
  game::Call<LogFn>(0x1402bab70)(reinterpret_cast<const char*>(0x142054710), reinterpret_cast<const char*>(0x14206dc80),
                                 reason);  // "NetInfo.log", "DisconnectFromServer(): Reason: %s"
  auto vcall = [](void* object, int slot) { (*reinterpret_cast<void (***)(void*)>(object))[slot](object); };
  if (void* object = game::Field<void*>(game, 0x3D3C0)) vcall(object, 0x90 / 8);
  using SendFn = void (*)(void*, SmallPacket*, int, bool);
  auto* state = game::Field<uint8_t*>(game, 0x314A8);
  if (state[0x198]) {
    SmallPacket packet{reinterpret_cast<void**>(0x142063d90), 0x5F, 0, reinterpret_cast<uint64_t>(state + 0xD0), 0};
    game::Call<SendFn>(0x14035f210)(game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19b98), 8), &packet, 1, true);
  }
  vcall(game, 0x1E8 / 8);
  if (auto* player = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80))
    game::Call<void (*)(uint8_t*)>(0x140558310)(player + 0x1260);
  auto* recorder = *reinterpret_cast<uint8_t**>(0x142b19b98);
  if (void* owner = game::Field<void*>(game, 0x38860)) {
    void* self = game::Call<void* (*)(void*)>(0x14071e830)(owner);
    if (self && game::Call<void* (*)(void*)>(0x14050f300)(self)) {
      if (recorder) {
        SmallPacket dismount{reinterpret_cast<void**>(0x142065c60), 0x71, 0, 3, 1};
        game::Call<SendFn>(0x14035f520)(game::Field<void*>(recorder, 8), &dismount, 0, true);
      }
      uint64_t value = *reinterpret_cast<uint64_t*>(0x142b181f8);
      game::Call<void (*)(void*, int, uint64_t*, void*)>(0x140505f60)(self, 0, &value, nullptr);
      uint64_t now = 0;
      game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&now);
      value = now;
      game::Call<void (*)(uint8_t*, void*, uint64_t*)>(0x1403d0d40)(game, self, &value);
    }
  }
  if (!game[0x3883A] && recorder) {
    game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206dca8));  // "Disconnecting from the server"
    SmallPacket logout{reinterpret_cast<void**>(0x142063c18), 7, 0, 0, 0};
    game::Call<SendFn>(0x14035f120)(game::Field<void*>(recorder, 8), &logout, 0, true);
    game::Call<void (*)(void*, int, bool)>(0x14063c2b0)(game::Field<void*>(recorder, 8), 2000, true);
  }
  if (void* handler = game::Field<void*>(game, 0x388C8)) game::Call<void (*)(void*)>(0x1409c6070)(handler);
  vcall(game, 0x110 / 8);
  state = game::Field<uint8_t*>(game, 0x314A8);
  if (void* player = game::Field<void*>(state, 0xF80))
    (*reinterpret_cast<void (***)(void*, int)>(player))[2](player, 1);  // deleting destructor
  game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80) = nullptr;
  *reinterpret_cast<void**>(0x142b19ba0) = nullptr;
  if (void* manager = *reinterpret_cast<void**>(0x142b19c60)) game::Call<void (*)(void*)>(0x1406565f0)(manager);
  if (void* manager = *reinterpret_cast<void**>(0x142b19c48)) game::Call<void (*)(void*)>(0x1407dc930)(manager);
  if (void* manager = *reinterpret_cast<void**>(0x142b19c68)) game::Call<void (*)(void*)>(0x140ae9ec0)(manager);
  if (void* object = game::Field<void*>(game, 0x388A8)) (*reinterpret_cast<void (***)(void*, int)>(object))[8](object, 1);
  game::Field<void*>(game, 0x388A8) = nullptr;
  game::Call<void (*)(uint8_t*, int)>(0x140469190)(game, 4);
  game::Call<void (*)(uint8_t*)>(0x14047cfe0)(game);
  if (void* object = game::Field<void*>(game, 0x388A0)) game::Call<void (*)(void*, int)>(0x1406145e0)(object, 0);
  if (void* owner = game::Field<void*>(game, 0x38860)) game::Call<void (*)(void*, int)>(0x14071c680)(owner, 0);
  if (game::Call<bool (*)(void*)>(0x140aae940)(*reinterpret_cast<void**>(0x142b199f0))) {
    game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206dcc8));  // "Saving user options"
    game::Call<void (*)(void*)>(0x140ab23d0)(*reinterpret_cast<void**>(0x142b199f0));
  }
}

// 0x14046fa20 (slot 49): refresh the job browser - "<HandlerJobBrowser>:SetJobCount"
// with the player's job count, one 0x14046b320 call per job in the player's
// list, then slot 50 with the value stored for the current job id (5-bucket
// hash at player+0x220, node {value +4, next +0x90, key +0x98}).
void GameClientRefreshJobBrowser(uint8_t* game) {
  soeutil::IString handler{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  using FindHandlerFn = bool (*)(void*, const char*, soeutil::IString*);
  if (game::Call<FindHandlerFn>(0x14048a5c0)(UiRoot(), reinterpret_cast<const char*>(0x14206d950), &handler)) {  // "HandlerJobBrowser"
    soeutil::IString function{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    soeutil::StringAssignString(&function, &handler);
    game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(&function, reinterpret_cast<const char*>(0x14206d980));  // ":SetJobCount"
    ScriptArgs args{reinterpret_cast<void**>(0x14206c548), nullptr, nullptr};
    auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
    int count = game::Field<int>(player(), 0x218);
    if (auto* slot = game::Call<int* (*)(ScriptArgs*, int)>(0x140418710)(&args, 0)) *slot = 0;
    // Script value {type, payload}: types 3, 7, 8 and 9 own an object at +8.
    auto* value = static_cast<uint8_t*>(args.begin);
    int type = game::Field<int>(value, 0);
    if (type == 3 || type == 7 || type == 8 || type == 9) {
      void* owned = value + 8;
      (*reinterpret_cast<void (***)(void*, int)>(owned))[0](owned, 0);
    }
    game::Field<int>(value, 0) = 1;  // integer
    game::Field<int>(value, 8) = count;
    game::Call<bool (*)(void*, const char*, ScriptArgs*, void*)>(0x140488cc0)(UiRoot(), function.data, &args, nullptr);
    int index = 0;
    for (auto* job = game::Field<uint8_t*>(player(), 0x208); job; job = game::Field<uint8_t*>(job, 0xA8), ++index)
      game::Call<void (*)(uint8_t*, int, const char*, uint8_t*)>(0x14046b320)(game, index, handler.data, job);
    game::Call<void (*)(ScriptArgs*)>(0x1403a06c0)(&args);
    function.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&function);
    uint8_t* self = player();
    unsigned key = game::Field<unsigned>(self, 0x2D0);
    int current = 0;
    for (auto* node = game::Field<uint8_t*>(self, 0x220 + static_cast<int>(key % 5) * 8); node; node = game::Field<uint8_t*>(node, 0x90)) {
      if (game::Field<unsigned>(node, 0x98) == key) {
        current = game::Field<int>(node, 4);
        break;
      }
    }
    (*reinterpret_cast<void (***)(uint8_t*, int)>(game))[0x190 / 8](game, current);
  }
  handler.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&handler);
}

static_assert(sizeof(soeutil::StringFixed<256>) == 0x120);

// 0x14046e660 (slot 38): connect to the gateway. Recreates the zone client
// (slot 86, honoring "UseCompression"), sets up the s-channel, connects to the
// address at +0x31608 with a 60 s timeout while pumping, logs the connect info
// and on failure reports "Failed gateway connection - <info> - <reason>"
// (error 0x1A when the gateway rejected us with reason 0x10, else 0x0F).
bool GameClientConnectToGateway(uint8_t* game) {
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  bool useCompression = game::Call<GetBoolFn>(0x1403051c0)(game::Field<void*>(game, 0x38E30), reinterpret_cast<const char*>(0x142046fcb),
                                                           reinterpret_cast<const char*>(0x14206d1a8), false, false, -1, -1);  // "UseCompression"
  auto& recorderSlot = *reinterpret_cast<uint8_t**>(0x142b19b98);
  if (recorderSlot) (*reinterpret_cast<void (***)(void*, int)>(recorderSlot))[0](recorderSlot, 1);
  recorderSlot = nullptr;
  recorderSlot = (*reinterpret_cast<uint8_t* (***)(uint8_t*, bool, bool)>(game))[0x2B0 / 8](game, true, useCompression);
  using ErrorFn = void (*)(const char*, const char*, ...);
  if (!game::Call<bool (*)(uint8_t*)>(0x1403d5ea0)(game))
    game::Call<ErrorFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206d1b8));  // "Failed to establish s-channel with gateway."
  const char* percentS = reinterpret_cast<const char*>(0x142046fb8);  // "%s"
  soeutil::StringFixed<64> reason;
  reason.vtable = reinterpret_cast<void**>(0x142049d00);
  reason.data = soeutil::EmptyStringData();
  reason.length = 0;
  reason.capacity = 0;
  uint64_t start = 0;
  game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&start);
  auto api = [&] { return game::Field<void*>(recorderSlot, 8); };
  bool connected;
  if (game::Field<int>(game, 0x31618) > 0) {
    game::Call<void (*)(void*, const char*, int, int)>(0x14063d680)(recorderSlot, game::Field<const char*>(game, 0x31610), 60000, 0);
    while (game::Call<bool (*)(void*)>(0x14063bda0)(api())) {  // IsConnecting
      game::Call<void (*)(void*, int)>(0x14063d8e0)(recorderSlot, 1000);
      game::Call<void (*)(unsigned)>(0x14032ec60)(10);  // Sleep(10)
    }
    connected = game::Call<bool (*)(void*)>(0x14063bdb0)(api());
  } else {
    soeutil::StringAssign(&reason, reinterpret_cast<const char*>(0x14206d1e8));  // "Invalid gateway address."
    game::Call<ErrorFn>(0x1402baba0)(nullptr, percentS, reason.data);
    connected = false;
  }
  uint64_t now = 0;
  int64_t delta = static_cast<int64_t>(*game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&now) - start);
  int elapsed = static_cast<int>(delta > 0x7fffffff ? 0x7fffffff : delta);
  auto* fixedVtable = reinterpret_cast<void**>(0x142049e08);
  soeutil::StringFixed<256> info;
  info.vtable = fixedVtable;
  info.data = soeutil::EmptyStringData();
  info.length = 0;
  info.capacity = 0;
  soeutil::StringFormat(&info, reinterpret_cast<const char*>(0x14206d210), elapsed, game::Field<uint64_t>(game, 0x38BF0),
                        game::Field<const char*>(game, 0x31610), game::Field<const char*>(game, 0x316B0));
  auto* releaseVtable = reinterpret_cast<void**>(0x142049de8);
  if (connected) {
    game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(nullptr, percentS, info.data);
  } else {
    bool rejected = false;
    if (reason.length == 0) {
      using ValueFn = int (*)(void*);
      using ReasonTextFn = const char* (*)(int);
      int disconnect = game::Call<ValueFn>(0x14063bbf0)(api());
      if (disconnect == 3) {
        soeutil::StringAssign(&reason, game::Call<ReasonTextFn>(0x140346020)(game::Call<ValueFn>(0x14063bc50)(api())));
        rejected = game::Call<ValueFn>(0x14063bc50)(api()) == 0x10;
      } else {
        soeutil::StringAssign(&reason, game::Call<ReasonTextFn>(0x140346020)(game::Call<ValueFn>(0x14063bbf0)(api())));
      }
    }
    soeutil::StringFixed<256> message;
    message.vtable = fixedVtable;
    message.data = soeutil::EmptyStringData();
    message.length = 0;
    message.capacity = 0;
    soeutil::StringFormat(&message, reinterpret_cast<const char*>(0x14206d260), info.data, reason.data);  // "Failed gateway connection - %s - %s"
    game::Call<ErrorFn>(0x1402baba0)(nullptr, percentS, message.data);
    game::Call<void (*)(int, const char*, void*, void*)>(0x1403e1d80)(rejected ? 0x1A : 0x0F, message.data, nullptr, nullptr);
    message.vtable = releaseVtable;
    soeutil::StringRelease(&message);
  }
  info.vtable = releaseVtable;
  soeutil::StringRelease(&info);
  reason.vtable = reinterpret_cast<void**>(0x142049ce0);
  soeutil::StringRelease(&reason);
  return connected;
}

// Inlined IString write helpers: get a private buffer of `bytes`, then write.
void StringMakeWritable(soeutil::IString* text, int bytes) {
  if (text->capacity < bytes || (text->capacity > 0 && reinterpret_cast<int*>(text->data)[-1] > 1))
    soeutil::StringReserve(text, bytes);
}
void StringAppendChar(soeutil::IString* text, char c) {
  StringMakeWritable(text, text->length + 2);
  text->data[text->length++] = c;
  text->data[text->length] = 0;
}

// Case-insensitive compare through the game's lower-case table (0x1429f91d0).
bool GameStringEqualsNoCase(const char* a, const char* b) {
  auto* lower = reinterpret_cast<const signed char*>(0x1429f91d0);
  while (*a && lower[static_cast<uint8_t>(*a)] == lower[static_cast<uint8_t>(*b)]) {
    ++a;
    ++b;
  }
  return lower[static_cast<uint8_t>(*a)] == lower[static_cast<uint8_t>(*b)];
}

// Login error detail {name IString +0, value IString +0x40}, 0x80 bytes;
// the list is {?, entries +8, count +0x10}.
constexpr int kLoginErrorDetailSize = 0x80;

// 0x14042c3f0 (slot 41): login-server failure -> Shutdown (slot 27) with the
// matching message. For "already linked" (4) the detail
// ALREADY_LINKED_ACCOUNT_NAME is masked (a***z), URL-escaped and passed on
// as "accountName=%s".
void GameClientOnLoginFailed(uint8_t* game, int code, uint8_t* details) {
  using ShutdownFn = void (*)(uint8_t*, bool, int, const char*, const char*);
  auto shutdown = [&](int error, uint64_t message, const char* extra) {
    (*reinterpret_cast<ShutdownFn**>(game))[0xD8 / 8](game, true, error, reinterpret_cast<const char*>(message), extra);
  };
  if (code == 2) return shutdown(0x1C, 0x14206dd08, nullptr);  // "Login Server is locked at this time."
  if (code != 4) {
    if (code == 9) return shutdown(0x1F, 0x14206dce0, nullptr);  // "Account is not bound to third party."
    return shutdown(0x1D, 0x14206dd90, nullptr);                 // "Unable to authenticate with Login Server."
  }
  soeutil::IString extra{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  if (details) {
    for (int i = 0; i < game::Field<int>(details, 0x10) && extra.length == 0; ++i) {
      auto* entry = game::Field<uint8_t*>(details, 8) + i * kLoginErrorDetailSize;
      if (!GameStringEqualsNoCase(game::Field<const char*>(entry, 8), reinterpret_cast<const char*>(0x14206dd30))) continue;  // "ALREADY_LINKED_ACCOUNT_NAME"
      soeutil::IString name{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      soeutil::StringAssignString(&name, reinterpret_cast<soeutil::IString*>(entry + 0x40));
      if (name.length > 3) {
        for (int at = 1; at < name.length - 1; ++at) {
          StringMakeWritable(&name, name.length + 1);
          name.data[at] = '*';
        }
      }
      soeutil::IString escaped{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      for (const char* c = name.data; *c; ++c) {
        auto byte = static_cast<uint8_t>(*c);
        bool plain = byte >= 0x21 && byte <= 0x7E && byte != '"' && byte != '#' && byte != '%' && byte != '<' && byte != '>';
        if (plain)
          StringAppendChar(&escaped, static_cast<char>(byte));
        else
          game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(&escaped, reinterpret_cast<const char*>(0x142066928),
                                                                                 static_cast<unsigned>(byte));  // "%%%02x"
      }
      soeutil::StringFormat(&extra, reinterpret_cast<const char*>(0x14206dd50), escaped.data);  // "accountName=%s"
      escaped.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&escaped);
      name.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&name);
    }
  }
  shutdown(0x20, 0x14206dd60, extra.data);  // "Account is already linked to third party."
  extra.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&extra);
}

// Packet 0x41 (slot 97): {vtable, opcode, subType, payload}.
struct Packet41 {
  void** vtable;
  int opcode;  // 0x41
  int pad0C;
  int subType;
  int pad14;
  uint64_t payload[2];
};
static_assert(offsetof(Packet41, subType) == 0x10);
static_assert(offsetof(Packet41, payload) == 0x18);

// Inline bounded reads used by slot 97 (a read past the end yields 0 and
// leaves the cursor at the end).
const uint8_t* SkipOpcodeAndSubType(const uint8_t* data, const uint8_t* end) { return data + 2 > end ? end : data + 2; }
int ReadIntOrZero(const uint8_t*& cursor, const uint8_t* end) {
  if (cursor + 4 > end) {
    cursor = end;
    return 0;
  }
  int value;
  std::memcpy(&value, cursor, 4);
  cursor += 4;
  return value;
}
// {int length, bytes}: an out-of-range length yields {nullptr, 0}.
void ReadBlob(const uint8_t* cursor, const uint8_t* end, const uint8_t*& blob, int& length) {
  length = ReadIntOrZero(cursor, end);
  blob = cursor;
  if (length < 0 || length > static_cast<int>(end - cursor)) {
    blob = nullptr;
    length = 0;
  }
}

// 0x140409ee0 (slot 97): packet 0x41 - the object at +0x38E68 (created by
// sub-type 6, destroyed by 7) and its entries; each change refreshes the UI
// at +0x388E8 (0x14098dcb0).
bool GameClientHandlePacket41(uint8_t* game, const uint8_t* data, int length) {
  const uint8_t* end = data + length;
  const uint8_t* afterOpcode = data + 1 > end ? end : data + 1;
  int subType = afterOpcode + 1 > end ? 0 : static_cast<int8_t>(*afterOpcode);
  auto object = [&] { return game::Field<uint8_t*>(game, 0x38E68); };
  auto refreshUi = [&] {
    if (void* ui = game::Field<void*>(game, 0x388E8)) game::Call<void (*)(void*)>(0x14098dcb0)(ui);
  };
  auto refreshUiWith = [&](uint64_t first) {
    if (void* ui = game::Field<void*>(game, 0x388E8)) {
      game::Call<void (*)(void*)>(first)(ui);
      game::Call<void (*)(void*)>(0x14098dcb0)(game::Field<void*>(game, 0x388E8));
    }
  };
  using SelectFn = void (*)(void*, int);
  using EntryFn = void* (*)(void*, int);
  auto removeEntry = [](void* owner, void* entry) { (*reinterpret_cast<void (***)(void*, void*)>(owner))[0x18 / 8](owner, entry); };
  Packet41 packet{};
  packet.opcode = 0x41;
  packet.subType = subType;
  switch (subType) {
    case 1: {  // select (first, second)
      packet.vtable = reinterpret_cast<void**>(0x1420682f8);
      const uint8_t* cursor = SkipOpcodeAndSubType(data, end);
      int first = ReadIntOrZero(cursor, end);
      int second = ReadIntOrZero(cursor, end);
      packet.payload[0] = static_cast<uint32_t>(first) | (static_cast<uint64_t>(static_cast<uint32_t>(second)) << 32);
      if (uint8_t* owner = object()) {
        game::Call<SelectFn>(0x140a0e290)(owner, first);
        refreshUi();
      }
      return true;
    }
    case 2: {
      packet.vtable = reinterpret_cast<void**>(0x142068300);
      ClientReader reader{data, length, data, end, 0};
      game::Call<void (*)(Packet41*, ClientReader*)>(0x140370450)(&packet, &reader);
      if (uint8_t* owner = object()) {
        game::Call<SelectFn>(0x140a0e290)(owner, 0);
        refreshUi();
      }
      return true;
    }
    case 3: {
      packet.vtable = reinterpret_cast<void**>(0x142068308);
      ClientReader reader{data, length, data, end, 0};
      game::Call<void (*)(Packet41*, ClientReader*)>(0x140377400)(&packet, &reader);
      if (object()) refreshUiWith(0x14098dac0);
      return true;
    }
    case 4: {  // add/replace one entry
      packet.vtable = reinterpret_cast<void**>(0x142068310);
      const uint8_t* blob;
      int blobLength;
      ReadBlob(SkipOpcodeAndSubType(data, end), end, blob, blobLength);
      if (!object()) return true;
      void* memory = GameAllocate(0x1E0);
      auto* entry = memory ? game::Call<uint8_t* (*)(void*)>(0x141704630)(memory) : nullptr;
      ClientReader reader{blob, blobLength, blob, blob + blobLength, 0};
      game::Call<void (*)(uint8_t*, ClientReader*)>(0x140370f20)(entry, &reader);
      if (void* existing = game::Call<EntryFn>(0x141704cd0)(object(), game::Field<int>(entry, 0x68))) removeEntry(object(), existing);
      game::Call<void (*)(void*, uint8_t*)>(0x141704bb0)(object(), entry);
      auto* self = game::Call<uint8_t* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860));
      void* identity = self + 0x630;
      uint64_t scratch;
      uint64_t* selfId = (*reinterpret_cast<uint64_t* (***)(void*, uint64_t*)>(identity))[0x68 / 8](identity, &scratch);
      if (game::Field<uint64_t>(entry, 0x80) == *selfId) game::Call<SelectFn>(0x140a0e290)(object(), game::Field<int>(entry, 0x68));
      refreshUi();
      return true;
    }
    case 6: {  // (re)create the object
      packet.vtable = reinterpret_cast<void**>(0x142068318);
      const uint8_t* blob;
      int blobLength;
      ReadBlob(SkipOpcodeAndSubType(data, end), end, blob, blobLength);
      if (uint8_t* old = object()) (*reinterpret_cast<void (***)(void*, int)>(old))[0](old, 1);
      void* memory = GameAllocate(0x6A0);
      auto* created = memory ? game::Call<uint8_t* (*)(void*)>(0x140a0d540)(memory) : nullptr;
      game::Field<uint8_t*>(game, 0x38E68) = created;
      ClientReader reader{blob, blobLength, blob, blob + blobLength, 0};
      game::Call<void (*)(ClientReader*, uint8_t*)>(0x1403859e0)(&reader, created + 8);
      int trailing = 0;
      if (reader.cursor + 4 <= reader.end) std::memcpy(&trailing, reader.cursor, 4);
      game::Field<int>(created, 0x348) = trailing;
      refreshUiWith(0x14098d950);
      return true;
    }
    case 7:  // destroy the object
      packet.vtable = reinterpret_cast<void**>(0x142068320);
      if (uint8_t* owner = object()) {
        (*reinterpret_cast<void (***)(void*, int)>(owner))[0](owner, 1);
        game::Field<uint8_t*>(game, 0x38E68) = nullptr;
        refreshUi();
      }
      return true;
    case 8: {  // remove an entry
      packet.vtable = reinterpret_cast<void**>(0x142068328);
      const uint8_t* cursor = SkipOpcodeAndSubType(data, end);
      int id = ReadIntOrZero(cursor, end);
      if (uint8_t* owner = object()) {
        if (void* existing = game::Call<EntryFn>(0x141704cd0)(owner, id)) removeEntry(object(), existing);
        if (game::Field<int>(object(), 0x69C) == id) game::Call<SelectFn>(0x140a0e290)(object(), 0);
        refreshUi();
      }
      return true;
    }
    case 11: {
      packet.vtable = reinterpret_cast<void**>(0x142068330);
      game::Call<void (*)(const uint8_t*, int, Packet41*)>(0x140387c10)(data, length, &packet);
      if (uint8_t* owner = object()) {
        (*reinterpret_cast<void (***)(void*, int)>(owner))[0x78 / 8](owner, static_cast<int>(packet.payload[0]));
        refreshUi();
      }
      return true;
    }
    case 12: {
      packet.vtable = reinterpret_cast<void**>(0x142068338);
      ClientReader reader{data, length, data, end, 0};
      game::Call<void (*)(Packet41*, ClientReader*)>(0x140376b00)(&packet, &reader);
      if (object()) refreshUiWith(0x14098d950);
      return true;
    }
    default:
      return false;
  }
}

// A StringFixed<256> starting empty with the given vtable.
void InitString256(soeutil::StringFixed<256>& text, uint64_t vtable) {
  text.vtable = reinterpret_cast<void**>(vtable);
  text.data = soeutil::EmptyStringData();
  text.length = 0;
  text.capacity = 0;
}

// Script argument list with 0xA8-byte values (vtable 0x14206d930):
// {vtable, values, count}. 0x140418710 inserts a value at an index.
struct ScriptArgList {
  void** vtable;
  uint8_t* values;
  int count;
  int capacity;
};
static_assert(sizeof(ScriptArgList) == 0x18);
constexpr int kScriptValueSize = 0xA8;

uint8_t* ScriptArgAt(ScriptArgList* args, int index) {
  if (index == 0 || args->count <= index) {
    if (auto* type = game::Call<int* (*)(ScriptArgList*, int)>(0x140418710)(args, index)) *type = 0;
  }
  return args->values + index * kScriptValueSize;
}
void ScriptArgSetInt(ScriptArgList* args, int index, int value) {
  uint8_t* slot = ScriptArgAt(args, index);
  game::Call<void (*)(uint8_t*, int)>(0x14046d7b0)(slot, value);
}
void ScriptArgSetString(ScriptArgList* args, int index, const char* value) {
  uint8_t* slot = ScriptArgAt(args, index);
  game::Call<void (*)(uint8_t*, const char*)>(0x14046d850)(slot, value);
}

// 0x140431ca0 (slot 50): show one of the player's jobs in the job browser -
// "<HandlerJobBrowser>:SetJob"(id, title, description, value +0x18, rank,
// 0, 0, 0x1403f5290 value) then "<HandlerJobBrowser>:PresentJob".
// Job node: {id +4, title string id +8, description string id +0xC, +0x18,
// next +0xA8}.
void GameClientPresentJob(uint8_t* game, int jobId) {
  soeutil::IString handler{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  using FindHandlerFn = bool (*)(void*, const char*, soeutil::IString*);
  auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  uint8_t* job = nullptr;
  if (game::Call<FindHandlerFn>(0x14048a5c0)(UiRoot(), reinterpret_cast<const char*>(0x14206d950), &handler) && player()) {  // "HandlerJobBrowser"
    for (job = game::Field<uint8_t*>(player(), 0x208); job && game::Field<int>(job, 4) != jobId; job = game::Field<uint8_t*>(job, 0xA8)) {
    }
  }
  if (job) {
    soeutil::StringFixed<256> title;
    InitString256(title, 0x14206d918);
    soeutil::StringFixed<256> description;
    InitString256(description, 0x14206d918);
    using LookupFn = soeutil::IString* (*)(void*, soeutil::IString*, int);
    auto lookup = [&](soeutil::IString* target, int offset) {
      soeutil::IString text;
      soeutil::StringAssignString(target, game::Call<LookupFn>(0x140484160)(game::Field<void*>(game, 0x3B6B8), &text, game::Field<int>(job, offset)));
      text.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&text);
    };
    lookup(&title, 8);
    lookup(&description, 0xC);
    soeutil::IString function{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    soeutil::StringAssignString(&function, &handler);
    game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(&function, reinterpret_cast<const char*>(0x14206d968));  // ":SetJob"
    ScriptArgList args{reinterpret_cast<void**>(0x14206d930), nullptr, 0, 0};
    ScriptArgSetInt(&args, 0, game::Field<int>(job, 4));
    ScriptArgSetString(&args, 1, title.data);
    ScriptArgSetString(&args, 2, description.data);
    ScriptArgSetInt(&args, 3, game::Field<int>(job, 0x18));
    // Rank: the single cached entry (+0xDD80) when the flag at 0x142ab8228
    // is set, else the 16-bucket hash at +0xDD98 {value +0xC, key +0x30, next +0x38}; default 1.
    int rank = 1;
    uint8_t* self = player();
    if (*reinterpret_cast<int*>(0x142ab8228) != 0) {
      if (auto* cached = game::Field<uint8_t*>(self, 0xDD80)) rank = game::Field<int>(cached, 0xC);
    } else {
      int id = game::Field<int>(job, 4);
      for (auto* node = game::Field<uint8_t*>(self, 0xDD98 + (id & 0xF) * 8); node; node = game::Field<uint8_t*>(node, 0x38)) {
        if (game::Field<int>(node, 0x30) == id) {
          rank = game::Field<int>(node, 0xC);
          break;
        }
      }
    }
    ScriptArgSetInt(&args, 4, rank);
    ScriptArgSetInt(&args, 5, 0);
    ScriptArgSetInt(&args, 6, 0);
    int id = game::Field<int>(job, 4);
    self = player();
    ScriptArgAt(&args, 7);
    int extra = game::Call<int (*)(uint8_t*, int)>(0x1403f5290)(self + 0xDD70, id);
    game::Call<void (*)(uint8_t*, int)>(0x14046d7b0)(args.values + 7 * kScriptValueSize, extra);
    game::Call<bool (*)(void*, const char*, ScriptArgList*, void*)>(0x140488cc0)(UiRoot(), function.data, &args, nullptr);
    game::Call<void (*)(ScriptArgList*)>(0x1403a0ae0)(&args);
    function.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&function);
    soeutil::StringFixed<256> present;
    InitString256(present, 0x142049de8);
    soeutil::StringAssignString(&present, &handler);
    present.vtable = reinterpret_cast<void**>(0x142049e08);
    game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(&present, reinterpret_cast<const char*>(0x14206d970));  // ":PresentJob"
    game::Call<bool (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), present.data, nullptr, nullptr);
    present.vtable = reinterpret_cast<void**>(0x142049de8);
    soeutil::StringRelease(&present);
    description.vtable = reinterpret_cast<void**>(0x14206d900);
    soeutil::StringRelease(&description);
    title.vtable = reinterpret_cast<void**>(0x14206d900);
    soeutil::StringRelease(&title);
  }
  handler.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&handler);
}

// 0x140432650 (slot 31): initialize the game client once the zone is known -
// rebuild the +0x388E0 helper, init the game world for the zone name at
// +0x38988 (Shutdown 0x16 "Failed to init the game world for: %s" on
// failure), place the camera, start the web browser from the "WebBrowser"
// ini section, apply the user options (*0x142b199f0) to the subsystems and
// read the "WallOfData" settings.
bool GameClientInitialize(uint8_t* game) {
  game::Field<uint64_t>(game, 0x38B88) = *reinterpret_cast<uint64_t*>(0x142b17e20);
  game::Call<void (*)(uint8_t*, int)>(0x1403d7290)(game, 0);
  if (void* old = game::Field<void*>(game, 0x388E0)) (*reinterpret_cast<void (***)(void*, int)>(old))[0](old, 1);
  void* memory = GameAllocate(0x58);
  game::Field<void*>(game, 0x388E0) = memory ? game::Call<void* (*)(void*)>(0x1408dc830)(memory) : nullptr;
  auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  if (uint8_t* self = player()) game::Call<void (*)(uint8_t*)>(0x1404ce1e0)(self + 0x5658);

  auto* world = game::Field<uint8_t*>(game, 0x3D3E0);
  uint8_t savedLoading = world[0x3B648];
  world[0x3B648] = 1;
  using InitWorldFn = bool (*)(void*, const char*, int);
  if (!game::Call<InitWorldFn>(0x1418687a0)(game::Field<void*>(game, 0x3B6F8), game::Field<const char*>(game, 0x38988),
                                            game::Field<int>(game, 0x38998))) {
    soeutil::StringFixed<128> message;
    soeutil::InitFixed(message, reinterpret_cast<void**>(0x142049dc8));
    soeutil::StringFormat(&message, reinterpret_cast<const char*>(0x14206d000),
                          game::Field<const char*>(game, 0x38988));  // "Failed to init the game world for: %s.  Exiting."
    using ShutdownFn = void (*)(uint8_t*, bool, int, const char*, const char*);
    (*reinterpret_cast<ShutdownFn**>(game))[0xD8 / 8](game, true, 0x16, message.data, nullptr);
    game::Call<void (*)(const char*, const char*, ...)>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x142046fb8), message.data);
    message.vtable = reinterpret_cast<void**>(0x142049da8);
    soeutil::StringRelease(&message);
    return false;
  }
  game::Field<uint8_t*>(game, 0x3D3E0)[0x3B648] = savedLoading;
  game::Call<void (*)(uint8_t*)>(0x14046e470)(game);
  game::Call<void (*)(void*, const char*, void*, bool, int)>(0x140675470)(
      game::Field<void*>(game, 0x38B58), game::Field<const char*>(game, 0x38988), game::Field<void*>(game, 0x38AF0), true,
      game::Field<int>(game, 0x3899C));
  alignas(16) uint8_t orientation[16];
  std::memcpy(orientation, reinterpret_cast<const void*>(0x142b06ac0), sizeof(orientation));
  void* position = player() ? static_cast<void*>(player() + 0x2E0)
                            : game::Call<void* (*)(void*)>(0x1404d5400)(game::Field<void*>(game, 0x38890));
  game::Call<void (*)(uint8_t*, void*, void*)>(0x14042adb0)(game, position, orientation);
  game::Call<void (*)(uint8_t*)>(0x1403dd960)(game);

  if (auto* root = *reinterpret_cast<uint8_t**>(0x142b19cc0)) {
    game::Call<void (*)(void*)>(0x14079c160)(game::Field<void*>(root, 0xB8));
    game::Call<void (*)(void*)>(0x14079fbc0)(game::Field<void*>(root, 0x130));
    if (uint8_t* self = player()) {
      uint64_t id = game::Field<uint64_t>(self, 0x18);
      game::Call<void (*)(void*, uint64_t*, int)>(0x14079eb10)(game::Field<void*>(root, 0x20), &id, 0x2A);
    }
  }

  if (*reinterpret_cast<void**>(0x142b195c8)) {  // web browser
    using GetStringFn = void (*)(void*, const char*, const char*, const char*, soeutil::IString*, bool, int, int);
    const char* section = reinterpret_cast<const char*>(0x142065fc8);  // "WebBrowser"
    soeutil::StringFixed<512> assets, resources, userData, logs;
    soeutil::StringFixed<512>* settings[] = {&assets, &resources, &userData, &logs};
    static const uint64_t kKeys[][2] = {
        {0x14206d050, 0x14206d038},  // "AssetListFilename", "WebBrowserAssets.txt"
        {0x14206d068, 0x14206d064},  // "ResourceDirectory", ""
        {0x14206d0a0, 0x14206d080},  // "UserDataDirectory", "./Resources/WebBrowser/UserData"
        {0x14206d0d8, 0x14206d0b8},  // "LogDirectory", "./Resources/WebBrowser/Logs"
    };
    for (int i = 0; i < 4; ++i) {
      soeutil::InitFixed(*settings[i], reinterpret_cast<void**>(0x14204aea0));
      game::Call<GetStringFn>(0x1403334f0)(game::Field<void*>(game, 0x38E30), section, reinterpret_cast<const char*>(kKeys[i][0]),
                                           reinterpret_cast<const char*>(kKeys[i][1]), settings[i], false, -1, -1);
    }
    game::Call<void (*)(void*, const char*, const char*, const char*, const char*)>(0x14078ab10)(
        *reinterpret_cast<void**>(0x142b195c8), assets.data, resources.data, userData.data, logs.data);
    for (int i = 3; i >= 0; --i) {
      settings[i]->vtable = reinterpret_cast<void**>(0x14204ae80);
      soeutil::StringRelease(settings[i]);
    }
  }

  auto options = [] { return *reinterpret_cast<uint8_t**>(0x142b199f0); };
  game::Call<void (*)(void*, int)>(0x140ab2e50)(options(), 0);
  game[0x3B6D4] = options()[0x2CB6];
  game[0x3B808] = options()[0x2CB7];
  void* display = game::Field<void*>(game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x38890), 0x40), 8);
  auto displayValue = [&](int slot) { return (*reinterpret_cast<int (***)(void*)>(display))[slot](display); };
  int third = displayValue(0xF8 / 8);
  int second = displayValue(0xF0 / 8);
  int first = displayValue(0x110 / 8);
  game::Call<void (*)(void*, int, int, int)>(0x1416ca600)(game::Field<void*>(game, 0x3B728), first, second, third);
  game::Call<void (*)(uint8_t*, int)>(0x14049c530)(game, game::Call<int (*)(void*)>(0x140aae8c0)(options()));
  game::Call<void (*)(uint8_t*, int)>(0x14049c1d0)(game, game::Field<int>(options(), 0x320C));
  game::Call<void (*)(uint8_t*, int)>(0x14049c180)(game, game::Field<int>(options(), 0x3210));
  game::Call<void (*)(uint8_t*, float)>(0x14049c210)(game, game::Field<float>(options(), 0x3218));
  game::Call<void (*)(uint8_t*, float)>(0x14049c5a0)(game, game::Field<float>(options(), 0x3214));
  soeutil::IString setting{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  soeutil::StringAssign(&setting, game::Field<const char*>(options(), 0x3170));
  game::Call<void (*)(uint8_t*, const char*)>(0x14049c680)(game, setting.data);
  game[0x3B730] = 1;
  game::Field<int>(game::Field<uint8_t*>(game, 0x388A0), 0x12590) = options()[0x32D4] != 0;
  game::Field<int>(game::Field<uint8_t*>(game, 0x388A0), 0x12594) = options()[0x32D5] != 0;
  bool flag = options()[0x32D6] != 0;
  void* subsystem = (*reinterpret_cast<void* (***)(uint8_t*)>(game))[0x88 / 8](game);
  (*reinterpret_cast<void (***)(void*, bool)>(subsystem))[0x70 / 8](subsystem, flag);
  uint8_t controlFlag = options()[0x32D7];
  if (auto* controls = game::Field<uint8_t*>(game, 0x388A0)) {
    controls[0x12570] = controlFlag;
    static const uint64_t kControlSets[][2] = {{0x14206d0e8, 0x2CB8}, {0x14206d0f8, 0x2D58}, {0x14206d108, 0x2DF8}};  // Infantry, GroundVehicle, Aircraft
    for (const auto& set : kControlSets) {
      soeutil::IString name{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      soeutil::StringAssign(&name, reinterpret_cast<const char*>(set[0]));
      game::Call<void (*)(void*, soeutil::IString*, uint8_t*)>(0x1406144a0)(controls, &name, options() + set[1]);
      name.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&name);
    }
  }
  if (uint8_t* self = player()) {
    uint8_t* camera = self + 0x9418;
    game::Call<void (*)(uint8_t*, uint8_t)>(0x1405fb7e0)(camera, options()[0x3281]);
    game::Call<void (*)(uint8_t*, uint8_t)>(0x1405fb7d0)(camera, options()[0x3282]);
    game::Call<void (*)(uint8_t*, uint8_t)>(0x1405fb7f0)(camera, options()[0x3283]);
    game::Call<void (*)(uint8_t*, uint8_t)>(0x1405fb800)(camera, options()[0x3284]);
  }

  // "WallOfData" telemetry settings, defaulting to the current values.
  void* wallIni = options() + 0x1D38;
  const char* wallSection = reinterpret_cast<const char*>(0x14206d128);  // "WallOfData"
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  using GetIntFn = int (*)(void*, const char*, const char*, int, bool, int, int);
  game[0x3B65E] = game::Call<GetBoolFn>(0x1403051c0)(wallIni, wallSection, reinterpret_cast<const char*>(0x14206d118), game[0x3B65E] != 0,
                                                     false, -1, -1);  // "Collecting"
  if (game::Call<GetBoolFn>(0x1403051c0)(wallIni, wallSection, reinterpret_cast<const char*>(0x14206d134), (game[0x3B65F] >> 7) != 0, false, -1,
                                         -1))  // "Input"
    game[0x3B65F] |= 0x80;
  else
    game[0x3B65F] &= 0x7F;
  if (game::Call<GetBoolFn>(0x1403051c0)(wallIni, wallSection, reinterpret_cast<const char*>(0x14206d140), ((game[0x3B65F] >> 6) & 1) != 0,
                                         false, -1, -1))  // "Framerate"
    game[0x3B65F] |= 0x40;
  else
    game[0x3B65F] &= 0xBF;
  game::Field<int>(game, 0x3B660) = game::Call<GetIntFn>(0x1403050e0)(wallIni, wallSection, reinterpret_cast<const char*>(0x14206d150),
                                                                      game::Field<int>(game, 0x3B660), false, -1, -1);  // "SampleRate"

  game::Call<void (*)(void*, int, bool)>(0x1406f1140)(game::Field<void*>(game, 0x38AE8), 0, true);
  if (void* audio = game::Field<void*>(game, 0x389E0)) {
    game::Call<void (*)(void*)>(0x1408174c0)(audio);
    game::Field<uint8_t*>(game, 0x389E0)[0x11E9D2] = 1;
  }
  using LogFn = void (*)(const char*, const char*, ...);
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206d160));  // "Initialized - Devices"
  game::Call<void (*)(uint8_t*, int)>(0x14046be80)(game, 0x10);
  game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206d178));  // "Successfully initialized the game client."
  setting.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&setting);
  return true;
}

// Packet 0x83 sub-packets (slot 101) share {vtable +0, opcode +8 (written as
// a qword 0x83), subType +0x10, fields +0x18...}; the reader fills the rest,
// so each one is built in a shared buffer as large as the original frame.
class Packet83 {
 public:
  // Packet 0x11 sub-packets (slot 93) use the same layout with opcode 0x11.
  Packet83(uint64_t vtable, int subType, uint8_t* storage, uint64_t opcode = 0x83) : bytes_(storage) {
    std::memset(bytes_, 0, kPacket83Bytes);
    Put<uint64_t>(0, vtable);
    Put<uint64_t>(8, opcode);
    Put<int>(0x10, subType);
  }
  template <class T>
  void Put(int offset, T value) {
    std::memcpy(bytes_ + offset, &value, sizeof(T));
  }
  template <class T>
  T Get(int offset) const {
    T value;
    std::memcpy(&value, bytes_ + offset, sizeof(T));
    return value;
  }
  uint8_t* At(int offset) { return bytes_ + offset; }
  static constexpr size_t kPacket83Bytes = 0x500;

 private:
  uint8_t* bytes_;
};

// Frees a reader-allocated array the way the game's inlined dtor does.
void FreePacketArray(void* data, int elementSize) {
  if (soeutil::ThreadAllocatorCount() == 0)
    game::Call<void (*)(void*)>(0x1402fc170)(data);  // operator delete[]
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, elementSize);
}

// 0x14040cd70 (slot 101): packet 0x83 - header {short opcode, int, byte
// subType}, then per sub-type read + apply (mostly to the local player).
void GameClientHandlePacket83(uint8_t* game, const uint8_t* data, int length) {
  if (!data) return;
  const uint8_t* end = data + length;
  bool failed = false;
  const uint8_t* cursor = data + 2;
  if (cursor > end) {
    failed = true;
    cursor = end;
  }
  if (cursor + 4 > end) {
    failed = true;
    cursor = end;
  } else {
    cursor += 4;
  }
  if (cursor + 1 > end) return;
  int subType = static_cast<int8_t>(*cursor);
  if (failed) return;

  alignas(16) uint8_t storage[Packet83::kPacket83Bytes];
  uint64_t defaultId = *reinterpret_cast<uint64_t*>(0x142b17e70);
  int invalidInt = *reinterpret_cast<int*>(0x142b186ac);
  auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
  using ReaderFn = void (*)(uint8_t*, ClientReader*);
  auto readWithReader = [&](Packet83& packet, uint64_t read) {
    ClientReader reader{data, length, data, end, 0};
    game::Call<ReaderFn>(read)(packet.At(0), &reader);
    return !static_cast<uint8_t>(reader.failed) && static_cast<int>(reader.end - reader.cursor) <= 0;
  };
  auto read = [&](Packet83& packet, uint64_t fn, bool flag) { return game::Call<ReadFn>(fn)(packet.At(0), data, length, flag); };
  auto blobReader = [](Packet83& packet, int pointerOffset, int lengthOffset) {
    auto* blob = packet.Get<const uint8_t*>(pointerOffset);
    int blobLength = packet.Get<int>(lengthOffset);
    return ClientReader{blob, blobLength, blob, blob + blobLength, 0};
  };
  void* world = game::Field<void*>(game, 0x3D4C0);

  switch (subType) {
    case 0x08: {
      Packet83 packet(0x142065e48, 8, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (readWithReader(packet, 0x140377d80) && player()) game::Call<void (*)(uint8_t*, uint8_t*)>(0x140631d20)(player(), packet.At(0));
      break;
    }
    case 0x0B: {
      Packet83 packet(0x142065e50, 0xB, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (read(packet, 0x14038eaf0, false) && player()) game::Call<void (*)(uint8_t*, uint8_t*)>(0x140631db0)(player(), packet.At(0x18));
      break;
    }
    case 0x0F: {
      Packet83 packet(0x142065e98, 0xF, storage);
      packet.Put<uint64_t>(0x18, 0x142065e78);  // embedded list {vtable, data, count}
      if (read(packet, 0x14038ee40, false) && player()) game::Call<void (*)(uint8_t*, uint8_t*)>(0x140631e90)(player(), packet.At(0));
      packet.Put<uint64_t>(0x18, 0x142065e78);
      game::Call<void (*)(uint8_t*, int)>(0x140459520)(packet.At(0x18), packet.Get<int>(0x28));
      FreePacketArray(packet.Get<void*>(0x20), 8);
      break;
    }
    case 0x11: {
      Packet83 packet(0x142065eb0, 0x11, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (readWithReader(packet, 0x140377710) && player()) {
        ClientReader blob = blobReader(packet, 0x28, 0x30);
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t, ClientReader*)>(0x140629a20)(player(), packet.At(0x18), packet.Get<uint8_t>(0x20), &blob);
      }
      break;
    }
    case 0x12: {
      Packet83 packet(0x142065eb8, 0x12, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (read(packet, 0x14038ebc0, false) && player())
        game::Call<void (*)(uint8_t*, uint8_t*, int, uint8_t)>(0x1406374e0)(player(), packet.At(0x18), packet.Get<int>(0x20), packet.Get<uint8_t>(0x24));
      break;
    }
    case 0x13: {
      Packet83 packet(0x142065ec0, 0x13, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (readWithReader(packet, 0x140377f90) && player()) {
        ClientReader blob = blobReader(packet, 0x28, 0x30);
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t, int, ClientReader*)>(0x140637510)(
            player(), packet.At(0x18), packet.Get<uint8_t>(0x20), packet.Get<int>(0x24), &blob);
      }
      break;
    }
    case 0x14: {  // read and discarded (sub-type field left 0 like the original)
      Packet83 packet(0x142065ea0, 0, storage);
      packet.Put<int>(0x18, invalidInt);
      packet.Put<int>(0x1C, invalidInt);
      ClientReader reader{data, length, data, end, 0};
      game::Call<ReaderFn>(0x140377a10)(packet.At(0), &reader);
      break;
    }
    case 0x15: {
      Packet83 packet(0x142065ec8, 0x15, storage);
      packet.Put<int>(0x1C, invalidInt);
      if (!read(packet, 0x14038ca80, true)) break;
      int key = packet.Get<int>(0x1C);
      if (key == game::Field<int>(game, 0x38BF8)) break;
      auto* object = game::Call<uint8_t* (*)(uint8_t*, int*)>(0x1403f8380)(game, &key);
      if (object && !(object[0x8D4] & 0x20))
        game::Call<void (*)(uint8_t*, int, const uint8_t*, int)>(0x1405147f0)(object, packet.Get<int>(0x18), data, length);
      break;
    }
    case 0x19: {
      Packet83 packet(0x142065ea8, 0, storage);
      packet.Put<int>(0x18, invalidInt);
      packet.Put<int>(0x1C, invalidInt);
      packet.Put<int>(0x20, invalidInt);
      if (readWithReader(packet, 0x1403778d0) && world) {
        bool flag = packet.Get<uint8_t>(0x40) != 0;
        int key = packet.Get<int>(0x20);
        uint64_t out;
        void* entry = game::Call<void* (*)(void*, uint64_t*, int*)>(0x140428fc0)(*reinterpret_cast<void**>(0x142b19780), &out, &key);
        game::Call<void (*)(void*, void*, uint8_t*, bool)>(0x14070b5d0)(game::Field<void*>(game, 0x3D4C0), entry, packet.At(0x28), flag);
      }
      break;
    }
    case 0x1B: {
      Packet83 packet(0x142065ed0, 0, storage);
      if (read(packet, 0x14038ef10, false)) {
        uint64_t id = packet.Get<uint64_t>(0x18);
        if (void* entity = game::Call<void* (*)(void*, uint64_t*)>(0x14071ee60)(game::Field<void*>(game, 0x38860), &id))
          game::Call<void (*)(void*, uint8_t)>(0x140514910)(entity, packet.Get<uint8_t>(0x20));
      }
      break;
    }
    case 0x1C: {
      Packet83 packet(0x142065ed8, 0x1C, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (read(packet, 0x14038ed30, false) && player())
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t, int)>(0x140637670)(player(), packet.At(0x18), packet.Get<uint8_t>(0x20), packet.Get<int>(0x24));
      break;
    }
    case 0x1E: {
      Packet83 packet(0x142065ee0, 0x1E, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      packet.Put<uint64_t>(0x30, 0x142063770);  // array {vtable, int* data, count}
      if (readWithReader(packet, 0x1403777f0) && player()) {
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t, int, int)>(0x140631cd0)(player(), packet.At(0x18), packet.Get<uint8_t>(0x20),
                                                                                packet.Get<int>(0x24), packet.Get<int>(0x28));
        if (game::Field<void*>(game, 0x3D4C0)) {
          for (int i = 0; i < packet.Get<int>(0x40); ++i) {
            int id = packet.Get<int*>(0x38)[i];
            uint64_t owner = game::Field<uint64_t>(game, 0x38BF0);
            if (auto* entry = game::Call<uint8_t* (*)(void*, uint64_t*, int)>(0x14070c6a0)(game::Field<void*>(game, 0x3D4C0), &owner, id)) {
              entry[0x620] |= 0x40;
              entry[0x621] |= 0x20;
            }
          }
        }
      }
      packet.Put<uint64_t>(0x30, 0x142063770);
      packet.Put<int>(0x40, 0);
      FreePacketArray(packet.Get<void*>(0x38), 4);
      break;
    }
    case 0x24: {
      Packet83 packet(0x142065ee8, 0x24, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      auto* text = reinterpret_cast<soeutil::IString*>(packet.At(0x20));
      *text = {soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      if (read(packet, 0x14038e860, false) && player()) game::Call<void (*)(uint8_t*, uint8_t*)>(0x140631b30)(player(), packet.At(0));
      text->vtable = soeutil::IStringVtable();
      soeutil::StringRelease(text);
      break;
    }
    case 0x25: {
      Packet83 packet(0x142065ef0, 0x25, storage);
      packet.Put<uint64_t>(0x18, defaultId);
      if (read(packet, 0x14038e990, false) && player()) game::Call<void (*)(uint8_t*, uint8_t*)>(0x140631c50)(player(), packet.At(0));
      break;
    }
    case 0x26: {
      Packet83 packet(0x142065ef8, 0x26, storage);
      packet.Put<uint64_t>(0x18, *reinterpret_cast<uint64_t*>(0x142b181f8));
      if (read(packet, 0x14038dc60, false) && world) {
        uint64_t owner = packet.Get<uint64_t>(0x18);
        auto* entry = game::Call<uint8_t* (*)(void*, uint64_t*, int)>(0x14070c6a0)(world, &owner, packet.Get<int>(0x20));
        if (entry) {
          if (void* effect = game::Call<void* (*)(uint8_t*)>(0x140702bb0)(entry)) {
            int value = packet.Get<int>(0x24);
            game::Call<void (*)(void*, int*)>(0x14143aea0)(effect, &value);
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

// Inlined IString prepend (the game copies the text down, then the prefix in).
void StringPrepend(soeutil::IString* text, const char* prefix) {
  int prefixLength = static_cast<int>(std::strlen(prefix));
  int total = text->length + prefixLength;
  if (total == 0) {
    soeutil::StringRelease(text);
    text->data = soeutil::EmptyStringData();
    text->length = 0;
    text->capacity = 0;
    return;
  }
  StringMakeWritable(text, total + 1);
  std::memmove(text->data + prefixLength, text->data, static_cast<size_t>(text->length + 1));
  std::memcpy(text->data, prefix, static_cast<size_t>(prefixLength));
  text->length = total;
}

template <class T>
T* ConstructNew(size_t size, uint64_t constructor) {
  void* memory = GameAllocate(size);
  return memory ? game::Call<T* (*)(void*)>(constructor)(memory) : nullptr;
}

// Intrusive handle at 0x142ae85e0: pointer | flag bit 0, object {vtable,
// refcount +8, ...}; the "SuppressAlerts" handler is a 0x28-byte functor
// {vtable 0x142046c78, refcount, game, function 0x140428c90, 0}.
void ReleaseAlertHandler() {
  auto& handle = *reinterpret_cast<uint64_t*>(0x142ae85e0);
  if (auto* object = reinterpret_cast<uint8_t*>(handle & ~1ull)) {
    if (_InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(object + 8), -1) == 1)
      (*reinterpret_cast<void (***)(void*, int)>(object))[0](object, 1);
  }
  handle &= 1;
}

// 0x14040ed60 (slot 2): Init(commandLine) - load the client ini (Inifile=,
// merged with LocalConfig.ini), user options, core state objects, the SoeData
// driver ("FilesystemRoot=<GraphicsDataPath>"), timers, the asset request
// handler and the Debug/UsePs4ControlEmulation flag.
bool GameClientInit(uint8_t* game, const char* commandLine) {
  using ParseFn = void (*)(void*, const char*);
  using GetBufferFn = void (*)(void*, const char*, const char*, const char*, char*, int, bool, int, int);
  const char* empty = reinterpret_cast<const char*>(0x142046fcb);
  auto* ini = ConstructNew<uint8_t>(0xE98, 0x140331340);
  game::Field<uint8_t*>(game, 0x38E30) = ini;
  game::Call<ParseFn>(0x140333e70)(ini, commandLine);
  const char* defaultIni = game::Call<const char* (*)(uint8_t*)>(0x14047aec0)(game);
  char iniPath[0x100];
  game::Call<GetBufferFn>(0x1403331e0)(game::Field<void*>(game, 0x38E30), empty, reinterpret_cast<const char*>(0x14204a3d0), defaultIni, iniPath,
                                       0x100, false, -1, -1);  // "Inifile"
  game::Call<void (*)(void*, bool)>(0x1403322c0)(game::Field<void*>(game, 0x38E30), true);
  soeutil::StringAssign(game + 0x38DF0, iniPath);

  soeutil::IString arguments{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  soeutil::StringAssign(&arguments, commandLine);
  const char* found = arguments.length > 0 ? game::Call<const char* (*)(const char*, const char*)>(0x140304100)(
                                                 arguments.data, reinterpret_cast<const char*>(0x14204a3d0))
                                           : nullptr;
  if (!found || static_cast<int>(found - arguments.data) == -1)
    game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(&arguments, reinterpret_cast<const char*>(0x142070918),
                                                                           iniPath);  // " Inifile=%s"
  game::Call<void (*)(uint8_t*, const char*)>(0x14034e580)(game, arguments.data);
  game::Call<void (*)(int, int)>(0x14032e890)(0, 0);
  if (!game::Call<bool (*)(void*, const char*)>(0x140334100)(game::Field<void*>(game, 0x38E30), iniPath)) {
    game::Call<void (*)(const char*, const char*)>(0x140309e00)(reinterpret_cast<const char*>(0x142070930), iniPath);  // "Failed to load the config file ..."
    game::Call<void (*)(int)>(0x140d5565c)(-1);  // exit
  }
  game::Call<ParseFn>(0x140333e70)(game::Field<void*>(game, 0x38E30), commandLine);

  auto* options = ConstructNew<uint8_t>(0x3390, 0x140aadde0);
  *reinterpret_cast<uint8_t**>(0x142b199f0) = options;
  game::Call<ParseFn>(0x140aae990)(options, commandLine);
  game::Call<void (*)(void*)>(0x140aae950)(options);
  game::Call<void (*)(void*)>(0x140ab7b20)(options);
  (*reinterpret_cast<void (***)(void*)>(options))[1](options);

  auto* localIni = ConstructNew<uint8_t>(0xE98, 0x140331340);
  game::Field<uint8_t*>(game, 0x38E38) = localIni;
  game::Call<ParseFn>(0x140333e70)(localIni, commandLine);
  char localPath[0x100];
  game::Call<GetBufferFn>(0x1403331e0)(game::Field<void*>(game, 0x38E38), empty, reinterpret_cast<const char*>(0x142070988),
                                       reinterpret_cast<const char*>(0x142070978), localPath, 0x100, false, -1, -1);  // "LocalConfig", "LocalConfig.ini"
  if (game::Call<bool (*)(const char*, bool)>(0x140336bd0)(localPath, false)) {
    game::Call<bool (*)(void*, const char*)>(0x140334100)(game::Field<void*>(game, 0x38E38), localPath);
    game::Call<void (*)(void*, void*, int)>(0x140335530)(game::Field<void*>(game, 0x38E30), game::Field<void*>(game, 0x38E38), 0);
  }
  game::Call<ParseFn>(0x140333e70)(game::Field<void*>(game, 0x38E38), commandLine);

  *reinterpret_cast<unsigned*>(0x142b176d0) = GetCurrentThreadId();
  *reinterpret_cast<uint8_t**>(0x142b19780) = game;
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  if ((*reinterpret_cast<bool (***)(uint8_t*)>(game))[0x90 / 8](game) &&
      game::Call<GetBoolFn>(0x1403051c0)(game::Field<void*>(game, 0x38E30), empty, reinterpret_cast<const char*>(0x142070998), false, false, -1,
                                         -1)) {  // "SuppressAlerts"
    ReleaseAlertHandler();
    ReleaseAlertHandler();
    auto* functor = static_cast<uint8_t*>(GameAllocate(0x28));
    if (functor) {
      game::Field<uint64_t>(functor, 0) = 0x142046c58;
      _InterlockedExchange64(reinterpret_cast<volatile long long*>(functor + 8), 1);
      game::Field<uint64_t>(functor, 0) = 0x142046c78;
      game::Field<uint8_t*>(functor, 0x10) = game;
      game::Field<uint64_t>(functor, 0x18) = 0x140428c90;
      game::Field<uint64_t>(functor, 0x20) = 0;
    }
    auto& handle = *reinterpret_cast<uint64_t*>(0x142ae85e0);
    handle = (handle & 1) | reinterpret_cast<uint64_t>(functor);
  }

  game::Field<void*>(game, 0x31498) = ConstructNew<void>(0xA8, 0x14039f820);
  void* stateMemory = GameAllocate(0x96DE8);
  game::Field<void*>(game, 0x314A8) = stateMemory ? game::Call<void* (*)(void*, uint8_t*)>(0x1403998d0)(stateMemory, game) : nullptr;
  auto now = [] {
    uint64_t value;
    return *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&value);
  };
  auto clock = [] {
    uint64_t value;
    return *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&value);
  };
  game::Field<uint64_t>(game, 0x38810) = now();
  game::Field<uint64_t>(game, 0x38800) = now();
  *reinterpret_cast<void**>(0x142b19790) = ConstructNew<void>(8, 0x1413438b0);
  auto* listener = static_cast<uint64_t*>(GameAllocate(8));
  if (listener) *listener = 0x142064da0;
  game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), 0x10) = listener;
  game::Call<void (*)()>(0x141341b90)();
  game::Field<uint64_t>(game, 0x3B698) = clock();
  game::Field<uint64_t>(game, 0x3B6A0) = now();
  game::Field<uint64_t>(game, 0x3B6A8) = clock();
  *reinterpret_cast<void**>(0x142b19ae0) = ConstructNew<void>(0x58, 0x140ab7b70);
  game::Field<uint64_t>(game, 0x31420) = 0;
  game::Field<int>(game, 0x31428) = 3;
  game[0x3142C] = 0;
  (*reinterpret_cast<void (***)(uint8_t*)>(game))[0x280 / 8](game);
  game::Call<void (*)(void*, const char*)>(0x14133dd40)(game::Field<void*>(game, 0x38E30), reinterpret_cast<const char*>(0x142063450));  // "CommandQueue"
  while (game::Call<int (*)()>(0x14133c270)() != 0) {
  }

  // SoeData file system driver.
  auto& dataManager = *reinterpret_cast<uint8_t**>(0x143c76ab8);
  if (!dataManager) dataManager = ConstructNew<uint8_t>(0x138, 0x1414e71d0);
  auto* driver = ConstructNew<uint8_t>(0x20, 0x1414f0ce0);
  soeutil::StringFixed<128> config;
  soeutil::InitFixed(config, reinterpret_cast<void**>(0x142049dc8));
  using GetStringFn = void (*)(void*, const char*, const char*, const char*, soeutil::IString*, bool, int, int);
  game::Call<GetStringFn>(0x1403334f0)(game::Field<void*>(game, 0x38E30), reinterpret_cast<const char*>(0x1420709d0),
                                       reinterpret_cast<const char*>(0x1420709b8), reinterpret_cast<const char*>(0x1420709a8), &config, false, -1,
                                       -1);  // "Libraries", "GraphicsDataPath", "../GraphicsData"
  StringPrepend(&config, reinterpret_cast<const char*>(0x1420709e0));  // "FilesystemRoot=\""
  game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(
      &config, reinterpret_cast<const char*>(0x1420709f8));  // "\" ClassDefinition=\"DeepDefinitions\" WatchChanges=false"
  if (!(*reinterpret_cast<bool (***)(void*, const char*)>(driver))[1](driver, config.data)) {
    game::Call<void (*)(const char*, const char*)>(0x1403243b0)(nullptr, reinterpret_cast<const char*>(0x142070a30));  // "Failed to initialize SoeData driver"
    game::Call<void (*)()>(0x14032dc60)();
  }
  uint8_t* manager = dataManager;
  if (!game::Field<void*>(driver, 8) && game::Field<uint8_t*>(manager, 0x28) != driver) {  // push onto the driver list
    game::Field<uint8_t*>(driver, 0x10) = game::Field<uint8_t*>(manager, 0x28);
    if (auto* head = game::Field<uint8_t*>(manager, 0x28))
      game::Field<uint8_t*>(head, 8) = driver;
    else
      game::Field<uint8_t*>(manager, 0x30) = driver;
    game::Field<uint8_t*>(manager, 0x28) = driver;
    ++game::Field<int>(manager, 0x38);
    manager = dataManager;
  }
  game::Call<void (*)(void*)>(0x1414ebed0)(manager);

  uint64_t value;
  game::Field<uint64_t>(game, 0x390E8) = *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fc80)(&value);
  game::Field<uint64_t>(game, 0x390F0) = 0;
  std::memcpy(game + 0x3B640, reinterpret_cast<const void*>(0x142072930), 16);
  game::Field<int>(game, 0x38998) = 0x7FFFFFFF;
  game::Field<uint64_t>(game, 0x38F60) = now();
  std::memset(game + 0x38F88, 0, 0x390BC - 0x38F88);
  game::Field<uint64_t>(game, 0x3B6C0) = now();
  game::Field<uint64_t>(game, 0x31E00) = now();
  game::Field<uint64_t>(game, 0x3B6D8) = 0;
  game[0x3B6E0] = 0;
  game::Field<uint64_t>(game, 0x3B6E8) = now();
  game[0x3B7DC] = 1;

  // Clock offset: shift the clock by the span {0, 1, 1, 0, 0}, measure it back.
  uint64_t start = clock();
  struct ClockSpan {
    int a, b, c;
    uint8_t rest[16];
  } span{0, 1, 1, {}};
  static_assert(sizeof(ClockSpan) == 0x1C);
  uint64_t shifted = start;
  game::Call<void (*)(uint64_t*, ClockSpan*, bool)>(0x14032fbe0)(&shifted, &span, true);
  uint64_t measured;
  game::Call<void (*)(uint64_t*, ClockSpan*, bool)>(0x14032fb60)(&measured, &span, false);
  game::Field<int>(game, 0x38824) = static_cast<int>(measured) - static_cast<int>(start);

  game::Call<void (*)(uint8_t*, int, int, int, const char*)>(0x14166a6f0)(game::Field<uint8_t*>(game, 0x314A8) + 0x96A18, 1, 0, 2,
                                                                         reinterpret_cast<const char*>(0x142070a58));  // "AssetRequestHandler"
  game::Call<void (*)(uint8_t*)>(0x14047b900)(game);
  game::Call<void (*)()>(0x140ac9260)();
  game::Field<void*>(game, 0x314D8) = ConstructNew<void>(0x60B28, 0x14166d170);
  game::Call<void (*)(uint8_t*)>(0x14049d110)(game);

  void* rootIni = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19780), 0x38E30);
  char defaultValue[0x400];
  game::Call<void (*)(char*, int, int, int)>(0x1403054d0)(defaultValue, 0x400, 0, 0);
  char rawValue[0x400];
  game::Call<GetBufferFn>(0x1403334e0)(rootIni, reinterpret_cast<const char*>(0x142070a88), reinterpret_cast<const char*>(0x142070a70), defaultValue,
                                       rawValue, 0x400, false, -1, -1);  // "Debug", "UsePs4ControlEmulation"
  char flag[0x800];
  flag[0] = 0;
  game::Call<void (*)(const char*, char*, int, char, bool, bool)>(0x1403309b0)(rawValue, flag, 0x800, 0, true, true);
  game[0x42E40] = flag[0] == '1' || flag[0] == 't' || flag[0] == 'T';

  config.vtable = reinterpret_cast<void**>(0x142049da8);
  soeutil::StringRelease(&config);
  arguments.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&arguments);
  return true;
}

// Empties a string the way the game does before rebuilding it.
void StringClear(soeutil::IString* text) {
  soeutil::StringRelease(text);
  text->data = soeutil::EmptyStringData();
  text->length = 0;
  text->capacity = 0;
}

// The overlay words are spelled from the character table at 0x142a00318
// ("sghoaicdmtenpr") so they do not appear as plain strings in the exe.
char OverlayChar(int index) { return reinterpret_cast<const char*>(0x142a00318)[index]; }

// 0x1403e7ea0 (slot 103): draw the status overlay - "godmode" (local player
// flag bit 25 at +0x1AD8), "hidden" (bit 31), "spectator" (player bit at
// +0x109DB unless suppressed by 0x142b176cc), and, while the +0x388A8 mode
// is 0x29, the name of the entry for +0x38E50 from *0x142b19cd0.
void GameClientDrawStatusOverlay(uint8_t* game) {
  void* display = game::Field<void*>(game, 0x38890);
  auto* self = game::Call<uint8_t* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860));
  if (game[0x3D518] || !self || !display) return;
  using DrawFn = void (*)(void*, const char*, float, float, uint32_t);
  auto draw = [&](const char* text, float x, float y, uint32_t color) { game::Call<DrawFn>(0x1404d8ff0)(display, text, x, y, color); };
  float margin = *reinterpret_cast<float*>(0x14207289c);
  auto suppressed = [] { return game::Call<bool (*)()>(0x1406ed8b0)(); };
  soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  auto spell = [&](std::initializer_list<int> letters) {
    StringClear(&text);
    for (int letter : letters) StringAppendChar(&text, OverlayChar(letter));
  };
  unsigned flags = game::Field<unsigned>(self, 0x1AD8);
  if ((flags >> 25) & 1 && !suppressed()) {
    spell({1, 3, 7, 8, 3, 7, 10});  // "godmode"
    draw(text.data, margin, margin, 0xFEF00000);
  }
  flags = game::Field<unsigned>(self, 0x1AD8);
  if ((flags >> 31) & 1 && !suppressed()) {
    spell({2, 5, 7, 7, 10, 11});  // "hidden"
    draw(text.data, *reinterpret_cast<float*>(0x1420728b4), margin, 0xFE00F000);
  }
  auto* player = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80);
  if (player && (player[0x109DB] & 1) && !*reinterpret_cast<uint8_t*>(0x142b176cc)) {
    spell({0, 12});  // "sp", then the rest through the out-of-line append
    static const int kRest[] = {10, 6, 9, 4, 9, 3, 13};  // "ectator"
    for (int letter : kRest) game::Call<void (*)(soeutil::IString*, char)>(0x140313b40)(&text, OverlayChar(letter));
    draw(text.data, *reinterpret_cast<float*>(0x1420728bc), margin, 0xFEF0F000);
  }
  if (void* mode = game::Field<void*>(game, 0x388A8)) {
    if ((*reinterpret_cast<int (***)(void*)>(mode))[0](mode) == 0x29) {
      uint64_t key = game::Field<uint64_t>(game, 0x38E50);
      if (auto* table = *reinterpret_cast<uint8_t**>(0x142b19cd0)) {
        if (auto* entry = game::Call<uint8_t* (*)(uint8_t*, uint64_t*)>(0x1403ea6f0)(table + 0x80, &key)) {
          float width = static_cast<float>(game::Call<int (*)(void*)>(0x1404d5820)(game::Field<void*>(game, 0x38890)));
          float height = static_cast<float>(game::Call<int (*)(void*)>(0x1404d5d30)(game::Field<void*>(game, 0x38890)));
          StringClear(&text);
          game::Call<void (*)(soeutil::IString*, void*)>(0x140306d10)(&text, entry + 8);
          draw(text.data, height * *reinterpret_cast<float*>(0x142072824), width * *reinterpret_cast<float*>(0x142072868), 0xFEF0F000);
        }
      }
    }
  }
  text.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&text);
}

// 0x1403db810 (slot 78): CreateAssetSystem - read the [AssetDelivery] settings,
// build the asset search path (".;<Solo AdditionalPaths>;<AdditionalPaths>"),
// create the direct and/or indirect asset system at +0x3D3C0, wait up to 600
// clock ticks for its manifest, then create the asset loader at +0x3D3C8 with
// a memory budget picked from the machine's memory.
bool GameClientCreateAssetSystem(uint8_t* game) {
  const char* section = reinterpret_cast<const char*>(0x14206c140);  // "AssetDelivery"
  auto ini = [&] { return game::Field<void*>(game, 0x38E30); };
  using GetIntFn = int (*)(void*, const char*, const char*, int, bool, int, int);
  using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
  using GetStringFn = void (*)(void*, const char*, const char*, const char*, soeutil::IString*, bool, int, int);
  using ErrorFn = void (*)(const char*, const char*, ...);
  using LogFn = void (*)(const char*, const char*, ...);
  const char* percentS = reinterpret_cast<const char*>(0x142046fb8);
  int threadCount = game::Call<GetIntFn>(0x1403050e0)(ini(), section, reinterpret_cast<const char*>(0x14206c128), 1, true, -1, -1);  // "DirectThreadCount"
  bool direct = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206c150), true, true, -1, -1);  // "DirectEnabled"
  bool indirect = game::Call<GetBoolFn>(0x1403051c0)(ini(), section, reinterpret_cast<const char*>(0x14206c160), true, true, -1, -1);  // "IndirectEnabled"
  soeutil::StringFixed<256> serverAddress;
  soeutil::InitFixed(serverAddress, reinterpret_cast<void**>(0x142049e08));
  game::Call<GetStringFn>(0x1403334f0)(ini(), section, reinterpret_cast<const char*>(0x14206c180), reinterpret_cast<const char*>(0x14206c170),
                                       &serverAddress, true, -1, -1);  // "IndirectServerAddress", "127.0.0.1:23777"
  game[0x38A38] = game::Call<GetBoolFn>(0x1403051c0)(ini(), reinterpret_cast<const char*>(0x142046fcb), reinterpret_cast<const char*>(0x14206c198), true,
                                                     false, -1, -1);  // "PreLoadPcModels"
  const char kDot[2] = {'.', 0};
  const char* separator = reinterpret_cast<const char*>(0x14206c1c0);  // ";"
  auto append = [](soeutil::IString* text, const char* more) { game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(text, more); };

  soeutil::StringFixed<2048> paths;
  soeutil::InitFixed(paths, reinterpret_cast<void**>(0x142049e28));
  soeutil::StringAssign(&paths, kDot);
  paths.vtable = reinterpret_cast<void**>(0x142049e48);
  auto appendExtraPaths = [&](const char* fromSection, soeutil::StringFixed<1024>& extra) {
    game::Call<GetStringFn>(0x1403334f0)(ini(), fromSection, reinterpret_cast<const char*>(0x14206c1a8), reinterpret_cast<const char*>(0x142046fcb),
                                         &extra, false, -1, -1);  // "AdditionalPaths"
    if (extra.length > 0) {
      append(&paths, separator);
      append(&paths, extra.data);
    }
  };
  if (game[0x3883A]) {
    soeutil::StringFixed<1024> soloPaths;
    soeutil::InitFixed(soloPaths, reinterpret_cast<void**>(0x14204b2f0));
    appendExtraPaths(reinterpret_cast<const char*>(0x14206c1b8), soloPaths);  // "Solo"
    soloPaths.vtable = reinterpret_cast<void**>(0x14204b2d0);
    soeutil::StringRelease(&soloPaths);
  }
  soeutil::StringFixed<1024> extraPaths;
  soeutil::InitFixed(extraPaths, reinterpret_cast<void**>(0x14204b2f0));
  soeutil::StringFixed<1024> packDirectory;
  soeutil::InitFixed(packDirectory, reinterpret_cast<void**>(0x14204b2f0));
  appendExtraPaths(section, extraPaths);
  game::Call<GetStringFn>(0x140333550)(ini(), section, reinterpret_cast<const char*>(0x14206c1c8), kDot, &packDirectory, false, -1, -1);  // "PackFileDir"
  soeutil::StringFixed<256> cacheDirectory;
  soeutil::InitFixed(cacheDirectory, reinterpret_cast<void**>(0x142049e08));
  (*reinterpret_cast<void (***)(uint8_t*, soeutil::IString*)>(game))[0x290 / 8](game, &cacheDirectory);

  const char* assets = reinterpret_cast<const char*>(0x14206c1d4);  // "Assets"
  bool ok = false;
  uint8_t* system = nullptr;
  bool created = true;
  if (direct && indirect) {
    void* memory = GameAllocate(0xC08);
    system = memory ? game::Call<uint8_t* (*)(void*, const char*, int, const char*, const char*, const char*, const char*, bool)>(0x14136af50)(
                          memory, paths.data, threadCount, packDirectory.data, assets, serverAddress.data, cacheDirectory.data, false)
                    : nullptr;
  } else if (direct) {
    void* memory = GameAllocate(0x1460);
    system = memory ? game::Call<uint8_t* (*)(void*, const char*, int, int)>(0x141368ac0)(memory, paths.data, threadCount, 0) : nullptr;
  } else if (indirect) {
    void* memory = GameAllocate(0x28AC0);
    system = memory ? game::Call<uint8_t* (*)(void*, const char*, const char*, const char*, const char*, int)>(0x141360870)(
                          memory, packDirectory.data, assets, serverAddress.data, cacheDirectory.data, 0)
                    : nullptr;
  } else {
    created = false;
    game::Call<ErrorFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206c1e0));  // "... neither direct or indirect were enabled"
  }

  if (created) {
    game::Field<uint8_t*>(game, 0x3D3C0) = system;
    auto assetSystem = [&] { return game::Field<uint8_t*>(game, 0x3D3C0); };
    auto vtable = [&] { return *reinterpret_cast<void***>(assetSystem()); };
    using DescribeFn = void (*)(void*, soeutil::IString*);
    using FailFn = void (*)(int, const char*, bool, void*);
    int error = reinterpret_cast<int (*)(void*)>(vtable()[1])(assetSystem());
    if (error != 0) {
      soeutil::StringFixed<128> message;
      soeutil::InitFixed(message, reinterpret_cast<void**>(0x142049dc8));
      soeutil::StringFixed<128> reason;
      soeutil::InitFixed(reason, reinterpret_cast<void**>(0x142049dc8));
      reinterpret_cast<DescribeFn>(vtable()[0x60 / 8])(assetSystem(), &reason);
      soeutil::StringFormat(&message, reinterpret_cast<const char*>(0x14206c250), error, reason.data);  // "... Error code: %d. Reason: %s"
      game::Call<ErrorFn>(0x1402baba0)(nullptr, percentS, message.data);
      if (uint8_t* broken = assetSystem()) (*reinterpret_cast<void (***)(void*, int)>(broken))[0](broken, 1);
      game::Call<FailFn>(0x1403e1d80)(0x1B, message.data, true, nullptr);
      reason.vtable = reinterpret_cast<void**>(0x142049da8);
      soeutil::StringRelease(&reason);
      message.vtable = reinterpret_cast<void**>(0x142049da8);
      soeutil::StringRelease(&message);
    } else {
      game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206c2b0));  // "Initialized - Asset Delivery"
      soeutil::StringFixed<128> status;
      soeutil::InitFixed(status, reinterpret_cast<void**>(0x142049dc8));
      auto clock = [] {
        uint64_t value;
        return *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&value);
      };
      auto ready = [&] { return reinterpret_cast<bool (*)(void*)>(vtable()[0x58 / 8])(assetSystem()); };
      uint64_t start = clock();
      while (!ready()) {
        if (static_cast<int>(clock()) - static_cast<int>(start) >= 600 || status.length != 0) break;
        reinterpret_cast<void (*)(void*, int)>(vtable()[0xD0 / 8])(assetSystem(), -1);
        reinterpret_cast<DescribeFn>(vtable()[0x60 / 8])(assetSystem(), &status);
        game::Call<void (*)(unsigned)>(0x14032ec60)(25);  // Sleep(25)
      }
      if (!ready()) {
        soeutil::StringFixed<128> message;
        soeutil::InitFixed(message, reinterpret_cast<void**>(0x142049dc8));
        soeutil::StringFormat(&message, reinterpret_cast<const char*>(0x14206c2d0), status.data);  // "... failed to receive manifest: %s"
        game::Call<ErrorFn>(0x1402baba0)(nullptr, percentS, message.data);
        if (uint8_t* broken = assetSystem()) (*reinterpret_cast<void (***)(void*, int)>(broken))[0](broken, 1);  // +0x3D3C0 is left dangling, as in the original
        game::Call<FailFn>(0x1403e1d80)(0x1B, message.data, true, nullptr);
        game::Call<void (*)(soeutil::IString*)>(0x1402bace0)(&message);  // StringFixed<128> destructor
      } else {
        game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206c318));  // "Initialized - Manifest"
        *reinterpret_cast<uint8_t**>(0x142b19af0) = assetSystem();
        void** systemVtable = vtable();
        void* stringTable = game::Call<void* (*)(void*, bool)>(0x140481b20)(game::Field<void*>(game, 0x3B6B8), true);
        reinterpret_cast<void (*)(void*, void*)>(systemVtable[0xA0 / 8])(assetSystem(), stringTable);
        game::Call<void (*)(uint8_t*)>(0x14047acd0)(game);
        // Loader cache budget from physical memory (+1%): 10 MB / 64 MB / 256 MB.
        uint64_t budget = 0x10000000;
        int64_t memoryBytes;
        if (game::Call<bool (*)(int64_t*)>(0x141341e10)(&memoryBytes)) {
          int64_t padded = memoryBytes * 101 / 100;
          if (padded <= static_cast<int64_t>(*reinterpret_cast<int*>(0x14219ddf4)) << 30)
            budget = 0xA00000;
          else if (padded <= static_cast<int64_t>(*reinterpret_cast<int*>(0x14219ddf8)) << 30)
            budget = 0x4000000;
        }
        void* loaderMemory = GameAllocate(0x96BA8);
        auto* loader = loaderMemory ? game::Call<uint8_t* (*)(void*, void*)>(0x141355630)(loaderMemory, assetSystem()) : nullptr;
        game::Field<uint8_t*>(game, 0x3D3C8) = loader;
        (*reinterpret_cast<void (***)(void*, bool)>(loader))[0x98 / 8](loader, true);
        game::Field<uint64_t>(game::Field<uint8_t*>(game, 0x3D3C8), 0x30) = budget;
        game::Field<uint64_t>(game::Field<uint8_t*>(game, 0x3D3C8), 0x50) = 0x800;
        game::Field<int>(game::Field<uint8_t*>(game, 0x3D3C8), 0x24) = 5000;
        *reinterpret_cast<uint8_t**>(0x142ae89a8) = game::Field<uint8_t*>(game, 0x3D3C8);
        *reinterpret_cast<uint8_t**>(0x142b19af8) = game::Field<uint8_t*>(game, 0x3D3C8);
        alignas(16) uint8_t registration[16];
        game::Call<void (*)(void*, void*, void*)>(0x14077fde0)(registration, game::Field<void*>(game, 0x3D3C8), game::Field<void*>(game, 0x3D3C0));
        game::Call<void (*)(void*)>(0x1403a97f0)(registration);
        ok = true;
      }
      status.vtable = reinterpret_cast<void**>(0x142049da8);
      soeutil::StringRelease(&status);
    }
  }

  cacheDirectory.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&cacheDirectory);
  packDirectory.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&packDirectory);
  extraPaths.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&extraPaths);
  paths.vtable = reinterpret_cast<void**>(0x142049e28);
  soeutil::StringRelease(&paths);
  serverAddress.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&serverAddress);
  return ok;
}

// Spin-wait backoff the game inlines: pause, then yield, then Sleep(0)/Sleep(1).
void SpinBackoff(int& spins) {
  if (spins < 25)
    _mm_pause();
  else if (spins < 27)
    SwitchToThread();
  else if (spins < 29)
    Sleep(0);
  else {
    Sleep(1);
    spins -= 4;
  }
  ++spins;
}

// 0x14043c0e0 (slot 89): Update(time, deltaMs) - the per-frame tick. Each
// subsystem is timed with the high-resolution timer (0x14032fde0) and the lap
// stored in the frame profile at state+0xA10 (+0x180..+0x430).
void GameClientUpdate(uint8_t* game, int time, int delta) {
  using TimerFn = uint64_t* (*)(uint64_t*);
  auto timer = [] {
    uint64_t value;
    return *game::Call<TimerFn>(0x14032fde0)(&value);
  };
  uint64_t last = timer();
  uint8_t* profile = game::Field<uint8_t*>(game, 0x314A8) + 0xA10;
  auto lap = [&](int offset) {
    uint64_t now = timer();
    game::Field<uint64_t>(profile, offset) = now - last;
    last = now;
  };
  auto lapAdd = [&](int offset) {
    uint64_t now = timer();
    game::Field<uint64_t>(profile, offset) += now - last;
    last = now;
  };
  auto field = [&](int offset) { return game::Field<uint8_t*>(game, offset); };
  auto call = [](uint64_t address, auto... args) { game::Call<void (*)(decltype(args)...)>(address)(args...); };
  auto vcall = [](void* object, int offset, auto... args) {
    (*reinterpret_cast<void (***)(void*, decltype(args)...)>(object))[offset / 8](object, args...);
  };
  auto display = [&] { return field(0x38890); };
  auto window = [&] { return field(0x38898); };

  if (delta > 0) {
    if (uint8_t* recorder = field(0x38DC8); recorder && recorder[0x20]) call(0x14083c2f0, recorder);
    lap(0x180);
    game::Call<void (*)(void*, float)>(0x141868970)(field(0x3B6F8), game::Field<float>(display(), 0x2F4));
    call(0x141868a80, field(0x3B6F8), time, delta, 3);
    lap(0x1D8);
    if (!game[0x38F50]) {
      if (uint8_t* a = field(0x389B0)) call(0x141877540, a);
      if (uint8_t* b = field(0x389B8)) call(0x1418770b0, b);
    }
    lap(0x1E0);
    call(0x1409fdb70, field(0x38B78), delta);
    lap(0x1E8);
    if (void* global = *reinterpret_cast<void**>(0x142b19bb0)) call(0x14077ae00, global, delta);
    if (uint8_t* a = field(0x3B7C0)) call(0x1406f6ca0, a);
    if (uint8_t* a = field(0x38A10)) call(0x140996270, a, delta);
    if (uint8_t* a = field(0x38A18)) call(0x140997c50, a, delta);
    lap(0x188);

    // Screen fade: move +0x3B650 toward +0x3B654 over the time left in +0x3B658.
    float one = *reinterpret_cast<float*>(0x1425ba090);
    float current = game::Field<float>(game, 0x3B650);
    float target = game::Field<float>(game, 0x3B654);
    if (current != target) {
      float step = (std::min)(static_cast<float>(delta), *reinterpret_cast<float*>(0x1425ba0a0));
      float remaining = (std::max)(game::Field<float>(game, 0x3B658), step);
      game::Field<float>(game, 0x3B650) = (target - current) * (step / remaining) + current;
      game::Field<float>(game, 0x3B658) -= step;
      auto* effect = game::Call<uint8_t* (*)(void*, unsigned)>(0x14128f190)(game::Field<void*>(display(), 0x48), 0x5C3E6A59);
      if (effect) {
        struct FloatParameter {
          void** vtable;
          float value;
        } parameter{reinterpret_cast<void**>(0x1420636e0), (std::max)((std::min)(game::Field<float>(game, 0x3B650), one), 0.0f)};
        // Find parameter 0x9D60ACD4 in the effect's {key, index, next} list.
        uint8_t* slot = nullptr;
        for (auto* node = game::Field<uint8_t*>(game::Field<uint8_t*>(effect, 0x10), 0xE0); node; node = game::Field<uint8_t*>(node, 8)) {
          if (game::Field<unsigned>(node, 0) == 0x9D60ACD4u) {
            int index = game::Field<int>(node, 4);
            if (index >= 0) slot = game::Field<uint8_t**>(effect, 0x20)[index];
            break;
          }
        }
        game::Call<void (*)(uint8_t*, FloatParameter*)>(0x141287680)(slot + 0x10, &parameter);
      }
    }
    lap(0x1F0);
    call(0x141426fe0, game::Field<void*>(field(0x314A8), 0x96BB0), int64_t{-1});
    if (uint8_t* a = field(0x3D4C0)) call(0x14070fa60, a);
    lap(0x1F8);
    if (uint8_t* a = field(0x38828)) vcall(a, 0x80);
    lap(0x190);

    // Camera.
    uint8_t* cameraSystem = game + 0x42E80;
    auto* camera = game::Call<uint8_t* (*)(uint8_t*)>(0x1402f39f0)(cameraSystem);
    if (!game[0x3B770]) {
      float& zoom = game::Field<float>(camera, 0x70);
      zoom = (std::max)((std::min)(zoom, *reinterpret_cast<float*>(0x14204791c)), 0.0f);
      call(0x1402f9680, camera);
    }
    uint8_t* view = game::Field<uint8_t*>(camera, 0x88);
    uint8_t* renderView = game::Field<uint8_t*>(display(), 0x50);
    alignas(16) uint8_t viewMatrix[0x40];
    alignas(16) uint8_t projectionMatrix[0x40];
    std::memcpy(viewMatrix, renderView + 0x50, sizeof(viewMatrix));
    std::memcpy(projectionMatrix, renderView + 0x130, sizeof(projectionMatrix));
    vcall(view, 0x108, static_cast<void*>(viewMatrix));
    vcall(view, 0x110, static_cast<void*>(projectionMatrix));
    call(0x1402f51c0, cameraSystem, game::Call<void* (*)(void*)>(0x1404d5400)(display()));
    call(0x1402f51a0, cameraSystem, game::Field<uint8_t*>(display(), 0x50) + 0x20);
    call(0x1402f51b0, cameraSystem, game::Field<uint8_t*>(display(), 0x50) + 0x30);
    call(0x1402f5570, cameraSystem, delta, true);
    lap(0x200);
    if (!game[0x3B770]) call(0x1402f96a0, camera);
    lapAdd(0x200);
    call(0x1403f4840, game, time, delta);
    lap(0x210);
    last = timer();
    if (uint8_t* a = field(0x38860)) call(0x140722d90, a, delta);
    lap(0x220);
    call(0x14047c240, game);
    call(0x14047c8b0, game);
    lap(0x1A0);

    auto minimized = [] {
      auto* device = game::Field<uint8_t*>(game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b19788), 0x40), 8);
      return (*reinterpret_cast<bool (***)(void*)>(device))[0x20 / 8](device);
    };
    using ClockFn = uint64_t* (*)(uint64_t*);
    if (minimized()) {
      call(0x14043d5a0, game, delta);
      call(0x14043d330, game);
      lap(0x298);
      call(0x140471700, game);
    } else {
      // Frame-time smoothing at +0x390F0.
      uint64_t previous = game::Field<uint64_t>(game, 0x390E8);
      uint64_t clockValue;
      int64_t elapsed = static_cast<int64_t>(*game::Call<ClockFn>(0x14032fc80)(&clockValue) - previous);
      float smoothed = static_cast<float>(elapsed) * *reinterpret_cast<float*>(0x142047918) -
                       game::Field<float>(window(), 0x17C) * *reinterpret_cast<float*>(0x1420728c8);
      smoothed = (smoothed + game::Field<float>(game, 0x390F0)) * *reinterpret_cast<float*>(0x1425ba080);
      game::Field<float>(game, 0x390F0) = smoothed;
      call(0x140ac5e60, window());
      game::Field<uint64_t>(game, 0x390E8) = *game::Call<ClockFn>(0x14032fc80)(&clockValue);
      call(0x140477bc0, game);
      call(0x14043d5a0, game, delta);
      if (field(0x388A8)) {
        game::Call<void (*)()>(0x141426830)();
        vcall(field(0x388A8), 0xF8);
      }
      lap(0x198);

      if (!game[0x38EF1]) {
        uint8_t* renderLock = game + 0x38AF8;
        if (game::Call<bool (*)(void*)>(0x14032f2e0)(renderLock)) {  // try-lock
          call(0x14032f270, renderLock);                             // lock
          call(0x14032f360, renderLock);                             // unlock
          lap(0x228);
          if (!minimized()) {
            call(0x140ac3c30, window());
            lap(0x238);
            call(0x140ac3c80, window(), true);
            lap(0x240);
            call(0x1404d4230, display(), game[0x3BB20] == 0);
            call(0x140915550, *reinterpret_cast<void**>(0x142b19b00));
            lap(0x1A8);
            if (uint8_t* a = field(0x38B60); a && game[0x38EB3]) call(0x140a8dce0, a);
            lap(0x248);
            float seconds = static_cast<float>(delta) * *reinterpret_cast<float*>(0x142047918);
            game::Call<void (*)(uint8_t*, float)>(0x1402f57e0)(cameraSystem, seconds);
            call(0x1402f3220, cameraSystem);
            lap(0x250);
            call(0x140ac4420, window());
            lap(0x258);

            // Listener position from the local player.
            void* listener = *reinterpret_cast<void**>(0x142b19b20);
            alignas(16) uint8_t position[16] = {};
            if (auto* self = game::Call<uint8_t* (*)(void*)>(0x14071e830)(field(0x38860))) {
              auto* source = (*reinterpret_cast<uint8_t* (***)(void*)>(self))[0x188 / 8](self);
              std::memcpy(position, source, 16);
              call(0x1418fe3f0, listener, static_cast<void*>(position));
            }
            auto* viewer = game::Call<uint8_t* (*)(void*)>(0x14071e830)(*reinterpret_cast<void**>(0x142b19b38));
            auto* viewerAgain = game::Call<uint8_t* (*)(void*)>(0x14071e830)(*reinterpret_cast<void**>(0x142b19b38));
            if (viewer) {
              uint64_t scratch[2];
              void* orientation;
              if ((*reinterpret_cast<bool (***)(void*)>(field(0x388A8)))[0x180 / 8](field(0x388A8)))
                orientation = game::Call<void* (*)(void*, uint64_t*)>(0x140927e90)(viewerAgain, &scratch[0]);
              else
                orientation = (*reinterpret_cast<void* (***)(void*, uint64_t*)>(viewer))[0x58 / 8](viewer, &scratch[1]);
              call(0x14186e3b0, listener, orientation);
            }
            if (void* scene = game::Field<void*>(display(), 0x80)) call(0x141307960, scene, game::Field<void*>(display(), 0x48));
            lap(0x260);

            uint8_t* state = field(0x314A8);
            void* streamer = game::Field<void*>(state, 0x96A10);
            if (streamer && (*reinterpret_cast<int (***)(void*)>(streamer))[0x30 / 8](streamer) > 0) {
              call(0x140477ff0, game::Field<void*>(field(0x314A8), 0xF78), -1);
              lap(0x268);
            } else {
              game::Field<uint64_t>(profile, 0x268) = 0;
              lap(0x270);
            }
            vcall(game, 0x338);
            static const int kSummed[] = {0x248, 0x318, 0x310, 0x308, 0x300, 0x2F0, 0x2E8, 0x320};
            uint64_t total = game::Field<uint64_t>(profile, 0x388);
            for (int offset : kSummed) total += game::Field<uint64_t>(profile, offset);
            game::Field<uint64_t>(profile, 0x430) = total;
            if (uint8_t* a = field(0x3D490)) vcall(a, 0x48);
            if (void* a = game::Field<void*>(field(0x314A8), 0x96A10)) vcall(a, 0x40);
            call(0x140ac5790, window());
            if (uint8_t* world = field(0x3D3E0)) call(0x140797f30, world + 0x20);
            call(0x1404d5420, display(), profile + 0x438);
            lap(0x278);
            call(0x140ac5690, window(), true, 0);
            lap(0x280);

            // Wait for the loader thread when it is synchronized to the frame.
            auto loaderSync = [&] { return game::Field<uint8_t*>(field(0x314A8), 0xF78); };
            if (loaderSync()[0x58]) {
              vcall(*reinterpret_cast<void**>(0x142ae89a8), 0x28);
              int spins = 0;
              while (!loaderSync()[0x59]) SpinBackoff(spins);
            }
            lap(0x288);
            call(0x140471700, game);
            last = timer();
            call(0x140790360, field(0x3D3E0));
            lap(0x2A0);
            game::Call<void (*)()>(0x141458500)();  // TexLod::Begin
            lap(0x2A8);
            call(0x140ac54a0, window(), true);
            lap(0x2B0);
            call(0x140ac53c0, window());
            lap(0x2B8);
            call(0x1404d4230, display(), true);
            game::Call<void (*)()>(0x141458620)();  // TexLod::End
            lap(0x2C0);
            lap(0x2C8);
            call(0x14049b980, game);
            lap(0x230);
            call(0x140ac41e0, window(), true);
            lap(0x2D0);
            vcall(*reinterpret_cast<void**>(0x142ae89a8), 0x20, delta, 5, true);
            lap(0x2E0);
            game::Field<uint64_t>(profile, 0x2D8) = 0;
            game::Field<uint64_t>(profile, 0x288) = 0;
            if (uint8_t* a = field(0x38B58)) call(0x140679700, a, time);
            lap(0x2E8);
            lap(0x2F0);
            if (void* a = *reinterpret_cast<void**>(0x142b19c70)) call(0x140a88510, a, time);
            lap(0x1B0);
            if (void* a = *reinterpret_cast<void**>(0x142b19c78)) call(0x140643b10, a);
            lap(0x1B8);
            if (void* a = *reinterpret_cast<void**>(0x142b19cd8)) call(0x1404cedc0, a);
            lap(0x1C0);
            vcall(game, 0x1F0);
            lap(0x2F8);
            call(0x140cf3330, field(0x38AC8));
            game::Field<int>(game, 0x38AD8) = 1;
            if (void* a = *reinterpret_cast<void**>(0x143bd4c88)) call(0x140955d70, a);
            if (void* a = *reinterpret_cast<void**>(0x143bd4830)) call(0x140950af0, a);
            lap(0x300);
            if (uint8_t* recorder = field(0x3D490)) {
              auto* device = game::Field<uint8_t*>(game::Field<uint8_t*>(display(), 0x40), 8);
              bool flag = (*reinterpret_cast<bool (***)(void*)>(device))[0x2E0 / 8](device);
              vcall(field(0x3D490), 0x18, flag);
            }
            lap(0x318);
            uint8_t* stats = window() + 0x1A8;
            game::Field<uint64_t>(profile, 0x308) = 0;
            game::Field<uint64_t>(profile, 0x310) = 0;
            game::Field<uint64_t>(profile, 0x318) = 0;
            game::Field<uint64_t>(profile, 0x320) = 0;
            game::Field<uint64_t>(profile, 0x3B0) = game::Field<uint64_t>(stats, 0x70);
            game::Field<uint64_t>(profile, 0x3B8) = game::Field<uint64_t>(stats, 0x78);
            std::memcpy(profile + 0x3C0, stats, 0x70);
            game::Call<void (*)(void*, void*, float)>(0x14186a260)(field(0x3D3D0), game::Field<void*>(display(), 0x48), seconds);
            lap(0x328);
            call(0x1407c7730, field(0x38AF0), time);
            lap(0x1C8);
            call(0x140ac5690, window(), true, 0);
            lap(0x280);
            call(0x140ac3f90, window());
            lap(0x330);

            // Queued debug primitives (+0x3F5B0 list, next +0x60).
            auto* renderer = game::Field<uint8_t*>(game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b19780), 0x38890), 0x50);
            for (auto* node = field(0x3F5B0); node; node = game::Field<uint8_t*>(node, 0x60)) {
              if (!renderer) continue;
              int color = game::Field<int>(node, 0x50) != -1 ? game::Field<int>(node, 0x50) : 0;
              alignas(16) uint8_t primitive[0x80];
              using BuildFn = void* (*)(void*, void*, int, void*, uint8_t*, bool, uint8_t*, uint8_t*, float, int, float);
              void* built = game::Call<BuildFn>(0x14138ae90)(primitive, *reinterpret_cast<void**>(0x142b19ce0), color, game::Field<void*>(node, 0x38), node,
                                                            true, node + 0x20, node + 0x10, game::Field<float>(node, 0x48), game::Field<int>(node, 0x4C),
                                                            one);
              call(0x141289020, renderer, built);
              call(0x1402f2140, static_cast<void*>(primitive + 0x78));
            }
          }
          call(0x14032f360, renderLock);  // unlock
        }
        lap(0x1D0);
      }
      call(0x140ac58e0, window());
      lap(0x340);
    }
    if (void* tracker = *reinterpret_cast<void**>(0x142b19c90)) {
      if (game[0x42E50]) call(0x1411e39d0, 0);
      game::Call<void (*)(void*, float)>(0x141393fe0)(tracker, static_cast<float>(delta) * *reinterpret_cast<float*>(0x142047918));
    }
  }
}

// 0x1403e57f0 (slot 77): Shutdown - destroy every client subsystem: the
// global singletons (each also cleared from the client state's slot table at
// state+0x96BB8..), the client-owned systems at +0x3D3E0..+0x3D510, the
// world, renderer helpers, camera and options, in the original order.
void GameClientShutdownSystems(uint8_t* game) {
  auto Global = [](uint64_t address) -> void*& { return *reinterpret_cast<void**>(address); };
  auto Member = [&](int offset) -> void*& { return game::Field<void*>(game, offset); };
  auto StateSlot = [&](int offset) -> void*& { return game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), offset); };
  auto DeleteVirtualSlot = [](void* object, int slot) {
    if (object) (*reinterpret_cast<void (***)(void*, int)>(object))[slot](object, 1);
  };
  auto FreeSized = [](void* object, size_t size) {
    if (object) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size);  // sized operator delete
  };
  auto DestroyAndFree = [&](void* object, uint64_t destructor, size_t size) {
    if (!object) return;
    game::Call<void (*)(void*)>(destructor)(object);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size);
  };
  auto noArgs = [](uint64_t address) { game::Call<void (*)()>(address)(); };
  // Detaches the +0x3D498 view and its child (+0x188) before they go away.
  auto detachView = [&](uint8_t* view) {
    game::Call<void (*)(void*, int)>(0x140454490)(view, 0);
    if (void* child = game::Field<void*>(view, 0x188)) game::Call<void (*)(void*, int)>(0x140454490)(child, 0);
  };
  // Flush the world with streaming paused (+0x3B648) around the waits.
  auto flushWorld = [&] {
    if (auto* world = static_cast<uint8_t*>(Member(0x3D3E0))) world[0x3B648] = 1;
    game::Call<void (*)(uint8_t*)>(0x14043d9b0)(game);
    noArgs(0x1414ff070);
    noArgs(0x1414d5e90);
    if (void* terrain = Member(0x3B6F8)) game::Call<void (*)(void*)>(0x141868540)(terrain);
    game::Call<void (*)(int)>(0x141295ba0)(4000);
    if (auto* world = static_cast<uint8_t*>(Member(0x3D3E0))) world[0x3B648] = 0;
    noArgs(0x141293730);
  };

  DeleteVirtualSlot(Global(0x142b19958), 0);
  Global(0x142b19958) = nullptr;
  DestroyAndFree(Global(0x142b19a98), 0x1403adb40, 0x70);
  Global(0x142b19a98) = nullptr;
  FreeSized(Global(0x142b19a80), 1);
  Global(0x142b19a80) = nullptr;
  DestroyAndFree(Global(0x142b19a88), 0x140ab82f0, 0x148);
  Global(0x142b19a88) = nullptr;
  DeleteVirtualSlot(Global(0x142b197a8), 0);
  Global(0x142b197a8) = nullptr;
  StateSlot(0x96bb8) = nullptr;
  DeleteVirtualSlot(Global(0x142b197b0), 0);
  Global(0x142b197b0) = nullptr;
  StateSlot(0x96bc0) = nullptr;
  DeleteVirtualSlot(Global(0x142b197b8), 0);
  Global(0x142b197b8) = nullptr;
  StateSlot(0x96bc8) = nullptr;
  DeleteVirtualSlot(Global(0x142b197c0), 0);
  Global(0x142b197c0) = nullptr;
  StateSlot(0x96bd0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ab8), 0);
  Global(0x142b19ab8) = nullptr;
  StateSlot(0x96bd8) = nullptr;
  DeleteVirtualSlot(Global(0x142b197e8), 0);
  Global(0x142b197e8) = nullptr;
  StateSlot(0x96be0) = nullptr;
  DestroyAndFree(Global(0x142b197f0), 0x1406606f0, 0x360);
  Global(0x142b197f0) = nullptr;
  StateSlot(0x96be8) = nullptr;
  DeleteVirtualSlot(Global(0x142b197c8), 0);
  Global(0x142b197c8) = nullptr;
  StateSlot(0x96bf0) = nullptr;
  DeleteVirtualSlot(Global(0x142b197d0), 0);
  Global(0x142b197d0) = nullptr;
  StateSlot(0x96bf8) = nullptr;
  DeleteVirtualSlot(Global(0x142b197d8), 0);
  Global(0x142b197d8) = nullptr;
  StateSlot(0x96c00) = nullptr;
  DeleteVirtualSlot(Global(0x142b197e0), 0);
  Global(0x142b197e0) = nullptr;
  StateSlot(0x96c08) = nullptr;
  DeleteVirtualSlot(Global(0x142b197f8), 0);
  Global(0x142b197f8) = nullptr;
  StateSlot(0x96c20) = nullptr;
  DeleteVirtualSlot(Global(0x142b19800), 0);
  Global(0x142b19800) = nullptr;
  StateSlot(0x96c28) = nullptr;
  DeleteVirtualSlot(Global(0x142b19820), 0);
  Global(0x142b19820) = nullptr;
  StateSlot(0x96c30) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ac0), 0);
  Global(0x142b19ac0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19828), 0);
  Global(0x142b19828) = nullptr;
  StateSlot(0x96c38) = nullptr;
  DeleteVirtualSlot(Global(0x142b19808), 0);
  Global(0x142b19808) = nullptr;
  StateSlot(0x96c40) = nullptr;
  DeleteVirtualSlot(Global(0x142b19810), 0);
  Global(0x142b19810) = nullptr;
  StateSlot(0x96c48) = nullptr;
  DeleteVirtualSlot(Global(0x142b19818), 0);
  Global(0x142b19818) = nullptr;
  StateSlot(0x96c50) = nullptr;
  DeleteVirtualSlot(Global(0x142b19830), 0);
  Global(0x142b19830) = nullptr;
  StateSlot(0x96c18) = nullptr;
  DeleteVirtualSlot(Global(0x142b19838), 0);
  Global(0x142b19838) = nullptr;
  StateSlot(0x96c58) = nullptr;
  DeleteVirtualSlot(Global(0x142b19840), 0);
  Global(0x142b19840) = nullptr;
  StateSlot(0x96c60) = nullptr;
  DeleteVirtualSlot(Global(0x142b19848), 0);
  Global(0x142b19848) = nullptr;
  StateSlot(0x96cc0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19850), 0);
  Global(0x142b19850) = nullptr;
  StateSlot(0x96cc8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19858), 0);
  Global(0x142b19858) = nullptr;
  StateSlot(0x96cd8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19860), 0);
  Global(0x142b19860) = nullptr;
  StateSlot(0x96ce0) = nullptr;
  DestroyAndFree(Global(0x142b19868), 0x140ada5e0, 0x10);
  Global(0x142b19868) = nullptr;
  StateSlot(0x96ce8) = nullptr;
  DestroyAndFree(Global(0x142b19870), 0x140a9b3d0, 0x48);
  Global(0x142b19870) = nullptr;
  StateSlot(0x96cf0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19880), 0);
  Global(0x142b19880) = nullptr;
  StateSlot(0x96cd0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19878), 0);
  Global(0x142b19878) = nullptr;
  StateSlot(0x96d20) = nullptr;
  DeleteVirtualSlot(Global(0x142b19888), 2);
  Global(0x142b19888) = nullptr;
  StateSlot(0x96c68) = nullptr;
  DeleteVirtualSlot(Global(0x142b19890), 0);
  Global(0x142b19890) = nullptr;
  StateSlot(0x96c70) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ac8), 0);
  Global(0x142b19ac8) = nullptr;
  StateSlot(0x96c78) = nullptr;
  DeleteVirtualSlot(Global(0x142b19898), 0);
  Global(0x142b19898) = nullptr;
  StateSlot(0x96c80) = nullptr;
  DeleteVirtualSlot(Global(0x142b198a0), 0);
  Global(0x142b198a0) = nullptr;
  StateSlot(0x96c98) = nullptr;
  DeleteVirtualSlot(Global(0x142b198a8), 0);
  Global(0x142b198a8) = nullptr;
  StateSlot(0x96c88) = nullptr;
  DeleteVirtualSlot(Global(0x142b198b0), 0);
  Global(0x142b198b0) = nullptr;
  StateSlot(0x96c90) = nullptr;
  DeleteVirtualSlot(Global(0x142b198b8), 0);
  Global(0x142b198b8) = nullptr;
  StateSlot(0x96ca0) = nullptr;
  DeleteVirtualSlot(Global(0x142b198c0), 0);
  Global(0x142b198c0) = nullptr;
  StateSlot(0x96ca8) = nullptr;
  FreeSized(Global(0x142b198c8), 1);
  Global(0x142b198c8) = nullptr;
  StateSlot(0x96cb0) = nullptr;
  DeleteVirtualSlot(Global(0x142b198d0), 0);
  Global(0x142b198d0) = nullptr;
  StateSlot(0x96cb8) = nullptr;
  DeleteVirtualSlot(Global(0x142b198d8), 0);
  Global(0x142b198d8) = nullptr;
  DestroyAndFree(Global(0x142b198e0), 0x140ad7700, 0x60);
  Global(0x142b198e0) = nullptr;
  DeleteVirtualSlot(Global(0x142b198e8), 0);
  Global(0x142b198e8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ad0), 0);
  Global(0x142b19ad0) = nullptr;
  DestroyAndFree(Global(0x142b19ad8), 0x140490a30, 0xc0);
  Global(0x142b19ad8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19910), 0);
  Global(0x142b19910) = nullptr;
  DeleteVirtualSlot(Global(0x142b198f0), 0);
  Global(0x142b198f0) = nullptr;
  StateSlot(0x96cf8) = nullptr;
  DeleteVirtualSlot(Global(0x142b198f8), 0);
  Global(0x142b198f8) = nullptr;
  StateSlot(0x96d00) = nullptr;
  DeleteVirtualSlot(Global(0x142b19900), 0);
  Global(0x142b19900) = nullptr;
  StateSlot(0x96d08) = nullptr;
  DeleteVirtualSlot(Global(0x142b19908), 0);
  Global(0x142b19908) = nullptr;
  StateSlot(0x96d10) = nullptr;
  DestroyAndFree(Global(0x142b19aa0), 0x140aa7690, 8);
  Global(0x142b19aa0) = nullptr;
  StateSlot(0x96d18) = nullptr;
  DeleteVirtualSlot(Member(0x3D488), 0);
  Member(0x3D488) = nullptr;
  Global(0x142b199d8) = nullptr;
  DeleteVirtualSlot(Global(0x142ae8ab0), 0);
  Global(0x142ae8ab0) = nullptr;
  DeleteVirtualSlot(Member(0x3D4c8), 0);
  Member(0x3D4c8) = nullptr;
  Global(0x142b19940) = nullptr;
  DeleteVirtualSlot(Member(0x3D4d0), 0);
  Member(0x3D4d0) = nullptr;
  DeleteVirtualSlot(Member(0x3D4d8), 0);
  Member(0x3D4d8) = nullptr;
  if (auto* view = static_cast<uint8_t*>(Member(0x3D498))) {
    detachView(view);
    view[0x138] = 0;
    if (auto* child = game::Field<uint8_t*>(view, 0x188)) child[0x138] = 0;
  }
  DeleteVirtualSlot(Member(0x3D490), 0);
  Member(0x3D490) = nullptr;
  Global(0x142b19a08) = nullptr;
  Global(0x142b19a10) = Member(0x3D490);
  Global(0x142b19a18) = Member(0x3D490);
  DeleteVirtualSlot(Member(0x3D4c0), 0);
  Member(0x3D4c0) = nullptr;
  Global(0x142b19a40) = nullptr;
  Global(0x142b19a48) = Member(0x3D4c0);
  DestroyAndFree(Member(0x3D4a0), 0x1414d9e00, 0x60);
  Member(0x3D4a0) = nullptr;
  Global(0x142b19a38) = nullptr;
  FreeSized(Member(0x3D4a8), 8);
  Member(0x3D4a8) = nullptr;
  Global(0x142ae8ab8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a78), 0);
  Global(0x142b19a78) = nullptr;
  DestroyAndFree(Global(0x142b19ae0), 0x1403ade00, 0x58);
  Global(0x142b19ae0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a50), 0);
  Global(0x142b19a50) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a58), 0);
  Global(0x142b19a58) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a60), 0);
  Global(0x142b19a60) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a68), 0);
  Global(0x142b19a68) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ab0), 0);
  Global(0x142b19ab0) = nullptr;
  DeleteVirtualSlot(Member(0x3D4b0), 0);
  Member(0x3D4b0) = nullptr;
  Global(0x142b19988) = nullptr;
  DeleteVirtualSlot(Member(0x3D4e0), 0);
  Member(0x3D4e0) = nullptr;
  Global(0x142b19938) = nullptr;
  DeleteVirtualSlot(Member(0x3D3e8), 0);
  Member(0x3D3e8) = nullptr;
  Global(0x142b19980) = nullptr;
  uint64_t released = 0;
  game::Call<void (*)(uint64_t*)>(0x1412adcc0)(&released);
  DeleteVirtualSlot(Member(0x3D3e0), 0);
  Member(0x3D3e0) = nullptr;
  Global(0x142b19918) = nullptr;
  Global(0x142b19920) = Member(0x3D3e0);
  flushWorld();
  noArgs(0x1414d5e90);
  noArgs(0x141426830);
  FreeSized(Member(0x3D460), 1);
  Member(0x3D460) = nullptr;
  Global(0x142b199f8) = nullptr;
  if (auto* queue = static_cast<uint8_t*>(Member(0x3D448))) {
    game::Field<uint64_t>(queue, 0x58) = 0x142067c10;  // array vtable
    game::Field<int>(queue, 0x68) = 0;
    FreePacketArray(game::Field<void*>(queue, 0x60), 8);
    game::Field<void*>(queue, 0x60) = nullptr;
    game::Call<void (*)(void*)>(0x14032f060)(queue + 0x10);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(queue, 0x70);
  }
  Member(0x3D448) = nullptr;
  Global(0x142b195e8) = nullptr;
  FreeSized(Member(0x3D478), 0x14);
  Member(0x3D478) = nullptr;
  Global(0x142b195f8) = nullptr;
  FreeSized(Member(0x3D458), 1);
  Member(0x3D458) = nullptr;
  Global(0x142b199e8) = nullptr;
  DestroyAndFree(Member(0x3D470), 0x141868fb0, 1);
  Member(0x3D470) = nullptr;
  Global(0x142b199e0) = nullptr;
  DestroyAndFree(Member(0x3D450), 0x141854b70, 4);
  Member(0x3D450) = nullptr;
  Global(0x142b195e0) = nullptr;
  noArgs(0x1403e4240);
  DeleteVirtualSlot(Member(0x3D420), 0);
  Member(0x3D420) = nullptr;
  Global(0x142b199b8) = nullptr;
  DeleteVirtualSlot(Member(0x3D3f0), 0);
  Member(0x3D3f0) = nullptr;
  Global(0x142b199a0) = nullptr;
  DeleteVirtualSlot(Member(0x3D438), 0);
  Member(0x3D438) = nullptr;
  Global(0x142b199a8) = nullptr;
  DeleteVirtualSlot(Member(0x3D418), 0);
  Member(0x3D418) = nullptr;
  Global(0x142b19998) = nullptr;
  DeleteVirtualSlot(Member(0x3D410), 0);
  Member(0x3D410) = nullptr;
  Global(0x142b19990) = nullptr;
  DeleteVirtualSlot(Member(0x38ab8), 0);
  Member(0x38ab8) = nullptr;
  Global(0x142b19948) = nullptr;
  DeleteVirtualSlot(Member(0x38ac0), 0);
  Member(0x38ac0) = nullptr;
  Global(0x142b19ae8) = nullptr;
  DeleteVirtualSlot(Member(0x3D440), 0);
  Member(0x3D440) = nullptr;
  Global(0x142b199d0) = nullptr;
  DeleteVirtualSlot(Member(0x3D4f8), 0);
  Member(0x3D4f8) = nullptr;
  Global(0x142b19978) = nullptr;
  DeleteVirtualSlot(Member(0x3D428), 0);
  Member(0x3D428) = nullptr;
  Global(0x142b19968) = nullptr;
  DeleteVirtualSlot(Member(0x3D430), 0);
  Member(0x3D430) = nullptr;
  Global(0x142b19970) = nullptr;
  DeleteVirtualSlot(Member(0x3D3f8), 0);
  Member(0x3D3f8) = nullptr;
  Global(0x142b195d8) = nullptr;
  DeleteVirtualSlot(Member(0x3D408), 0);
  Member(0x3D408) = nullptr;
  Global(0x142b199b0) = nullptr;
  DeleteVirtualSlot(Member(0x3D400), 0);
  Member(0x3D400) = nullptr;
  Global(0x142b199c0) = nullptr;
  DeleteVirtualSlot(Member(0x3D4b8), 0);
  Member(0x3D4b8) = nullptr;
  Global(0x142b199c8) = nullptr;
  DeleteVirtualSlot(Member(0x3D510), 0);
  Member(0x3D510) = nullptr;
  game::Call<void (*)(void*)>(0x140aa9460)(game::Call<void* (*)()>(0x140aa8500)());
  noArgs(0x140aa83c0);
  if (void* singleton = Global(0x142b19e30)) {
    Global(0x142b19e30) = nullptr;
    DeleteVirtualSlot(singleton, 0);
  }
  game::Call<void (*)(void*)>(0x141657ec0)(game::Call<void* (*)()>(0x141650f80)());
  DeleteVirtualSlot(Member(0x3D508), 0);
  Member(0x3D508) = nullptr;
  DestroyAndFree(Member(0x3D4E8), 0x1403e2f10, 0x2BF8);
  Member(0x3D4E8) = nullptr;
  Global(0x142b19930) = nullptr;
  noArgs(0x14182e6d0);
  DeleteVirtualSlot(Global(0x142b19a90), 0);
  Global(0x142b19a90) = nullptr;
  if (void* camera = Global(0x142b19a20)) game::Call<void (*)(void*)>(0x1402f8a00)(camera);
  flushWorld();
  game::Call<void (*)(uint8_t*)>(0x1402f3140)(game + 0x42E80);
  Global(0x142b19a20) = nullptr;
  DestroyAndFree(Member(0x3D468), 0x14185aec0, 1);
  Member(0x3D468) = nullptr;
  Global(0x142b195f0) = nullptr;
  DestroyAndFree(Global(0x142b19a28), 0x1414d9330, 0xB0);
  Global(0x142b19a28) = nullptr;
  if (void* physics = Member(0x3D500)) {
    game::Call<void (*)(void*)>(0x14184e920)(physics);
    DestroyAndFree(Member(0x3D500), 0x14184e820, 0xF10);
    Member(0x3D500) = nullptr;
    Global(0x142b19960) = nullptr;
  }
  noArgs(0x1414599c0);
  DeleteVirtualSlot(Global(0x142b199f0), 0);
  Global(0x142b199f0) = nullptr;
  noArgs(0x141293730);
  DestroyAndFree(Member(0x42E48), 0x140aedc00, 0x4AD0);  // the original leaves +0x42E48 set
  if (auto* view = static_cast<uint8_t*>(Member(0x3D498))) {
    detachView(view);
    DestroyAndFree(Member(0x3D498), 0x1403af4c0, 0x190);
    Member(0x3D498) = nullptr;
    Global(0x142b195d0) = nullptr;
  }
}

// 0x1403e42c0 (slot 37): ShutdownGame - save UI state ("GuiOnSave"), then
// destroy the client-owned game systems (+0x387F0..+0x3B948) and their
// globals, flush the world, and finish with the display and window
// (slot 79 via tail call).
void GameClientShutdownGame(uint8_t* game) {
  auto Global = [](uint64_t address) -> void*& { return *reinterpret_cast<void**>(address); };
  auto Member = [&](int offset) -> void*& { return game::Field<void*>(game, offset); };
  auto StateSlot = [&](int offset) -> void*& { return game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), offset); };
  auto DeleteVirtualSlot = [](void* object, int slot) {
    if (object) (*reinterpret_cast<void (***)(void*, int)>(object))[slot](object, 1);
  };
  auto FreeSized = [](void* object, size_t size) {
    if (object) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size);
  };
  auto DestroyAndFree = [&](void* object, uint64_t destructor, size_t size) {
    if (!object) return;
    game::Call<void (*)(void*)>(destructor)(object);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size);
  };
  auto noArgs = [](uint64_t address) { game::Call<void (*)()>(address)(); };
  auto vcall = [](void* object, int offset) { (*reinterpret_cast<void (***)(void*)>(object))[offset / 8](object); };
  vcall(game, 0x120);
  if (void* loader = Member(0x3D3C8)) game::Call<void (*)(void*)>(0x14135c6b0)(loader);
  game::Call<void (*)(uint8_t*)>(0x1403e7870)(game);
  DestroyAndFree(Global(0x142b19b18), 0x141901250, 0xc0);
  Global(0x142b19b18) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b20), 0);
  Global(0x142b19b20) = nullptr;
  Global(0x142b19b28) = nullptr;
  if (void* display = Member(0x38890)) game::Call<void (*)(void*)>(0x1404da8b0)(display);
  *reinterpret_cast<uint8_t*>(0x142b1885b) = 1;
  DeleteVirtualSlot(Member(0x38a40), 0);
  Member(0x38a40) = nullptr;
  vcall(game, 0x110);
  DeleteVirtualSlot(Member(0x388e0), 0);
  Member(0x388e0) = nullptr;
  noArgs(0x1406810d0);
  if (void* ui = Global(0x143c45470)) {
    const char* onSave = reinterpret_cast<const char*>(0x14206c330);  // "GuiOnSave"
    if (game::Call<bool (*)(void*, const char*)>(0x14048b2d0)(ui, onSave))
      game::Call<bool (*)(void*, const char*, void*, void*)>(0x140488cc0)(Global(0x143c45470), onSave, nullptr, nullptr);
  }
  if (Member(0x389A8)) {
    for (int offset : {0x38B40, 0x38B48, 0x38B50})
      if (void* layer = Member(offset)) game::Call<void (*)(void*, void*)>(0x14068b010)(Member(0x389A8), layer);
  }
  DeleteVirtualSlot(Member(0x3b800), 0);
  Member(0x3b800) = nullptr;
  // Shared-pointer release of state+0x969E8 {object, control {strong, weak}}.
  if (auto* shared = static_cast<uint8_t*>(std::exchange(StateSlot(0x969e8), nullptr))) {
    auto* control = game::Field<long*>(shared, 8);
    bool lastStrong = _InterlockedDecrement(control) == 0;
    if (_InterlockedExchangeAdd(control + 1, -1) == 1 && control) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(control, 0x10);
    if (lastStrong) (*reinterpret_cast<void (***)(void*)>(shared))[1](shared);
  }
  DeleteVirtualSlot(StateSlot(0x969e0), 0);
  StateSlot(0x969e0) = nullptr;
  DestroyAndFree(Member(0x38920), 0x140980860, 0x4a8);
  Member(0x38920) = nullptr;
  DeleteVirtualSlot(Member(0x3b7c0), 0);
  Member(0x3b7c0) = nullptr;
  DestroyAndFree(Member(0x38928), 0x140a9c070, 0x50);
  Member(0x38928) = nullptr;
  DeleteVirtualSlot(Member(0x38b40), 0);
  Member(0x38b40) = nullptr;
  DeleteVirtualSlot(Member(0x38b48), 0);
  Member(0x38b48) = nullptr;
  DeleteVirtualSlot(Member(0x38b50), 0);
  Member(0x38b50) = nullptr;
  DestroyAndFree(Member(0x38b68), 0x1409375d0, 0x40);
  Member(0x38b68) = nullptr;
  DeleteVirtualSlot(Member(0x389a8), 0);
  Member(0x389a8) = nullptr;
  DeleteVirtualSlot(Member(0x38958), 0);
  Member(0x38958) = nullptr;
  DeleteVirtualSlot(Member(0x38938), 0);
  Member(0x38938) = nullptr;
  DeleteVirtualSlot(Member(0x38940), 1);
  Member(0x38940) = nullptr;
  if (auto* timers = static_cast<uint8_t*>(Member(0x31498))) {  // +0x31498 is left set, as in the original
    auto* name = reinterpret_cast<soeutil::IString*>(timers + 0x58);
    name->vtable = soeutil::IStringVtable();
    soeutil::StringRelease(name);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(timers, 0xA8);
  }
  DestroyAndFree(Global(0x142b19b30), 0x14076d9c0, 0x170);
  Global(0x142b19b30) = nullptr;
  DestroyAndFree(Member(0x38a10), 0x140995130, 0xd90);
  Member(0x38a10) = nullptr;
  DestroyAndFree(Member(0x38a18), 0x140997600, 0x910);
  Member(0x38a18) = nullptr;
  DeleteVirtualSlot(Member(0x38a70), 2);
  Member(0x38a70) = nullptr;
  DeleteVirtualSlot(Member(0x38a20), 0);
  Member(0x38a20) = nullptr;
  DeleteVirtualSlot(Global(0x142b197a0), 0);
  Global(0x142b197a0) = nullptr;
  if (auto* list = static_cast<uint64_t*>(Member(0x38A00))) {
    *list = 0x142068b98;
    game::Call<void (*)(void*)>(0x140453920)(list);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(list, 0x20);
  }
  Member(0x38a00) = nullptr;
  DeleteVirtualSlot(Member(0x389f0), 5);
  Member(0x389f0) = nullptr;
  DeleteVirtualSlot(Member(0x388a8), 8);
  Member(0x388a8) = nullptr;
  DestroyAndFree(Member(0x38de0), 0x140917e20, 0x30);
  Member(0x38de0) = nullptr;
  DeleteVirtualSlot(Member(0x38828), 1);
  DeleteVirtualSlot(Member(0x38828), 0);
  Member(0x38828) = nullptr;
  DestroyAndFree(Member(0x38860), 0x140719190, 0x1bc8);
  Member(0x38860) = nullptr;
  Global(0x142b19b38) = nullptr;
  DeleteVirtualSlot(Member(0x38868), 0);
  Member(0x38868) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b40), 0);
  Global(0x142b19b40) = nullptr;
  DeleteVirtualSlot(Member(0x389c0), 0);
  Member(0x389c0) = nullptr;
  game::Call<void (*)(uint8_t*)>(0x1403d4c60)(game + 0x390F8);
  DeleteVirtualSlot(Member(0x389c8), 0);
  Member(0x389c8) = nullptr;
  Global(0x142b19b48) = nullptr;
  Global(0x142b19b50) = nullptr;
  DestroyAndFree(Member(0x389b0), 0x141875700, 0x504b0);
  Member(0x389b0) = nullptr;
  DestroyAndFree(Member(0x389b8), 0x141875540, 0x504b0);
  Member(0x389b8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b58), 0);
  Global(0x142b19b58) = nullptr;
  DeleteVirtualSlot(Member(0x38968), 0);
  Member(0x38968) = nullptr;
  DeleteVirtualSlot(Member(0x38960), 0);
  Member(0x38960) = nullptr;
  DeleteVirtualSlot(Member(0x38910), 0);
  Member(0x38910) = nullptr;
  DestroyAndFree(Member(0x38918), 0x14094bd00, 0x170);
  Member(0x38918) = nullptr;
  DestroyAndFree(Member(0x38930), 0x140983f00, 0x78);
  Member(0x38930) = nullptr;
  DeleteVirtualSlot(Member(0x389d0), 1);
  Member(0x389d0) = nullptr;
  DeleteVirtualSlot(Member(0x389d8), 0);
  Member(0x389d8) = nullptr;
  DeleteVirtualSlot(Member(0x38948), 0);
  Member(0x38948) = nullptr;
  DestroyAndFree(Member(0x38950), 0x1406452e0, 1);
  Member(0x38950) = nullptr;
  DeleteVirtualSlot(Member(0x389e0), 0);
  Member(0x389e0) = nullptr;
  DestroyAndFree(Member(0x389e8), 0x141700c80, 0x300);
  Member(0x389e8) = nullptr;
  DeleteVirtualSlot(Member(0x38a60), 0);
  Member(0x38a60) = nullptr;
  Global(0x142b19b60) = nullptr;
  Global(0x142b19b68) = Member(0x38A60);
  DeleteVirtualSlot(Global(0x142b19b70), 0);
  Global(0x142b19b70) = nullptr;
  game::Call<void (*)(uint8_t*)>(0x14049d100)(game);
  if (auto* ui = static_cast<uint8_t*>(Global(0x143c45470))) {
    game::Field<void*>(ui, 0x10) = nullptr;
    game::Field<void*>(ui, 8) = nullptr;
  }
  DestroyAndFree(Member(0x38dd8), 0x140487cd0, 8);
  Member(0x38dd8) = nullptr;
  if (auto* camera = game::Call<uint8_t* (*)(uint8_t*)>(0x1402f39f0)(game + 0x42E80)) {
    if (void* view = game::Field<void*>(camera, 0x88)) vcall(view, 0x130);
  }
  if (void* terrain = Member(0x3B6F8)) game::Call<void (*)(void*)>(0x141868440)(terrain);
  DeleteVirtualSlot(Member(0x3b6f8), 0);
  Member(0x3b6f8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b78), 0);
  Global(0x142b19b78) = nullptr;
  Global(0x142b19b80) = nullptr;
  DeleteVirtualSlot(Member(0x38850), 0);
  Member(0x38850) = nullptr;
  if (auto* object = static_cast<uint8_t*>(Member(0x3B6F0))) DeleteVirtualSlot(object + 8, 0);  // via its secondary base
  Member(0x3b6f0) = nullptr;
  if (auto* manager = static_cast<uint8_t*>(Member(0x3B6B0))) {
    game::Call<void (*)(void*)>(0x1403e3590)(manager);
    game::Call<void (*)(void*)>(0x1403e3820)(manager + 0x340);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(manager, 0x358);
  }
  Member(0x3b6b0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b88), 0);
  Global(0x142b19b88) = nullptr;
  Global(0x142b19b90) = nullptr;
  Member(0x38dd0) = nullptr;
  DeleteVirtualSlot(Member(0x388b8), 1);
  Member(0x388b8) = nullptr;
  DeleteVirtualSlot(Member(0x388d0), 1);
  Member(0x388d0) = nullptr;
  DeleteVirtualSlot(Member(0x388c0), 1);
  Member(0x388c0) = nullptr;
  DeleteVirtualSlot(Member(0x38b80), 0);
  Member(0x38b80) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b98), 0);
  Global(0x142b19b98) = nullptr;
  DeleteVirtualSlot(StateSlot(0xf80), 2);
  StateSlot(0xf80) = nullptr;
  Global(0x142b19ba0) = nullptr;
  if (auto* stream = static_cast<uint8_t*>(Member(0x38C40))) {
    game::Call<void (*)(void*)>(0x14030bf90)(stream + 8);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(stream, 0x2038);
  }
  Member(0x38c40) = nullptr;
  DeleteVirtualSlot(StateSlot(0x96c10), 1);
  StateSlot(0x96c10) = nullptr;
  DeleteVirtualSlot(Member(0x388d8), 0);
  Member(0x388d8) = nullptr;
  DeleteVirtualSlot(Member(0x388e8), 0);
  Member(0x388e8) = nullptr;
  DestroyAndFree(Member(0x388f0), 0x141701dc0, 0x578);
  Member(0x388f0) = nullptr;
  DeleteVirtualSlot(Member(0x388f8), 0);
  Member(0x388f8) = nullptr;
  FreeSized(Member(0x38900), 1);
  Member(0x38900) = nullptr;
  DeleteVirtualSlot(Member(0x38970), 2);
  Member(0x38970) = nullptr;
  DeleteVirtualSlot(Member(0x38978), 2);
  Member(0x38978) = nullptr;
  DeleteVirtualSlot(Member(0x38908), 0);
  Member(0x38908) = nullptr;
  Global(0x142b19ba8) = nullptr;
  DeleteVirtualSlot(Member(0x38b78), 0);
  Member(0x38b78) = nullptr;
  DestroyAndFree(Member(0x389f8), 0x141712030, 0x220);
  Member(0x389f8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b10), 0);
  Global(0x142b19b10) = nullptr;
  DestroyAndFree(Global(0x142b19bb0), 0x14077aa20, 0x270);
  Global(0x142b19bb0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bb8), 0);
  Global(0x142b19bb8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bc0), 0);
  Global(0x142b19bc0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bc8), 0);
  Global(0x142b19bc8) = nullptr;
  DeleteVirtualSlot(Member(0x38a08), 0);
  Member(0x38a08) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bd0), 0);
  Global(0x142b19bd0) = nullptr;
  DestroyAndFree(Member(0x38a28), 0x141713ba0, 0x78);
  Member(0x38a28) = nullptr;
  DestroyAndFree(Member(0x38a30), 0x140999590, 0x1b8);
  Member(0x38a30) = nullptr;
  DeleteVirtualSlot(Member(0x38a98), 0);
  Member(0x38a98) = nullptr;
  DeleteVirtualSlot(Member(0x38a68), 0);
  Member(0x38a68) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bd8), 0);
  Global(0x142b19bd8) = nullptr;
  DeleteVirtualSlot(Member(0x38a80), 0);
  Member(0x38a80) = nullptr;
  DeleteVirtualSlot(Member(0x38a88), 0);
  Member(0x38a88) = nullptr;
  if (auto* object = static_cast<uint64_t*>(Member(0x38A90))) {
    *object = 0x142068b38;
    game::Call<void (*)(void*)>(0x14044d220)(object);
    game::Call<void (*)(void*)>(0x1403a7eb0)(reinterpret_cast<uint8_t*>(object) + 0x228);
    *object = 0x142068b08;
    game::Call<void (*)(void*)>(0x14044d220)(object);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, 0x850);
  }
  Member(0x38a90) = nullptr;
  Global(0x142b19be0) = nullptr;
  Global(0x142b19be8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19bf0), 0);
  Global(0x142b19bf0) = nullptr;
  DestroyAndFree(Global(0x142b19bf8), 0x1408d7400, 0x130);
  Global(0x142b19bf8) = nullptr;
  Member(0x38aa8) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c00), 0);
  Global(0x142b19c00) = nullptr;
  DestroyAndFree(Member(0x38aa0), 0x140a3ded0, 0xc8);
  Member(0x38aa0) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c08), 0);
  Global(0x142b19c08) = nullptr;
  Global(0x142b19c10) = nullptr;
  Global(0x142b19c18) = nullptr;
  DeleteVirtualSlot(Global(0x142b19b00), 2);
  Global(0x142b19b00) = nullptr;
  DeleteVirtualSlot(Member(0x38ab0), 0);
  Member(0x38ab0) = nullptr;
  DeleteVirtualSlot(Member(0x38ac8), 0);
  Member(0x38ac8) = nullptr;
  Global(0x142b19c20) = nullptr;
  DestroyAndFree(Member(0x38ad0), 0x140cf9630, 0x228);
  Member(0x38ad0) = nullptr;
  Global(0x142b19c28) = nullptr;
  DeleteVirtualSlot(Member(0x38a50), 0);
  Member(0x38a50) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c30), 0);
  Global(0x142b19c30) = nullptr;
  DeleteVirtualSlot(Member(0x38a58), 0);
  Member(0x38a58) = nullptr;
  DestroyAndFree(Member(0x3b718), 0x1418b9d30, 8);
  Member(0x3b718) = nullptr;
  DestroyAndFree(Member(0x3b710), 0x1418b9ba0, 0x20);
  Member(0x3b710) = nullptr;
  DeleteVirtualSlot(Member(0x3b720), 0);
  Member(0x3b720) = nullptr;
  DestroyAndFree(Member(0x3b728), 0x1416ca4c0, 0x58);
  Member(0x3b728) = nullptr;
  Member(0x387f0) = nullptr;
  Member(0x387f8) = nullptr;
  noArgs(0x140aa47c0);
  Global(0x142b19798) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c38), 0);
  Global(0x142b19c38) = nullptr;
  Member(0x3b6b8) = nullptr;
  DestroyAndFree(Member(0x3b780), 0x14098ee50, 0xcd8);
  Member(0x3b780) = nullptr;
  DestroyAndFree(Global(0x142b19c40), 0x1407e9fb0, 0x90);
  Global(0x142b19c40) = nullptr;
  DestroyAndFree(Member(0x38e30), 0x140331760, 0xe98);
  Member(0x38e30) = nullptr;
  DeleteVirtualSlot(Member(0x38a48), 0);
  Member(0x38a48) = nullptr;
  DeleteVirtualSlot(Member(0x3b708), 0);
  Member(0x3b708) = nullptr;
  DestroyAndFree(Global(0x142b19c48), 0x1407dbbf0, 0x250);
  Global(0x142b19c48) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c50), 0);
  Global(0x142b19c50) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c58), 0);
  Global(0x142b19c58) = nullptr;
  DeleteVirtualSlot(Global(0x142b19c60), 0);
  Global(0x142b19c60) = nullptr;
  DestroyAndFree(Global(0x142b19c68), 0x140ae7b60, 0x2d8);
  Global(0x142b19c68) = nullptr;
  DestroyAndFree(Member(0x3b938), 0x1407e8510, 0x360);
  Member(0x3b938) = nullptr;
  noArgs(0x1403e41c0);
  DestroyAndFree(Global(0x142b19c78), 0x140643210, 0x78);
  Global(0x142b19c78) = nullptr;
  DeleteVirtualSlot(StateSlot(0x96a10), 0);
  StateSlot(0x96a10) = nullptr;
  Global(0x142b19c80) = nullptr;
  Global(0x142b19c88) = nullptr;
  DeleteVirtualSlot(Member(0x38ae8), 1);
  Member(0x38ae8) = nullptr;
  DestroyAndFree(Global(0x142b19c90), 0x141392930, 0x70);
  Global(0x142b19c90) = nullptr;
  DeleteVirtualSlot(Member(0x38b58), 0);
  Member(0x38b58) = nullptr;
  DeleteVirtualSlot(Member(0x38b60), 0);
  Member(0x38b60) = nullptr;
  DeleteVirtualSlot(Member(0x3b948), 0);
  Member(0x3b948) = nullptr;
  DestroyAndFree(Member(0x3b940), 0x1406f8840, 0x68);
  Member(0x3b940) = nullptr;
  DestroyAndFree(Member(0x38af0), 0x1407c5440, 0x503c8);
  Member(0x38af0) = nullptr;
  if (void* display = Member(0x38890)) game::Call<void (*)(void*)>(0x1404da8b0)(display);
  DestroyAndFree(Member(0x38be0), 0x1415f9be0, 0x60);
  Member(0x38be0) = nullptr;
  if (auto* world = static_cast<uint8_t*>(Member(0x3D3E0))) world[0x3B648] = 1;
  game::Call<void (*)(uint8_t*)>(0x14043d9b0)(game);
  noArgs(0x1414ff070);
  noArgs(0x1414d5e90);
  if (void* terrain = Member(0x3B6F8)) game::Call<void (*)(void*)>(0x141868540)(terrain);
  game::Call<void (*)(int)>(0x141295ba0)(4000);
  if (auto* world = static_cast<uint8_t*>(Member(0x3D3E0))) world[0x3B648] = 0;
  vcall(game, 0x268);
  DeleteVirtualSlot(Global(0x143bd4830), 0);
  Global(0x143bd4830) = nullptr;
  if (void* profiler = std::exchange(Global(0x143c73100), nullptr)) {
    game::Call<void (*)(void*)>(0x14141df30)(profiler);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(profiler, 0xC8);
  }
  noArgs(0x140956b30);
  noArgs(0x140cc0520);
  noArgs(0x140cc7bc0);
  noArgs(0x140cc8f90);
  Global(0x142b19c98) = nullptr;
  DeleteVirtualSlot(Global(0x142b19ca0), 0);
  Global(0x142b19ca0) = nullptr;
  DeleteVirtualSlot(Member(0x3d3d0), 0);
  Member(0x3d3d0) = nullptr;
  DeleteVirtualSlot(Member(0x38898), 0);
  Member(0x38898) = nullptr;
  Global(0x142b19c18) = nullptr;
  if (auto* display = static_cast<uint8_t*>(Member(0x38890))) DeleteVirtualSlot(display + 8, 0);  // via its secondary base
  Member(0x38890) = nullptr;
  Global(0x142b19788) = nullptr;
  DeleteVirtualSlot(Global(0x142b19a30), 0);
  Global(0x142b19a30) = nullptr;
  Global(0x142b19ca8) = nullptr;
  DeleteVirtualSlot(Member(0x388e0), 0);
  Member(0x388e0) = nullptr;
  vcall(game, 0x278);  // tail call
}

// 0x1403fa350 (slot 9): GiveTime - the client's connection/login state
// machine (state at +0x314B0, 36 states; shutdown reason at 0x142b176c4),
// followed by the per-frame profile/time bookkeeping shared by every state.
void GameClientGiveTime(uint8_t* game) {
  using TimerFn = uint64_t* (*)(uint64_t*);
  auto timer = [] {
    uint64_t value;
    return *game::Call<TimerFn>(0x14032fde0)(&value);
  };
  auto now = [] {
    uint64_t value;
    return *game::Call<TimerFn>(0x14032fd30)(&value);
  };
  auto setState = [&](int state) { game::Call<void (*)(uint8_t*, int)>(0x140474de0)(game, state); };
  auto& shutdownReason = *reinterpret_cast<int*>(0x142b176c4);
  using LogFn = void (*)(const char*, const char*, ...);
  auto error = [](uint64_t text) { game::Call<LogFn>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(text)); };
  auto vcall = [](void* object, int offset) { (*reinterpret_cast<void (***)(void*)>(object))[offset / 8](object); };
  auto recorder = [] { return *reinterpret_cast<uint8_t**>(0x142b19b98); };
  auto gatewayConnected = [&] { return game::Call<bool (*)(void*)>(0x14063bdb0)(game::Field<void*>(recorder(), 8)); };

  auto login = [&] { return game::Field<void*>(game, 0x38B80); };
  auto fireConsoleEvent = [](uint64_t name) {
    if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
      const char* text = reinterpret_cast<const char*>(name);
      soeutil::IString event{soeutil::IStringVtable(), const_cast<char*>(text), static_cast<int>(std::strlen(text)), -1};
      game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(console, &event, nullptr, nullptr);
      event.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&event);
    }
  };
  auto fireAssignedEvent = [](uint64_t name) {
    if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
      soeutil::IString event{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      game::Call<void (*)(soeutil::IString*, const char*, int)>(0x1402ee880)(&event, reinterpret_cast<const char*>(name), -1);
      game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(*reinterpret_cast<void**>(0x143bd4830), &event, nullptr, nullptr);
      event.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&event);
    }
  };
  // Debug setting 0x36713FC6 in the settings list at *(0x142b197a0)+0x3E90
  // (NaN counts as zero, like the original ucomisd/jne).
  auto debugFlagSet = [] {
    uint8_t* node = game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b197a0), 0x3E90);
    for (; node && game::Field<unsigned>(node, 0x18) != 0x36713FC6u; node = game::Field<uint8_t*>(node, 0x20)) {
    }
    if (!node) return false;
    double value = game::Field<double>(node, 0);
    return value < 0.0 || value > 0.0;
  };

  game::Call<void (*)()>(0x141426830)();
  game::Call<void (*)(uint8_t*)>(0x14047b7a0)(game);
  if (void* a = game::Field<void*>(game, 0x42E48)) game::Call<void (*)(void*)>(0x140af0020)(a);
  if (void* a = *reinterpret_cast<void**>(0x143bc1408)) game::Call<void (*)(void*)>(0x1407411b0)(a);
  uint8_t* profile = game::Field<uint8_t*>(game, 0x314A8) + 0xA10;
  uint64_t previousEnd = game::Field<uint64_t>(game, 0x31538);
  game::Field<uint64_t>(profile, 0x30) = previousEnd == *reinterpret_cast<uint64_t*>(0x142b186b8) ? 0 : timer() - previousEnd;
  uint64_t frameStart = timer();
  game::Field<uint64_t>(game, 0x31530) = frameStart;
  uint64_t lapStart = frameStart;

  switch (game::Field<int>(game, 0x314B0)) {
    case 0:
      game[0x38839] = 1;
      game::Call<void (*)(uint8_t*)>(0x140472810)(game);
      break;
    case 4: {  // splash screen
      bool waitForSplash = true;
      if (!*reinterpret_cast<void**>(0x142b19b08)) {
        using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
        game::Call<GetBoolFn>(0x1403051c0)(*reinterpret_cast<uint8_t**>(0x142b199f0) + 0x1D38, reinterpret_cast<const char*>(0x14206d358),
                                           reinterpret_cast<const char*>(0x14206d348), true, false, -1, -1);  // [UI] SplashScreen (result unused)
        waitForSplash = *reinterpret_cast<void**>(0x142b19b08) != nullptr;
      }
      if (waitForSplash) {
        int64_t shown = static_cast<int64_t>(now() - game::Field<uint64_t>(game, 0x314D0));
        if (static_cast<int>(shown > 0x7fffffff ? 0x7fffffff : shown) < 2500) break;
      }
      if ((*reinterpret_cast<bool (***)(uint8_t*)>(game))[0xE8 / 8](game)) {
        setState(12);
      } else {
        shutdownReason = 4;
        setState(0x23);
      }
      break;
    }
    case 12: {  // log in
      game::Field<uint64_t>(*reinterpret_cast<uint8_t**>(0x142b19780), 0x314F8) = now();
      game::Call<void (*)(uint8_t*, const char*)>(0x140470070)(game, reinterpret_cast<const char*>(0x14206d35c));  // "Login"
      game::Field<uint64_t>(game, 0x38B70) = now();
      if (void* launcher = *reinterpret_cast<void**>(0x142b19b10); launcher && game::Call<bool (*)(void*)>(0x14065c010)(launcher)) {
        game::Call<void (*)(void*)>(0x14065b540)(launcher);
        if (game::Call<bool (*)(void*)>(0x14065c130)(launcher)) {
          if (!game::Call<bool (*)(void*)>(0x14065e8c0)(launcher)) {
            game::Call<void (*)(void*)>(0x14065de00)(launcher);
            break;
          }
          if (game::Call<bool (*)(void*)>(0x14065f1b0)(launcher)) break;
        }
        game::Call<void (*)(uint8_t*)>(0x140477d40)(game);
      }
      game::Call<void (*)(void*, uint8_t*, int, int)>(0x140738550)(login(), game + 0x31608, game::Field<int>(game, 0x31DF4), game::Field<int>(game, 0x31DF8));
      game::Call<void (*)(void*, uint8_t*)>(0x1407385d0)(game::Field<void*>(game, 0x38B80), game + 0x31A28);
      game::Call<void (*)(void*, const char*, int)>(0x140736260)(game::Field<void*>(game, 0x38B80), reinterpret_cast<const char*>(0x14206d368), 3);
      if (void* splash = *reinterpret_cast<void**>(0x142b19b08)) {
        game::Call<void (*)(void*)>(0x141342530)(splash);
        *reinterpret_cast<void**>(0x142b19b08) = nullptr;
      }
      setState(game::Call<bool (*)(void*, int)>(0x140738660)(game::Field<void*>(game, 0x38B80), 0) ? 13 : 17);
      break;
    }
    case 29: {  // waiting for a relogin session
      uint64_t deadline = game::Field<uint64_t>(game, 0x38B88);
      if (deadline != *reinterpret_cast<uint64_t*>(0x142b17e20) && static_cast<int64_t>(deadline) < static_cast<int64_t>(now())) {
        error(0x14206d388);  // "Timed out while waiting for a relogin session."
        shutdownReason = 0x16;
        setState(0x23);
      } else if (!gatewayConnected()) {
        error(0x14206d3c0);  // "Lost connection to the gateway while waiting for ..."
        shutdownReason = 0x14;
        setState(0x23);
      } else {
        game::Call<void (*)(void*, int)>(0x14063d8e0)(recorder(), 1000);
      }
      break;
    }
    case 30: {  // back to character select
      fireConsoleEvent(0x14206d408);  // "EVENT_LOGIN_BEGIN"
      (*reinterpret_cast<void (***)(uint8_t*, const char*)>(game))[0x140 / 8](game, reinterpret_cast<const char*>(0x14206d420));  // "Exiting to character select"
      game::Call<void (*)(void*, size_t)>(0x140d0fb84)(game::Field<void*>(game, 0x390E0), 0x10);
      game::Field<void*>(game, 0x390E0) = nullptr;
      if (game::Call<bool (*)(void*, bool)>(0x140738660)(login(), true)) {
        setState(13);
      } else {
        shutdownReason = 0x15;
        setState(0x23);
      }
      break;
    }
    case 13: {  // waiting for the login server
      game::Call<void (*)(void*)>(0x1407369d0)(login());
      auto* session = game::Field<uint8_t*>(login(), 0x10);
      if (session && session[0x2BC]) {
        setState(14);
        break;
      }
      if (game[0x38839] && game::Call<bool (*)(void*)>(0x140736d50)(login())) break;
      fireConsoleEvent(0x14206d440);  // "EVENT_TITLE_SCREEN_LOGIN_FAILED"
      shutdownReason = 0x13;
      setState(0x23);
      break;
    }
    case 14: {  // character select shown
      game::Call<void (*)(void*)>(0x1407369d0)(login());
      if (!game[0x38EB2]) {
        auto* window = game::Call<void* (*)(void*, const char*)>(0x140cf41f0)(game::Field<void*>(game, 0x38AC8),
                                                                              reinterpret_cast<const char*>(0x14206d460));  // "Main.wndCharacterSelect"
        auto page = [&] {
          return game::Call<void* (*)(void*)>(0x140cfa3c0)(game::Call<void* (*)(void*, bool)>(0x140cf3e60)(window, true));
        };
        if (window && game::Call<void* (*)(void*, bool)>(0x140cf3e60)(window, true) && page()) {
          void* first = page();
          if ((*reinterpret_cast<void* (***)(void*)>(first))[0x150 / 8](first)) {
            void* second = page();
            if (!(*reinterpret_cast<bool (***)(void*)>(second))[0x168 / 8](second)) setState(16);
          }
        }
      } else {
        auto* state = static_cast<uint8_t*>(login());
        if (state[0x15A] || state[0x15B]) setState(16);
      }
      if (!game[0x38839]) {
        shutdownReason = 9;
        setState(0x23);
      }
      break;
    }
    case 15:
      game::Call<void (*)(void*)>(0x1407369d0)(login());
      break;
    case 16: {  // character chosen
      game::Call<void (*)(void*)>(0x1407369d0)(login());
      if (game::Call<bool (*)(void*)>(0x140736d30)(login())) {
        setState(17);
      } else if (!game::Call<bool (*)(void*)>(0x140736d50)(login())) {
        error(0x14206d478);  // "Failure during login."
        setState(14);
        ScriptArgList args{reinterpret_cast<void**>(0x14206d328), nullptr, 0, 0};
        game::Call<void (*)(uint8_t*, int)>(0x14046d940)(ScriptArgAt(&args, 0), 0);
        ScriptArgSetInt(&args, 1, 3);
        game::Call<bool (*)(void*, const char*, ScriptArgList*, void*)>(0x140488cc0)(
            UiRoot(), reinterpret_cast<const char*>(0x14206d490), &args, nullptr);  // "CharacterSelectHandler:OnCharacterLogin"
        game::Call<void (*)(ScriptArgList*)>(0x1403a0770)(&args);
      } else if (game[0x3B770] && game::Field<uint8_t*>(login(), 0x10)[0x2BC]) {
        game::Call<void (*)(uint8_t*, const char*)>(0x14040daa0)(game, reinterpret_cast<const char*>(0x14206d4b8));  // "Has character list"
      }
      if (!game[0x38839]) {
        shutdownReason = 0x12;
        setState(0x23);
      }
      break;
    }
    case 17: {  // connect to the gateway with the chosen character
      using ConnectFn = void (*)(uint8_t*, uint64_t, uint64_t*, uint64_t, uint64_t, uint64_t, const char*, int, const char*, bool);
      auto connect = (*reinterpret_cast<ConnectFn**>(game))[0xE0 / 8];
      const char* empty = reinterpret_cast<const char*>(0x142046fcb);
      if (game::Call<bool (*)(void*)>(0x140736d30)(login())) {
        auto* session = game::Field<uint8_t*>(login(), 0x10);
        soeutil::StringFixed<256> ticket;
        soeutil::InitFixed(ticket, reinterpret_cast<void**>(0x142049e08));
        const void* encoded = game::Field<int>(session, 0xA8) ? game::Field<void*>(session, 0xA0) : nullptr;
        game::Call<void (*)(const void*, int, soeutil::IString*)>(0x14166ae60)(encoded, game::Field<int>(session, 0xA8), &ticket);
        uint64_t characterId = game::Field<uint64_t>(session, 0x100);
        connect(game, game::Field<uint64_t>(session, 0x108), &characterId, game::Field<uint64_t>(session, 0x158), game::Field<uint64_t>(session, 0x20),
                game::Field<uint64_t>(session, 0x60), ticket.data, game::Field<int>(session, 0xF8), empty, false);
        ticket.vtable = reinterpret_cast<void**>(0x142049de8);
        soeutil::StringRelease(&ticket);
      } else {
        uint64_t characterId = game::Field<uint64_t>(game, 0x38BF0);
        connect(game, game::Field<uint64_t>(game, 0x31DE8), &characterId, game::Field<uint64_t>(game, 0x38C08), game::Field<uint64_t>(game, 0x31610),
                game::Field<uint64_t>(game, 0x316B0), game::Field<const char*>(game, 0x31750), game::Field<int>(game, 0x38BE8), empty, true);
      }
      if (game[0x3883A]) {
        setState(0x13);
        break;
      }
      if (!(*reinterpret_cast<bool (***)(uint8_t*)>(game))[0x130 / 8](game)) {
        error(0x14206d4d0);  // "Failure to set up initial connection."
        fireConsoleEvent(0x14206d440);
        shutdownReason = 0x11;
        setState(0x23);
        break;
      }
      if (debugFlagSet() || !(*reinterpret_cast<bool (***)(uint8_t*)>(game))[0x90 / 8](game)) game::Call<void (*)(uint8_t*)>(0x1403d5f60)(game);
      game::Call<void (*)(uint8_t*, int)>(0x1403d7290)(game, 0);
      if (gatewayConnected()) {
        game::Call<void (*)(void*)>(0x14063be10)(game::Field<void*>(recorder(), 8));
        game::Field<uint64_t>(game, 0x314B8) = now();
        setState(0x12);
      } else {
        error(0x14206d4f8);  // "Connection to gateway lost before authenticating"
        fireConsoleEvent(0x14206d440);
        shutdownReason = 0xE;
        setState(0x23);
      }
      break;
    }
    case 18: {  // waiting for our character
      if (game[0x38838]) {
        error(0x14206d530);  // "While connecting to the server the client was ..."
        shutdownReason = 0x10;
        setState(0x23);
        break;
      }
      uint8_t* player = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80);
      if (player) {
        game::Call<bool (*)(void*, const char*, void*, void*)>(0x140488cc0)(
            UiRoot(), reinterpret_cast<const char*>(0x14206d580), nullptr, nullptr);  // "CharacterSelectHandler:OnCharacterLoginComplete"
        fireConsoleEvent(0x14206d5b0);  // "EVENT_LOGIN_COMPLETE"
        auto* name = game::Call<uint8_t* (*)(uint8_t*)>(0x1416cb000)(game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80) + 0x28);
        game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206d5c8), game::Field<const char*>(game, 0x31610),
                                       game::Field<const char*>(name, 8));  // "Connected to the server at: %s and received our character: %s."
        game::Call<void (*)(uint8_t*)>(0x140467bb0)(game);
        void* strings = game::Call<void* (*)(void*, bool)>(0x140481b20)(game::Field<void*>(game, 0x3B6B8), true);
        alignas(16) uint8_t localePacket[0x1B0];
        void* packet = game::Call<void* (*)(void*, void*)>(0x14039d970)(localePacket, strings);
        game::Call<void (*)(void*, void*, int, bool)>(0x14035f8f0)(game::Field<void*>(recorder(), 8), packet, 1, true);
        game::Call<void (*)(void*)>(0x1403b0df0)(localePacket);
        SmallPacket clockOffset{reinterpret_cast<void**>(0x142063db8), 0x72, 0, static_cast<uint32_t>(game::Field<int>(game, 0x38824)), 0};
        game::Call<void (*)(void*, SmallPacket*, int, bool)>(0x14035ee40)(game::Field<void*>(recorder(), 8), &clockOffset, 0, true);
        setState(0x13);
        break;
      }
      uint64_t since = game::Field<uint64_t>(game, 0x314B8);
      if (game::Call<int (*)(uint64_t*)>(0x1403f71e0)(&since) > 120000) {
        error(0x14206d608);  // "Timed out while waiting to receive character data."
        fireConsoleEvent(0x14206d440);
        shutdownReason = 0xF;
        setState(0x23);
      } else if (gatewayConnected()) {
        game::Call<void (*)(void*, int)>(0x14063d8e0)(recorder(), 1000);
      } else {
        error(0x14206d640);  // "Lost connection to the gateway while waiting to receive character data."
        fireAssignedEvent(0x14206d440);
        shutdownReason = 0x14;
        setState(0x23);
      }
      break;
    }
    case 19:  // post-initialize
      if ((*reinterpret_cast<bool (***)(uint8_t*)>(game))[0xF8 / 8](game)) {
        vcall(game, 0x108);
        setState(0x14);
      } else {
        error(0x14206d688);  // "Failure in PostInitialize."
        shutdownReason = 0xD;
        setState(0x23);
      }
      break;
    case 20:  // waiting for initial deployment
      if (game::Call<void* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860))) {
        setState(0x15);
        break;
      }
      game::Call<void (*)(uint8_t*)>(0x140477650)(game);
      game::Call<void (*)(void*, int)>(0x14063d8e0)(recorder(), 1000);
      if (!gatewayConnected()) {
        error(0x14206d6a8);  // "Disconnect from gateway waiting for initial deployment."
        shutdownReason = 0xC;
        setState(0x23);
      }
      break;
    case 21:  // enter the world
      game::Call<void (*)(void*)>(0x140cf6190)(game::Field<void*>(game, 0x38AC8));
      game::Call<void (*)(void*)>(0x140cfb4d0)(game::Field<void*>(game, 0x38AD0));
      vcall(game, 0x1E0);
      game::Call<void (*)(uint8_t*, int)>(0x14046be80)(game, 0x14);
      game::Call<bool (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(0x14206d6e0), nullptr,
                                                                          nullptr);  // "MiniMap:StartMiniMap"
      (*reinterpret_cast<unsigned (__stdcall**)(unsigned)>(0x1440a0540))(5);  // timeBeginPeriod (game import)
      game::Field<uint64_t>(game, 0x38800) = now();
      if (game[0x3883A]) {
        game::Call<void (*)(uint8_t*)>(0x140474500)(game);
        game::Call<void (*)(uint8_t*, const char*)>(0x14040daa0)(game, reinterpret_cast<const char*>(0x14206d6f8));  // "Solo: Starting Run"
        setState(0x18);
      } else {
        setState(0x16);
      }
      break;
    case 22: {
      int result = game::Call<int (*)(uint8_t*)>(0x140478560)(game);
      if (result == 1) {
        setState(0x17);
      } else if (result == 2) {
        error(0x14206d710);  // "WaitForWorldReady failed."
        shutdownReason = 0xA;
        setState(0x23);
      }
      break;
    }
    case 23: {
      int result = game::Call<int (*)(uint8_t*)>(0x140478080)(game);
      if (result == 1) {
        game::Call<void (*)(uint8_t*, int)>(0x1403edab0)(game, 0);
        if (game[0x3B7DC]) {
          setState(0x18);
        } else {
          if (game[0x38EB2]) fireAssignedEvent(0x14206d730);  // "EVENT_LOADING_ZONE_LOAD_COMPLETE"
          setState(0x1A);
        }
      } else if (result == 2) {
        error(0x14206d710);
        shutdownReason = 0xB;
        setState(0x23);
      }
      break;
    }
    case 24: {  // solo run start
      game::Call<void (*)(uint8_t*)>(0x140433440)(game);
      float minutes = *reinterpret_cast<float*>(0x1425ba090);
      for (auto* node = game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b197a0), 0xF98); node; node = game::Field<uint8_t*>(node, 0x20)) {
        if (game::Field<unsigned>(node, 0x18) == 0xC0DC29E7u) {
          minutes = static_cast<float>(game::Field<double>(node, 0));
          break;
        }
      }
      game::Field<int>(game, 0x314C4) = static_cast<int>(minutes * *reinterpret_cast<float*>(0x1420728c8));
      game::Field<uint64_t>(game, 0x314F8) = now();
      if (game[0x38EB2]) fireAssignedEvent(0x14206d730);
      setState(0x1A);
      break;
    }
    case 26: {  // solo run in progress
      game::Call<void (*)(uint8_t*)>(0x14049b6a0)(game + 0x3B9B0);
      if (!game[0x38839]) {
        shutdownReason = 9;
        setState(0x23);
        break;
      }
      uint64_t lastSent = game::Field<uint64_t>(game, 0x3B778);
      if (game::Call<int (*)(uint64_t*)>(0x1403f71e0)(&lastSent) >= game::Field<int>(game, 0x314C4)) {
        game::Field<uint64_t>(game, 0x3B778) = game::Field<uint64_t>(game, 0x3B9C8);
        uint64_t scratch;
        int* score = game::Call<int* (*)(uint8_t*, uint64_t*)>(0x1403f88b0)(game, &scratch);
        SmallPacket report{reinterpret_cast<void**>(0x142063d18), 0x3C, 0, static_cast<uint32_t>(*score), 0};
        game::Call<void (*)(void*, SmallPacket*, int, bool)>(0x14035f650)(game::Field<void*>(recorder(), 8), &report, 0, true);
      }
      uint64_t endTime = game::Field<uint64_t>(game, 0x38F80);
      if (endTime != *reinterpret_cast<uint64_t*>(0x142b18168)) {
        uint64_t clockValue;
        int remaining = static_cast<int>(endTime) - static_cast<int>(*game::Call<TimerFn>(0x14032fe90)(&clockValue));
        if (remaining >= 0) {
          // Countdown "mm:ss" pushed to the HUD string at 0x142b17bf8 when it changes.
          soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          soeutil::StringFormat(&text, reinterpret_cast<const char*>(0x14206d758), remaining / 60, remaining % 60);  // "%02d:%02d"
          soeutil::IString copy{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          soeutil::StringAssign(&copy, text.data);
          auto* shown = reinterpret_cast<soeutil::IString*>(0x142b17bf8);
          if (shown->length != copy.length || std::memcmp(shown->data, copy.data, static_cast<size_t>(shown->length)) != 0) {
            soeutil::StringAssignString(shown, &copy);
            game::Call<void (*)(void*)>(0x140cff570)(reinterpret_cast<void*>(0x142b17b90));
          }
          copy.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&copy);
          text.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&text);
        }
      }
      game::Call<void (*)(uint8_t*)>(0x14049b560)(game + 0x3B9B0);
      game::Call<void (*)(uint8_t*)>(0x140477e60)(game);
      game::Call<void (*)(uint8_t*)>(0x1403d4180)(game);
      break;
    }
    case 27: {
      int result = game::Call<int (*)(uint8_t*)>(0x140478310)(game);
      if (result == 1) {
        setState(0x1A);
      } else if (result == 2) {
        error(0x14206d710);
        shutdownReason = 8;
        setState(0x23);
      }
      break;
    }
    case 28: {
      int result = game::Call<int (*)(uint8_t*)>(0x140478560)(game);
      if (result == 1 || result == 2) game::Call<void (*)(uint8_t*, bool)>(0x1403ee100)(game, result == 1);
      break;
    }
    case 35: {  // shut down
      const char* reason = game::Call<const char* (*)(int)>(0x140499ef0)(shutdownReason);
      game::Call<LogFn>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206d768), reason);  // "Beginning to shutdown the game client reason %s."
      if (void* loader = *reinterpret_cast<void**>(0x142ae89a8)) {
        (*reinterpret_cast<void (***)(void*, bool)>(loader))[0x98 / 8](loader, false);
        vcall(loader, 0xA8);
      }
      (*reinterpret_cast<void (***)(uint8_t*, const char*)>(game))[0x140 / 8](game, reinterpret_cast<const char*>(0x14206d7a0));  // "Game client shutdown"
      (*reinterpret_cast<unsigned (__stdcall**)(unsigned)>(0x1440a0548))(5);  // timeEndPeriod (game import)
      game[0x38839] = 0;
      game[0x312D1] = 0;
      game::Call<void (*)(uint8_t*)>(0x140470ae0)(game);  // does not return
      __debugbreak();
      break;
    }
    default:
      break;
  }

  // ---- shared per-frame tail ----
  uint64_t afterState = timer();
  game::Field<uint64_t>(profile, 0x18) = afterState - lapStart;
  if (*reinterpret_cast<uint8_t**>(0x142b197a0)) {
    bool check = debugFlagSet() || !(*reinterpret_cast<bool (***)(uint8_t*)>(game))[0x90 / 8](game);
    if (check && *reinterpret_cast<uint8_t*>(0x142b176cd) && *reinterpret_cast<uint8_t*>(0x142b176ce) &&
        !(*reinterpret_cast<bool (**)()>(0x142b176e8))()) {
      game::Call<LogFn>(0x1402baba0)(reinterpret_cast<const char*>(0x142054700), reinterpret_cast<const char*>(0x14206d7c0));  // "H1Z1.log", "... pfnRun() failed ..."
      (*reinterpret_cast<void (**)()>(0x142b176e0))();
      (*reinterpret_cast<BOOL (__stdcall**)(HMODULE)>(0x14409fef8))(*reinterpret_cast<HMODULE*>(0x142b176d8));  // FreeLibrary (game import)
      game::Call<int (*)(const char*)>(0x140d42358)(reinterpret_cast<const char*>(0x14206d820));
      game::Call<int (*)(const char*, const char*)>(0x140d55674)(reinterpret_cast<const char*>(0x14206d840), reinterpret_cast<const char*>(0x14206d820));
      vcall(game, 0xF0);
    }
  }
  int frameMs = game::Field<int>(game, 0x31540);
  if (frameMs > 0 && game::Field<int>(game, 0x314B0) >= 12)
    game::Call<void (*)(uint8_t*, float)>(0x1403fa010)(game, static_cast<float>(frameMs) * *reinterpret_cast<float*>(0x142047918));
  uint64_t afterTimers = timer();
  game::Field<uint64_t>(profile, 0x20) = afterTimers - afterState;
  int state = game::Field<int>(game, 0x314B0);
  if (state <= 0x1A && ((0x4016000u >> state) & 1)) {  // states 13, 14, 16, 26
    game::Call<void (*)(uint8_t*)>(0x1403fba60)(game);
  } else if (state > 4 && state != 0x23) {
    if (!game[0x38839]) {
      shutdownReason = 0x17;
      setState(0x23);
    } else {
      if (void* handler = game::Field<void*>(game, 0x388C8)) {
        game::Call<void (*)(void*)>(0x1409d7200)(handler);
        while (game::Call<int (*)()>(0x14133c270)() != 0) {
        }
      }
      game::Call<void (*)(uint8_t*, bool, bool, bool, int)>(0x140474860)(game, true, false, true, 10);
    }
  }
  game::Field<uint64_t>(profile, 0x10) = timer() - afterTimers;
  game::Field<uint64_t>(profile, 8) = timer() - game::Field<uint64_t>(game, 0x31530);
  uint64_t since = game::Field<uint64_t>(game, 0x31538) != *reinterpret_cast<uint64_t*>(0x142b186b8) ? game::Field<uint64_t>(game, 0x31538)
                                                                                                    : game::Field<uint64_t>(game, 0x31530);
  int64_t total = static_cast<int64_t>(timer() - since);
  game::Field<uint64_t>(profile, 0) = static_cast<uint64_t>(total);
  game::Field<int>(game, 0x31540) = static_cast<int>(total / 1000000);
  game::Field<uint64_t>(game, 0x31538) = timer();
}

// SOE heap allocation used by the service factories: MemoryAllocate when a
// thread allocator is active, else operator new[](size, nothrow).
void* SoeHeapAllocate(size_t size) {
  if (soeutil::ThreadAllocatorCount() == 0)
    return game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(size, reinterpret_cast<void*>(0x143c46658));
  return game::Call<void* (*)(size_t, int)>(0x14032f910)(size, 0);
}

// Base of the inline-constructed services: {vtable, named-object vtable +8
// (0x1424bc0a0), name data +0x10, +0x18, +0x20, +0x28, +0x30}.
void InitNamedServiceBase(uint8_t* object) {
  game::Field<void*>(object, 0x10) = soeutil::EmptyStringData();
  game::Field<uint64_t>(object, 0x18) = 0;
  game::Field<uint64_t>(object, 8) = 0x1424bc0a0;
  game::Field<uint64_t>(object, 0x28) = 0;
  game::Field<uint64_t>(object, 0x30) = 0;
  game::Field<uint64_t>(object, 0x20) = 0;
}
// Inline hash table {vtable, +8 count, +0xC 0x7FFFFFFF, +0x10, +0x18, +0x20, buckets +0x28}.
void InitInlineHashTable(uint8_t* table, uint64_t vtable, size_t bucketBytes) {
  game::Field<uint64_t>(table, 0) = vtable;
  game::Field<int>(table, 0x20) = 0;
  game::Field<uint64_t>(table, 0x10) = 0;
  game::Field<uint64_t>(table, 0x18) = 0;
  std::memset(table + 0x28, 0, bucketBytes);
  game::Field<int>(table, 8) = 0;
  game::Field<int>(table, 0xC) = 0x7FFFFFFF;
}

// 0x1403d8a00 (slot 76): CreateAppServices - construct the client's ~60 app
// services, store each in the state's service table (state+0x96BB8..) and
// its global, and initialize them with the app context at +0x38830.
bool GameClientCreateAppServices(uint8_t* game) {
  auto StateSlot = [&](int offset) -> void*& { return game::Field<void*>(game::Field<uint8_t*>(game, 0x314A8), offset); };
  auto Global = [](uint64_t address) -> void*& { return *reinterpret_cast<void**>(address); };
  auto context = [&] { return game::Field<void*>(game, 0x38830); };
  auto initService = [&](void* service, int slot = 0x30 / 8) {
    (*reinterpret_cast<void (***)(void*, void*, void*)>(service))[slot](service, context(), nullptr);
  };
  enum Alloc { Heap, Game };
  // allocate + construct (+ vtable/post-construct), store in a state slot,
  // optionally InitService and publish to a global.
  auto CreateService = [&](Alloc alloc, size_t size, uint64_t constructor, uint64_t vtable, uint64_t postConstruct, int slot, bool init,
                           uint64_t global) {
    void* memory = alloc == Heap ? SoeHeapAllocate(size) : GameAllocate(size);
    void* service = nullptr;
    if (memory) {
      service = game::Call<void* (*)(void*)>(constructor)(memory);
      if (vtable) {
        service = memory;
        *static_cast<uint64_t*>(memory) = vtable;
        if (postConstruct) game::Call<void (*)(void*)>(postConstruct)(memory);
      }
    }
    StateSlot(slot) = service;
    if (init) initService(StateSlot(slot));
    if (global) Global(global) = StateSlot(slot);
  };

  CreateService(Heap, 0x2F40, 0x140397360, 0x142069da8, 0x140a5ae10, 0x96BB8, true, 0x142b197a8);
  CreateService(Heap, 0x3A40, 0x140397530, 0, 0, 0x96BC0, true, 0x142b197b0);
  CreateService(Heap, 0x408, 0x141731c60, 0, 0, 0x96BC8, true, 0x142b197b8);
  {  // inline-constructed service with an embedded hash table at +0x38
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0xE8));
    if (object) {
      uint8_t* table = object + 0x38;
      Global(0x142b19648) = table;
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206a180;
      InitInlineHashTable(table, 0x14206a150, 0x80);
      object[0xE0] = 0;
    }
    StateSlot(0x96BD0) = object;
    Global(0x142b197c0) = StateSlot(0x96BD0);
  }
  CreateService(Heap, 0x480, 0x14039bb00, 0, 0, 0x96BF0, false, 0);
  (*reinterpret_cast<void (***)(void*, void*, void*)>(StateSlot(0x96BF0)))[0x30 / 8](StateSlot(0x96BF0), nullptr, nullptr);  // no context
  Global(0x142b197c8) = StateSlot(0x96BF0);
  CreateService(Heap, 0x6330, 0x14039b0c0, 0, 0, 0x96BF8, true, 0x142b197d0);
  CreateService(Heap, 0x1B0, 0x14039bdb0, 0x142069350, 0, 0x96C00, true, 0x142b197d8);
  CreateService(Heap, 0x21A0, 0x14039bf00, 0x14206a3d8, 0, 0x96C08, true, 0x142b197e0);
  CreateService(Heap, 0x1720, 0x1417355f0, 0x14206a1e8, 0x140a63f70, 0x96BE0, true, 0x142b197e8);
  CreateService(Game, 0x360, 0x140660320, 0, 0, 0x96BE8, false, 0x142b197f0);
  CreateService(Heap, 0x6128, 0x14039c6e0, 0x142069478, 0, 0x96C20, true, 0x142b197f8);
  CreateService(Heap, 0x2890, 0x14039c8b0, 0, 0, 0x96C28, true, 0x142b19800);
  CreateService(Heap, 0x63F0, 0x14039c990, 0x142069870, 0, 0x96C30, false, 0);
  CreateService(Heap, 0x90, 0x141739c50, 0, 0, 0x96C40, true, 0x142b19808);
  CreateService(Heap, 0x8B8, 0x14171e6f0, 0, 0, 0x96C48, true, 0x142b19810);
  CreateService(Game, 0x48, 0x140a22d60, 0, 0, 0x96C50, false, 0);
  initService(StateSlot(0x96C50), 1);  // this service initializes through slot 1
  Global(0x142b19818) = StateSlot(0x96C50);
  Global(0x142b19820) = StateSlot(0x96C30);
  CreateService(Heap, 0x45A0, 0x14039caf0, 0x142069a70, 0x140a3c7f0, 0x96C38, true, 0x142b19828);
  CreateService(Heap, 0x5F0, 0x14039c590, 0, 0, 0x96C18, true, 0x142b19830);
  CreateService(Heap, 0x2C18, 0x14039cf40, 0x142069608, 0, 0x96C58, true, 0x142b19838);
  CreateService(Heap, 0x4C8, 0x14039d0f0, 0x142064c60, 0x1404a3340, 0x96C60, true, 0x142b19840);
  {
    void* memory = SoeHeapAllocate(0x420);
    StateSlot(0x96CC0) = memory ? game::Call<void* (*)(void*, int)>(0x141753b50)(memory, 0) : nullptr;
    initService(StateSlot(0x96CC0));
    Global(0x142b19848) = StateSlot(0x96CC0);
  }
  CreateService(Heap, 0xE8, 0x140af58a0, 0, 0, 0x96CC8, true, 0x142b19850);
  CreateService(Heap, 0x10E8, 0x1417569c0, 0, 0, 0x96CD8, true, 0x142b19858);
  CreateService(Heap, 0x98, 0x1417593d0, 0, 0, 0x96CE0, true, 0x142b19860);
  CreateService(Game, 0x10, 0x140ada5b0, 0, 0, 0x96CE8, false, 0x142b19868);
  CreateService(Game, 0x48, 0x140a9b390, 0, 0, 0x96CF0, false, 0x142b19870);
  {  // inline service with a 0x2000-byte hash table at +0x38
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x2060));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206bbb0;
      InitInlineHashTable(object + 0x38, 0x14206bb80, 0x2000);
    }
    StateSlot(0x96D20) = object;
    initService(StateSlot(0x96D20));
    Global(0x142b19878) = StateSlot(0x96D20);
  }
  CreateService(Heap, 0x48D0, 0x14174c9f0, 0, 0, 0x96CD0, true, 0x142b19880);
  CreateService(Heap, 0x4390, 0x14039d360, 0, 0, 0x96C68, false, 0);
  initService(static_cast<uint8_t*>(StateSlot(0x96C68)) + 8);  // through its secondary base
  Global(0x142b19888) = StateSlot(0x96C68);
  CreateService(Heap, 0xC90, 0x14039e500, 0x14206a770, 0x140a7c730, 0x96C70, true, 0x142b19890);
  {  // inline service with four small lists
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x1C0));
    if (object) {
      uint8_t* first = object + 0x38;
      Global(0x142b19670) = first;
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206a818;
      game::Field<uint64_t>(first, 8) = 0;
      game::Field<uint64_t>(first, 0x10) = 0;
      game::Field<uint64_t>(first, 0) = 0x14206a7f8;
      for (int list : {0x98, 0xF8, 0x158}) {
        game::Field<uint64_t>(object, list + 8) = 0;
        game::Field<uint64_t>(object, list + 0x10) = 0;
        game::Field<uint64_t>(object, list) = 0x14206a7f8;
      }
      object[0x1B8] = 0;
    }
    StateSlot(0x96C80) = object;
    initService(StateSlot(0x96C80));
    Global(0x142b19898) = StateSlot(0x96C80);
  }
  {
    auto* object = static_cast<uint64_t*>(SoeHeapAllocate(0x1B20));
    if (object) {
      game::Call<void (*)(void*)>(0x14039dcb0)(object);
      *object = 0x14206aad0;
      game::Call<void (*)(void*)>(0x140a85740)(object);
      game::Call<void (*)(void*)>(0x140a85750)(object);
    }
    StateSlot(0x96C88) = object;
    initService(StateSlot(0x96C88));
    Global(0x142b198a0) = StateSlot(0x96C88);
  }
  CreateService(Heap, 0x2E0, 0x1417414f0, 0x14206abd0, 0, 0x96C98, true, 0x142b198a8);
  CreateService(Game, 0x9B0, 0x140667e80, 0, 0, 0x96C90, false, 0x142b198b0);
  CreateService(Game, 0x9A0, 0x14066ede0, 0, 0, 0x96CA0, false, 0x142b198b8);
  CreateService(Heap, 0x20E0, 0x14039f630, 0x14206ad00, 0, 0x96CA8, true, 0x142b198c0);
  StateSlot(0x96CB0) = GameAllocate(1);  // empty tag object
  Global(0x142b198c8) = StateSlot(0x96CB0);
  {  // inline service with a hash table at +0x38 (0x80 buckets)
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0xE0));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206a0f0;
      InitInlineHashTable(object + 0x38, 0x14206a0c0, 0x80);
    }
    StateSlot(0x96CB8) = object;
    initService(StateSlot(0x96CB8));
    Global(0x142b198d0) = StateSlot(0x96CB8);
  }
  {
    void* memory = SoeHeapAllocate(0x148);
    void* service = memory ? game::Call<void* (*)(void*)>(0x141754e20)(memory) : nullptr;
    initService(service);  // not null-checked in the original
    Global(0x142b198d8) = service;
  }
  {
    void* memory = GameAllocate(0x60);
    Global(0x142b198e0) = memory ? game::Call<void* (*)(void*)>(0x140ad7480)(memory) : nullptr;
  }
  {
    void* memory = SoeHeapAllocate(0x7A8);
    void* service = memory ? game::Call<void* (*)(void*)>(0x14186f4c0)(memory) : nullptr;
    using InitFn = bool (*)(void*, void*, void*);
    if (!(*reinterpret_cast<InitFn**>(service))[0x30 / 8](service, context(), nullptr) &&
        !(*reinterpret_cast<bool (***)(uint8_t*)>(game))[0xA0 / 8](game)) {
      using ShutdownFn = void (*)(uint8_t*, bool, int, const char*, const char*);
      (*reinterpret_cast<ShutdownFn**>(game))[0xD8 / 8](game, true, 8, nullptr, nullptr);
      return false;
    }
    Global(0x142b198e8) = service;
  }
  {
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x2060));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206bc80;
      InitInlineHashTable(object + 0x38, 0x14206bc50, 0x2000);
    }
    StateSlot(0x96CF8) = object;
    initService(StateSlot(0x96CF8));
    Global(0x142b198f0) = StateSlot(0x96CF8);
  }
  {
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x2060));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206bd50;
      InitInlineHashTable(object + 0x38, 0x14206bd20, 0x2000);
    }
    StateSlot(0x96D00) = object;
    initService(StateSlot(0x96D00));
    Global(0x142b198f8) = StateSlot(0x96D00);
  }
  {
    void* memory = SoeHeapAllocate(0x4088);
    if (memory) std::memset(memory, 0, 0x4088);
    StateSlot(0x96D08) = memory ? game::Call<void* (*)(void*)>(0x14039c3b0)(memory) : nullptr;
    initService(StateSlot(0x96D08));
    Global(0x142b19900) = StateSlot(0x96D08);
  }
  CreateService(Heap, 0x1E8, 0x14175b490, 0, 0, 0x96D10, true, 0x142b19908);
  {
    void* memory = SoeHeapAllocate(0x78);
    void* service = memory ? game::Call<void* (*)(void*)>(0x140ae3930)(memory) : nullptr;
    Global(0x142b19910) = service;
    initService(service);  // not null-checked in the original
  }
  // World.
  uint8_t* renderer = game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x38890), 0x70);
  {
    auto* hook = static_cast<uint64_t*>(GameAllocate(8));
    if (hook) *hook = 0x14206b488;
    game::Field<void*>(renderer, 0xA388) = hook;
    void* memory = GameAllocate(0x3B658);
    void* world = memory ? game::Call<void* (*)(void*, void*, void*)>(0x14078c430)(
                               memory, game::Field<void*>(game::Field<uint8_t*>(game, 0x38890), 0x40), renderer)
                         : nullptr;
    game::Field<void*>(game, 0x3D3E0) = world;
    Global(0x142b19918) = world;
    Global(0x142b19920) = game::Field<void*>(game, 0x3D3E0);
  }
  game::Call<void (*)(uint8_t*)>(0x1403d5b10)(game);
  // Animation feature weights from the debug settings (key, list offset, default).
  auto settingFloat = [](int listOffset, unsigned key, uint64_t fallback) {
    for (auto* node = game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b197a0), listOffset); node;
         node = game::Field<uint8_t*>(node, 0x20))
      if (game::Field<unsigned>(node, 0x18) == key) return static_cast<float>(game::Field<double>(node, 0));
    return *reinterpret_cast<float*>(fallback);
  };
  struct Feature {
    int listOffset;
    unsigned key;
    uint64_t fallback;
    uint64_t name;
  };
  static const Feature kFeatures[] = {
      {0x36C8, 0xEF256ECD, 0x142072850, 0x14206bdc0},  // Feature_FootIK
      {0x640, 0x8D3D18BC, 0x1425ba080, 0x14206bdd0},   // Feature_JumpAdditives
      {0x3FB0, 0xBBE177EA, 0x14204862c, 0x14206bde8},  // Feature_PoseAdjustment
      {0x1600, 0xB78952B4, 0x1425ba078, 0x14206be00},  // Feature_SpineAdditives
      {0x39F0, 0x72BF9732, 0x1425ba08c, 0x14206be18},  // Feature_WristIK
  };
  for (const Feature& feature : kFeatures) {
    float weight = settingFloat(feature.listOffset, feature.key, feature.fallback);
    game::Call<void (*)(void*, const char*, float)>(0x141384900)(*reinterpret_cast<void**>(0x142b19928), reinterpret_cast<const char*>(feature.name),
                                                                weight);
  }
  {  // two inline 0x15E0-byte hash tables
    auto* tables = static_cast<uint8_t*>(GameAllocate(0x2BF8));
    if (tables) {
      game::Field<int>(tables, 8) = 0;
      std::memset(tables + 0x10, 0, 0x15E0);
      game::Field<int>(tables, 0) = 0;
      game::Field<int>(tables, 4) = 0x7FFFFFFF;
      game::Field<uint64_t>(tables, 0x15F8) = 0;
      game::Field<uint64_t>(tables, 0x1600) = 0;
      game::Field<int>(tables, 0x1608) = 0;
      std::memset(tables + 0x1610, 0, 0x15E0);
      game::Field<int>(tables, 0x15F0) = 0;
      game::Field<int>(tables, 0x15F4) = 0x7FFFFFFF;
      game::Field<int>(tables, 0x2BF0) = 0;
    }
    game::Field<void*>(game, 0x3D4E8) = tables;
    Global(0x142b19930) = tables;
  }
  {
    void* memory = SoeHeapAllocate(0xBC0);
    void* service = memory ? game::Call<void* (*)(void*)>(0x141432bc0)(memory) : nullptr;
    game::Field<void*>(game, 0x3D4E0) = service;
    Global(0x142b19938) = service;
  }
  {
    void* memory = SoeHeapAllocate(0xE8);
    game::Field<void*>(game, 0x3D4C8) = memory ? game::Call<void* (*)(void*)>(0x14184c6c0)(memory) : nullptr;
    initService(game::Field<void*>(game, 0x3D4C8));
    Global(0x142b19940) = game::Field<void*>(game, 0x3D4C8);
  }
  const char* resources = reinterpret_cast<const char*>(0x14206be28);  // "Resources/"
  {
    void* memory = GameAllocate(0x360);
    void* service = memory ? game::Call<void* (*)(void*)>(0x1407e8370)(memory) : nullptr;
    game::Field<void*>(game, 0x3B938) = service;
    game::Call<void (*)(void*, const char*)>(0x1407e8e70)(service, resources);
  }
  using InitPathFn = void (*)(void*, void*, const char*);
  {
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x160));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206b160;
      InitInlineHashTable(object + 0x38, 0x14206b130, 0x100);
    }
    game::Field<void*>(game, 0x3D4D0) = object;
    (*reinterpret_cast<InitPathFn**>(object))[0x30 / 8](object, nullptr, resources);
  }
  {
    void* memory = SoeHeapAllocate(0x288);
    void* service = memory ? game::Call<void* (*)(void*)>(0x14039e270)(memory) : nullptr;
    game::Field<void*>(game, 0x3D4D8) = service;
    (*reinterpret_cast<InitPathFn**>(service))[0x30 / 8](service, nullptr, resources);
  }
  {
    auto* object = static_cast<uint64_t*>(GameAllocate(0x20));
    if (object) {
      object[0] = 0x1420688b0;
      object[1] = 0x142068890;
      object[2] = 0;
      object[3] = 0;
    }
    game::Field<void*>(game, 0x38AB8) = object;
    Global(0x142b19948) = object;
  }
  {
    auto* object = static_cast<uint64_t*>(GameAllocate(8));
    if (object) *object = 0x14206b6f0;
    game::Field<void*>(game, 0x38AC0) = object;
    Global(0x142b19950) = object;
  }
  {  // inline service with two hash tables (+0x38, +0x68)
    auto* object = static_cast<uint8_t*>(SoeHeapAllocate(0x98));
    if (object) {
      InitNamedServiceBase(object);
      game::Field<uint64_t>(object, 0) = 0x14206b7e8;
      static const uint64_t kTables[][2] = {{0x38, 0x14206b758}, {0x68, 0x14206b7b8}};
      for (const auto& table : kTables) {
        uint8_t* t = object + table[0];
        game::Field<int>(t, 0x20) = 0;
        game::Field<uint64_t>(t, 0x10) = 0;
        game::Field<uint64_t>(t, 0x18) = 0;
        game::Field<int>(t, 8) = 0;
        game::Field<int>(t, 0xC) = 0x7FFFFFFF;
        game::Field<uint64_t>(t, 0x28) = 0;
        game::Field<uint64_t>(t, 0) = table[1];
      }
    }
    Global(0x142b19958) = object;
    initService(object);  // not null-checked in the original
  }
  {
    void* memory = GameAllocate(0xD3C8);
    void* streamer = memory ? game::Call<void* (*)(void*, void*)>(0x14136c120)(memory, game::Field<void*>(game, 0x3D3C0)) : nullptr;
    game::Field<void*>(game, 0x3D508) = streamer;
    auto manager = [] { return game::Call<void* (*)()>(0x141650f80)(); };
    game::Call<void (*)(void*, void*, void*)>(0x1416583e0)(manager(), streamer, nullptr);
    game::Call<void (*)(void*)>(0x14164e6d0)(manager());
    if (game::Call<uint8_t (*)(void*)>(0x140aa86c0)(game::Call<void* (*)()>(0x140aa8500)()) != 1) return false;
    game::Call<void (*)(void*)>(0x14164fd50)(manager());
  }
  {
    void* memory = GameAllocate(0xF10);
    void* audio = memory ? game::Call<void* (*)(void*)>(0x14184e7a0)(memory) : nullptr;
    game::Field<void*>(game, 0x3D500) = audio;
    Global(0x142b19960) = audio;
  }
  soeutil::StringFixed<256> audioPath;
  soeutil::InitFixed(audioPath, reinterpret_cast<void**>(0x142049e08));
  soeutil::StringFixed<256> instancesPath;
  soeutil::InitFixed(instancesPath, reinterpret_cast<void**>(0x142049e08));
  using GetStringFn = void (*)(void*, const char*, const char*, const char*, soeutil::IString*, bool, int, int);
  game::Call<GetStringFn>(0x1403334f0)(game::Field<void*>(game, 0x38E30), reinterpret_cast<const char*>(0x14206be88),
                                       reinterpret_cast<const char*>(0x14206be70), reinterpret_cast<const char*>(0x14206be38), &instancesPath, true,
                                       -1, -1);  // [AudioPaths] GameObjectInstances
  if (int soundError = game::Call<int (*)(void*, const char*)>(0x14184e8b0)(game::Field<void*>(game, 0x3D500), instancesPath.data))
    game::Call<void (*)(const char*, const char*, ...)>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206bea0),
                                                                     soundError);  // "... failed to initialize the sound manager.  Error code: %d."
  auto assetSystem = [&] { return game::Field<void*>(game, 0x3D3C0); };
  auto assetLoader = [&] { return game::Field<void*>(game, 0x3D3C8); };
  {
    void* memory = game::Call<void* (*)(size_t, int)>(0x14032f910)(0x178B20, 0x10);
    auto* display = game::Field<uint8_t*>(game, 0x38890);
    game::Field<void*>(game, 0x3D510) =
        memory ? game::Call<void* (*)(void*, void*, void*, void*)>(0x14130f8c0)(memory, game::Field<void*>(display, 0x40),
                                                                                game::Field<void*>(display, 0x48), assetLoader())
               : nullptr;
  }
  // Asset-backed resource managers: ctor(memory, assetSystem, assetLoader), then
  // the three vtables (primary, +8, +0x48) of the concrete manager.
  auto resourceManager = [&](size_t size, uint64_t constructor, uint64_t vtable0, uint64_t vtable8, uint64_t vtable48, int member, uint64_t global) {
    auto* object = static_cast<uint64_t*>(GameAllocate(size));
    if (object) {
      game::Call<void (*)(void*, void*, void*)>(constructor)(object, assetSystem(), assetLoader());
      object[0] = vtable0;
      object[1] = vtable8;
      object[0x48 / 8] = vtable48;
    }
    game::Field<void*>(game, member) = object;
    Global(global) = object;
  };
  resourceManager(0x8318, 0x140396150, 0x142067640, 0x142067688, 0x1420676b0, 0x3D428, 0x142b19968);
  resourceManager(0x1318, 0x1403962b0, 0x142067868, 0x1420678b0, 0x1420678d8, 0x3D430, 0x142b19970);
  {
    void* memory = GameAllocate(0x8320);
    void* manager =
        memory ? game::Call<void* (*)(void*, void*, void*, bool)>(0x141845390)(memory, assetSystem(), assetLoader(), true) : nullptr;
    game::Field<void*>(game, 0x3D4F8) = manager;
    Global(0x142b19978) = manager;
  }
  using LoadFileFn = void (*)(void*, const char*, void*, void*, int, int);
  {
    void* memory = GameAllocate(0x9A0);
    auto* sockets = memory ? game::Call<uint8_t* (*)(void*, void*)>(0x140398540)(memory, assetSystem()) : nullptr;
    game::Field<void*>(game, 0x3D3E8) = sockets;
    void* files = game::Field<void*>(sockets, 0x990);
    (*reinterpret_cast<LoadFileFn**>(files))[0x18 / 8](files, reinterpret_cast<const char*>(0x14206bf00), sockets, nullptr, 2,
                                                       0);  // "ActorSockets.xml"
    Global(0x142b19980) = game::Field<void*>(game, 0x3D3E8);
  }
  {  // occlusion zones: {vtable, assetSystem +8, hash table +0x10, assetSystem +0xB0, flag +0xB8}
    auto* zones = static_cast<uint8_t*>(GameAllocate(0xC0));
    if (zones) {
      void* system = assetSystem();
      game::Field<void*>(zones, 8) = system;
      game::Field<uint64_t>(zones, 0) = 0x142063838;
      game::Field<uint64_t>(zones, 0x18) = 0;
      game::Field<uint64_t>(zones, 0x20) = 0;
      game::Field<int>(zones, 0x28) = 0;
      std::memset(zones + 0x30, 0, 0x80);
      game::Field<int>(zones, 0x10) = 0;
      game::Field<int>(zones, 0x14) = 0x7FFFFFFF;
      game::Field<void*>(zones, 0xB0) = system;
      zones[0xB8] = 0;
    }
    game::Field<void*>(game, 0x3D4B0) = zones;
    Global(0x142b19988) = zones;
    auto* stored = game::Field<uint8_t*>(game, 0x3D4B0);
    void* files = game::Field<void*>(stored, 0xB0);
    (*reinterpret_cast<LoadFileFn**>(files))[0x18 / 8](files, reinterpret_cast<const char*>(0x14206bf18), stored, nullptr, 2,
                                                       0);  // "OcclusionZones.xml"
  }
  resourceManager(0x1318, 0x140395ff0, 0x142067418, 0x142067460, 0x142067488, 0x3D410, 0x142b19990);
  resourceManager(0x1318, 0x140395bd0, 0x142066f88, 0x142066fd0, 0x142066ff8, 0x3D418, 0x142b19998);
  resourceManager(0x1318, 0x140395650, 0x142067de0, 0x142067e28, 0x142067e50, 0x3D3F0, 0x142b199a0);
  resourceManager(0x1318, 0x140396410, 0x142068230, 0x142068278, 0x1420682a0, 0x3D438, 0x142b199a8);
  resourceManager(0x8318, 0x1403957b0, 0x142066be0, 0x142066c28, 0x142066c50, 0x3D3F8, 0x142b195d8);
  resourceManager(0x1318, 0x140395e90, 0x142068008, 0x142068050, 0x142068078, 0x3D408, 0x142b199b0);
  resourceManager(0x1318, 0x140395d30, 0x1420671b0, 0x1420671f8, 0x142067220, 0x3D420, 0x142b199b8);
  resourceManager(0x1318, 0x140395910, 0x142066d60, 0x142066da8, 0x142066dd0, 0x3D400, 0x142b199c0);
  resourceManager(0x1318, 0x140396570, 0x142067b98, 0x142067be0, 0x142067c08, 0x3D4B8, 0x142b199c8);
  resourceManager(0x8318, 0x140395a70, 0x1420679e8, 0x142067a30, 0x142067a58, 0x3D440, 0x142b199d0);
  {
    void* memory = SoeHeapAllocate(0x230);
    game::Field<void*>(game, 0x3D488) = memory ? game::Call<void* (*)(void*)>(0x14184a150)(memory) : nullptr;
    initService(game::Field<void*>(game, 0x3D488));
    Global(0x142b199d8) = game::Field<void*>(game, 0x3D488);
  }
  // Small client-owned helpers: construct (or zero) and publish.
  auto smallObject = [&](size_t size, uint64_t constructor, int member, uint64_t global) {
    void* memory = GameAllocate(size);
    void* object = nullptr;
    if (memory) {
      if (constructor)
        object = game::Call<void* (*)(void*)>(constructor)(memory);
      else {
        *static_cast<uint8_t*>(memory) = 0;
        object = memory;
      }
    }
    game::Field<void*>(game, member) = object;
    Global(global) = object;
  };
  smallObject(1, 0x141868d80, 0x3D470, 0x142b199e0);
  smallObject(1, 0, 0x3D458, 0x142b199e8);
  smallObject(0x70, 0x1403986a0, 0x3D448, 0x142b195e8);
  {  // {flag, index -1, 0, flag}
    auto* settings = static_cast<uint8_t*>(GameAllocate(0x14));
    if (settings) {
      settings[0] = 0;
      game::Field<int>(settings, 4) = -1;
      game::Field<uint64_t>(settings, 8) = 0;
      settings[0x10] = 0;
    }
    game::Field<void*>(game, 0x3D478) = settings;
    settings[0] = (*reinterpret_cast<uint8_t**>(0x142b199f0))[0x2E98];  // not null-checked in the original
    game::Field<int>(game::Field<uint8_t*>(game, 0x3D478), 4) = game::Field<int>(game::Field<uint8_t*>(game, 0x31418), 0x8018);
    Global(0x142b195f8) = game::Field<void*>(game, 0x3D478);
  }
  smallObject(1, 0, 0x3D460, 0x142b199f8);
  smallObject(1, 0x14185ade0, 0x3D468, 0x142b195f0);
  smallObject(4, 0x141854440, 0x3D450, 0x142b195e0);
  {
    void* memory = GameAllocate(0xA0);
    Global(0x142b19a00) = memory ? game::Call<void* (*)(void*)>(0x140aa6360)(memory) : nullptr;
  }
  game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206bf30));  // "... Initialized Effects systems."
  {
    void* memory = GameAllocate(0x90);
    game::Field<void*>(game, 0x3D490) = memory ? game::Call<void* (*)(void*)>(0x140aa5c50)(memory) : nullptr;
    Global(0x142b19a08) = game::Field<void*>(game, 0x3D490);
    Global(0x142b19a10) = game::Field<void*>(game, 0x3D490);
    Global(0x142b19a18) = game::Field<void*>(game, 0x3D490);
  }
  if (!Global(0x142ae8ab0)) {
    void* memory = GameAllocate(0x640);
    Global(0x142ae8ab0) = memory ? game::Call<void* (*)(void*)>(0x1402fab20)(memory) : nullptr;
  }
  game::Call<void (*)()>(0x1417f5db0)();
  // Camera system with a ref-counted callback {vtable, refcount, function 0x1416c8a60}.
  bool cameraFlag = game::Call<bool (*)(void*)>(0x1404d3090)(game::Field<void*>(game, 0x38890));
  uint64_t callbackHandle = 0;  // pointer | flag bit 0
  if (auto* callback = static_cast<uint8_t*>(GameAllocate(0x18))) {
    game::Field<uint64_t>(callback, 0) = 0x142071f00;
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(callback + 8), 1);
    game::Field<uint64_t>(callback, 0) = 0x1420721d0;
    game::Field<uint64_t>(callback, 0x10) = 0x1416c8a60;
    callbackHandle = (callbackHandle & 1) | reinterpret_cast<uint64_t>(callback);
  }
  int cameraMode = game::Field<int>(*reinterpret_cast<uint8_t**>(0x142b199f0), 0x2C94) != 0 && cameraFlag ? 1 : 0;
  game::Call<void (*)(uint8_t*, void*, int, uint64_t*)>(0x1402f3b60)(game + 0x42E80, game::Field<void*>(game, 0x38890), cameraMode, &callbackHandle);
  auto camera = [&] { return game::Call<void* (*)(uint8_t*)>(0x1402f39f0)(game + 0x42E80); };
  Global(0x142b19a20) = camera();
  {
    void* memory = GameAllocate(0xB0);
    Global(0x142b19a28) = memory ? game::Call<void* (*)(void*)>(0x1414d92a0)(memory) : nullptr;
  }
  {
    void* memory = GameAllocate(0x190);
    void* view = memory ? game::Call<void* (*)(void*, void*)>(0x14039c2a0)(memory, Global(0x142b19a30)) : nullptr;
    game::Field<void*>(game, 0x3D498) = view;
    Global(0x142b195d0) = view;
  }
  {
    void* memory = GameAllocate(0x60);
    void* controller = memory ? game::Call<void* (*)(void*, void*, int)>(0x1414d9c80)(memory, camera(), 3) : nullptr;
    game::Field<void*>(game, 0x3D4A0) = controller;
    Global(0x142b19a38) = controller;
  }
  {
    void* memory = GameAllocate(8);
    void* listener = memory ? game::Call<void* (*)(void*, void*)>(0x1414d91b0)(memory, camera()) : nullptr;
    game::Field<void*>(game, 0x3D4A8) = listener;
    Global(0x142ae8ab8) = listener;
  }
  {
    void* memory = GameAllocate(0x53FD8);
    void* entities =
        memory ? game::Call<void* (*)(void*, int, void*)>(0x140708300)(memory, 0x19, game::Field<void*>(game, 0x38890)) : nullptr;
    game::Field<void*>(game, 0x3D4C0) = entities;
    Global(0x142b19a40) = entities;
    Global(0x142b19a48) = game::Field<void*>(game, 0x3D4C0);
  }
  auto zeroedService = [&](size_t size, uint64_t constructor) {
    void* memory = SoeHeapAllocate(size);
    if (!memory) return static_cast<void*>(nullptr);
    std::memset(memory, 0, size);
    return game::Call<void* (*)(void*)>(constructor)(memory);
  };
  Global(0x142b19a50) = zeroedService(0x180, 0x14039f700);
  initService(Global(0x142b19a50));  // not null-checked in the original
  {
    void* memory = GameAllocate(0x250);
    Global(0x142b19a58) = memory ? game::Call<void* (*)(void*)>(0x140769bd0)(memory) : nullptr;
  }
  {
    void* memory = GameAllocate(0x830);
    void* service = memory ? game::Call<void* (*)(void*)>(0x14076bd70)(memory) : nullptr;
    Global(0x142b19a60) = service;
    (*reinterpret_cast<void (***)(void*, const char*)>(service))[1](service, resources);
  }
  Global(0x142b19a68) = zeroedService(0x70, 0x14039f4f0);
  initService(Global(0x142b19a68));
  game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x14206bf70));  // "... Initialized AppServices."
  {
    void* memory = GameAllocate(0xB0);
    void* service = memory ? game::Call<void* (*)(void*)>(0x14039bd30)(memory) : nullptr;
    game::Field<void*>(game, 0x3D3D0) = service;
    Global(0x142b19a70) = service;
  }
  game::Call<void (*)()>(0x1414589c0)();
  game::Call<void (*)(int)>(0x1414585d0)(0);
  {
    void* memory = SoeHeapAllocate(0x170);
    Global(0x142b19a78) = memory ? game::Call<void* (*)(void*)>(0x14174fb60)(memory) : nullptr;
    initService(Global(0x142b19a78));
  }
  Global(0x142b19a80) = GameAllocate(1);
  {
    void* memory = GameAllocate(0x148);
    Global(0x142b19a88) = memory ? game::Call<void* (*)(void*)>(0x140ab8210)(memory) : nullptr;
  }
  {
    void* memory = GameAllocate(0xAA8);
    Global(0x142b19a90) = memory ? game::Call<void* (*)(void*)>(0x14139f080)(memory) : nullptr;
  }
  {
    void* input = (*reinterpret_cast<void* (***)(uint8_t*)>(game))[0x88 / 8](game);
    (*reinterpret_cast<void (***)(void*, void*)>(input))[0x1B0 / 8](input, Global(0x142b19a90));
  }
  {
    void* memory = GameAllocate(0x70);
    Global(0x142b19a98) = memory ? game::Call<void* (*)(void*)>(0x140abfe80)(memory) : nullptr;
  }
  CreateService(Game, 8, 0x140aa75b0, 0, 0, 0x96D18, false, 0x142b19aa0);
  {
    void* memory = GameAllocate(0x4AD0);
    void* service = memory ? game::Call<void* (*)(void*)>(0x140aed470)(memory) : nullptr;
    game::Field<void*>(game, 0x42E48) = service;
    Global(0x142b19aa8) = service;
  }
  {
    void* memory = SoeHeapAllocate(0x1B58);
    Global(0x142b19ab0) = memory ? game::Call<void* (*)(void*)>(0x14039b870)(memory) : nullptr;
    initService(Global(0x142b19ab0));
  }
  // Drop our reference to the camera callback.
  if (auto* callback = reinterpret_cast<uint8_t*>(callbackHandle & ~1ull)) {
    if (_InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(callback + 8), -1) == 1)
      (*reinterpret_cast<void (***)(void*, int)>(callback))[0](callback, 1);
  }
  callbackHandle &= 1;
  instancesPath.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&instancesPath);
  audioPath.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&audioPath);
  return true;
}

// Runs one crash-report entry; a fault inside it skips just that entry (the
// original wraps each entry in try/catch(...) for the same reason).
template <class Entry>
void GuardedCrashEntry(Entry&& entry) {
  __try {
    entry();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
}

// 0x14042c8a0 (slot 117): WriteCrashInfo - add ~130 key/value entries about
// the client, character, renderer, animation and machine to the crash report
// writer at +0xA8.
void GameClientWriteCrashInfo(uint8_t* game) {
  void* writer = game + 0xA8;
  auto* client = *reinterpret_cast<uint8_t**>(0x142b19780);
  const char* percentS = reinterpret_cast<const char*>(0x142046fb8);  // "%s"
  auto key = [](uint64_t text) { return reinterpret_cast<const char*>(text); };
  using PrintfFn = void (*)(void*, const char*, const char*, ...);
  auto printfEntry = [&](uint64_t name, const char* format, auto... args) {
    game::Call<PrintfFn>(0x140316650)(writer, key(name), format, args...);
  };
  using ValueFn = void (*)(void*, const char*, const void*);
  auto valueEntry = [&](uint64_t writerFn, uint64_t name, const void* value) { game::Call<ValueFn>(writerFn)(writer, key(name), value); };
  auto textEntry = [&](uint64_t name, const char* text) { valueEntry(0x14035c750, name, &text); };
  auto intEntry = [&](uint64_t name, int value) { valueEntry(0x14035c3f0, name, &value); };
  // StringFixed<64> copy of a C string, written with "%s".
  auto copiedTextEntry = [&](uint64_t name, const char* text) {
    soeutil::StringFixed<64> copy;
    soeutil::InitFixed(copy, reinterpret_cast<void**>(0x142049d00));
    if (text && *text) soeutil::StringAssign(&copy, text);
    GuardedCrashEntry([&] { printfEntry(name, percentS, copy.data); });
    copy.vtable = reinterpret_cast<void**>(0x142049ce0);
    soeutil::StringRelease(&copy);
  };

  if (void* splash = *reinterpret_cast<void**>(0x142b19b08)) game::Call<void (*)(void*)>(0x141342530)(splash);
  copiedTextEntry(0x14204a000, *reinterpret_cast<const char**>(0x1429fbd88));  // "Version"
  GuardedCrashEntry([&] {
    uint64_t guid = game::Field<uint64_t>(client, 0x38BF0);
    valueEntry(0x14035cc30, 0x14206f5a0, &guid);  // "Player GUID"
  });
  GuardedCrashEntry([&] { valueEntry(0x14035cb60, 0x14206f5b0, client + 0x31DE8); });  // "StationId"
  copiedTextEntry(0x14206f5c0, game::Call<const char* (*)(uint8_t*, int)>(0x1403d5850)(client, game::Field<int>(game, 0x314A8)));  // "Client State"
  {
    uint64_t now;
    int64_t elapsed = static_cast<int64_t>(*game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&now) - game::Field<uint64_t>(client, 0x38810));
    int seconds = static_cast<int>(elapsed > 0x7fffffff ? 0x7fffffff : elapsed) / 1000;
    soeutil::StringFixed<64> text;
    soeutil::InitFixed(text, reinterpret_cast<void**>(0x142049d00));
    soeutil::StringFormat(&text, reinterpret_cast<const char*>(0x142047028), seconds);
    GuardedCrashEntry([&] { printfEntry(0x14206f5d0, percentS, text.data); });  // "AppRuntime Seconds"
    text.vtable = reinterpret_cast<void**>(0x142049ce0);
    soeutil::StringRelease(&text);
  }
  GuardedCrashEntry([&] { textEntry(0x14206f5e8, game::Field<const char*>(game, 0x42DF8)); });            // "Last Scaleform Url Requested"
  GuardedCrashEntry([&] { valueEntry(0x14035c3f0, 0x14206f608, reinterpret_cast<void*>(0x142b176c0)); });  // "Last critical error"
  GuardedCrashEntry([&] { textEntry(0x14206f620, *reinterpret_cast<const char**>(0x1429fbed8)); });       // "Last Critical error Meassage"
  GuardedCrashEntry([&] {
    textEntry(0x14206f640, game::Call<const char* (*)(int)>(0x140499ef0)(*reinterpret_cast<int*>(0x142b176c4)));  // "Shutdown Reason"
  });
  if (void* player = *reinterpret_cast<void**>(0x142b19ba0)) {
    GuardedCrashEntry([&] { intEntry(0x14206f650, game::Field<int>(game::Call<uint8_t* (*)(void*)>(0x1403f5110)(player), 4)); });  // "Active profile ID"
  }
  GuardedCrashEntry([&] { textEntry(0x14206f668, game::Field<const char*>(client, 0x38988)); });  // "Zone geometry"
  {
    soeutil::StringFixed<128> rulesets;
    soeutil::InitFixed(rulesets, reinterpret_cast<void**>(0x142049da8));
    soeutil::StringAssign(&rulesets, reinterpret_cast<const char*>(0x1420664f0));  // "Unknown"
    rulesets.vtable = reinterpret_cast<void**>(0x142049dc8);
    GuardedCrashEntry([&] {
      if (void* manager = *reinterpret_cast<void**>(0x142b19b78)) game::Call<void (*)(void*, soeutil::IString*)>(0x1416ccd10)(manager, &rulesets);
      printfEntry(0x14206f678, percentS, rulesets.data);  // "Rulesets"
    });
    rulesets.vtable = reinterpret_cast<void**>(0x142049da8);
    soeutil::StringRelease(&rulesets);
  }
  if (void* scheduler = *reinterpret_cast<void**>(0x142b19790)) {
    GuardedCrashEntry([&] { textEntry(0x14206f688, game::Call<const char* (*)(void*)>(0x141345780)(scheduler)); });  // "EventScheduler Current Event"
  }
  GuardedCrashEntry([&] { textEntry(0x14206f6a8, *reinterpret_cast<const char**>(0x142aaa1c8)); });               // "LastMaterialPaletteAssetName"
  GuardedCrashEntry([&] { textEntry(0x14206f6c8, *reinterpret_cast<const char**>(0x142aaa1e0)); });               // "...TextureName"
  GuardedCrashEntry([&] { valueEntry(0x14035c3f0, 0x14206f6f0, reinterpret_cast<void*>(0x142aaa210)); });          // "...TextureRefCount"
  GuardedCrashEntry([&] {
    uint64_t vtable = *reinterpret_cast<uint64_t*>(0x142aaa208);
    valueEntry(0x14035cb60, 0x14206f718, &vtable);  // "...TextureVTable"
  });
  GuardedCrashEntry([&] { intEntry(0x14206f740, *reinterpret_cast<uint8_t*>(0x142aaa214)); });                    // "...TextureIsReady"
  GuardedCrashEntry([&] { textEntry(0x14206f768, *reinterpret_cast<const char**>(0x142aaa1f8)); });               // "...TextureDebugState"
  auto doubleEntry = [&](uint64_t name, double value) { valueEntry(0x14035c670, name, &value); };
  auto flagEntry = [&](uint64_t name, bool set) {
    if (set) valueEntry(0x14035c0b0, name, reinterpret_cast<const void*>(0x1420632f0));  // "true"
  };
  const char* vectorFormat = reinterpret_cast<const char*>(0x14206fc58);  // "%f,%f,%f,%f"
  auto vectorEntry = [&](uint64_t name, const float* v) {
    printfEntry(name, vectorFormat, static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]), static_cast<double>(v[3]));
  };
  void* proxy = *reinterpret_cast<void**>(0x142b19b38);
  auto self = [&] { return game::Call<uint8_t* (*)(void*)>(0x14071e830)(proxy); };
  if (proxy && self()) {
    uint8_t* character = self();
    GuardedCrashEntry([&] {
      textEntry(0x14206f7d0, game::Field<const char*>(game::Call<uint8_t* (*)(uint8_t*)>(0x1416cb000)(character + 0x738), 8));  // "Character name"
    });
    GuardedCrashEntry([&] { textEntry(0x14206f7e0, game::Field<const char*>(character, 0x1CE8)); });  // "Actor definition name"
    GuardedCrashEntry([&] { textEntry(0x14206f808, game::Field<const char*>(character, 0x870)); });   // "Sub-text name"
    uint8_t* identity = character + 0x630;
    GuardedCrashEntry([&] {
      uint64_t scratch[2];
      void* guid = (*reinterpret_cast<void* (***)(void*, void*)>(identity))[0x68 / 8](identity, scratch);
      valueEntry(0x14035cc30, 0x14206f828, guid);  // "Character GUID"
    });
    GuardedCrashEntry([&] {
      uint64_t hash = game::Field<uint64_t>(character, 0x1D0);
      intEntry(0x14206f838, static_cast<int>(static_cast<uint32_t>(hash >> 32) ^ static_cast<uint32_t>(hash)));  // "Character hash key"
    });
    GuardedCrashEntry([&] { intEntry(0x14206f850, game::Field<int>(character, 0x1AC8)); });  // "Model ID"
    GuardedCrashEntry([&] { doubleEntry(0x14206f860, game::Call<float (*)(uint8_t*)>(0x1403f5ea0)(character)); });  // "Expected speed"
    GuardedCrashEntry([&] {
      uint64_t now;
      int64_t elapsed =
          static_cast<int64_t>(*game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(&now) - game::Field<uint64_t>(client, 0x38810));
      intEntry(0x14206f5d0, static_cast<int>(elapsed > 0x7fffffff ? 0x7fffffff : elapsed) / 1000);  // "AppRuntime Seconds"
    });
    GuardedCrashEntry([&] { flagEntry(0x14206f870, (character[0x1AD8] >> 6) & 1); });  // "Player IsStunned"
    GuardedCrashEntry([&] {
      void* combat = character + 0x320;
      flagEntry(0x14206f8a8, (*reinterpret_cast<bool (***)(void*)>(combat))[0xA8 / 8](combat));  // "Player IsKnockedOut"
    });
    GuardedCrashEntry([&] { flagEntry(0x14206f8e8, (game::Field<unsigned>(character, 0x1AD8) >> 9) & 1); });   // "Player IsKnockedBack"
    GuardedCrashEntry([&] { flagEntry(0x14206f928, (game::Field<unsigned>(character, 0x1AD8) >> 16) & 1); });  // "Player IsPull"
    GuardedCrashEntry([&] { flagEntry(0x14206f958, (game::Field<unsigned>(character, 0x1AD8) >> 8) & 1); });   // "Player IsNonattackable"
    GuardedCrashEntry([&] { flagEntry(0x14206f998, character[0x8CA] >= 0x80); });        // "Player IsFalling"
    GuardedCrashEntry([&] { flagEntry(0x14206f9d0, (character[0x8CA] & 1) != 0); });     // "Player IsIgnoringCollision"
    GuardedCrashEntry([&] { flagEntry(0x14206fa20, (character[0x8CA] & 0x40) != 0); });  // "Player IsJumping"
    GuardedCrashEntry([&] { flagEntry(0x14206fa58, (character[0x8CA] & 8) != 0); });     // "Player IsJuking"
    GuardedCrashEntry([&] { flagEntry(0x14206fa88, (character[0x8CB] & 0x40) != 0); });  // "Player IsInWater"
    GuardedCrashEntry([&] { flagEntry(0x14206fac0, (character[0x8CB] & 0x20) != 0); });  // "Player IsSwimming"
    GuardedCrashEntry([&] { flagEntry(0x14206fb00, game::Call<bool (*)(uint8_t*)>(0x140519500)(character)); });  // "Player IsMoving"
    GuardedCrashEntry([&] { intEntry(0x14206fb30, game::Field<int>(character, 0x1AC4)); });               // "Gender"
    GuardedCrashEntry([&] { textEntry(0x14206fb38, game::Field<const char*>(character, 0x1A00)); });     // "GetModelCustomizationName"
    GuardedCrashEntry([&] { textEntry(0x14206fb60, game::Field<const char*>(character, 0x19B8)); });     // "GetHairModelName"
    GuardedCrashEntry([&] { textEntry(0x14206fb80, game::Field<const char*>(character, 0x19D0)); });     // "GetHeadModelName"
    for (auto [slot, name] : {std::pair<int, uint64_t>{0xA8, 0x14206fba0}, {0xB0, 0x14206fbc0}}) {  // skin tone, face paint
      soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      GuardedCrashEntry([&] {
        auto* result = (*reinterpret_cast<soeutil::IString* (***)(void*, soeutil::IString*)>(identity))[slot / 8](identity, &text);
        textEntry(name, result->data);
      });
      text.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&text);
    }
    GuardedCrashEntry([&] {
      int terrain = game::Field<int>(character, 0x3C8);
      valueEntry(0x14035ca90, 0x14206fbe0, &terrain);  // "TerrainObjectId"
    });
    GuardedCrashEntry([&] { doubleEntry(0x14206fbf0, game::Call<float (*)(uint8_t*)>(0x14050dd30)(character)); });  // "Heading"
    GuardedCrashEntry([&] { doubleEntry(0x14206fbf8, game::Field<float>(character, 0x1A84)); });                    // "GetVerticalVelocity"
    GuardedCrashEntry([&] { doubleEntry(0x14206fc10, game::Field<float>(character, 0x1A80)); });                    // "GetVerticalOffset"
    GuardedCrashEntry([&] { doubleEntry(0x14206fc28, game::Call<float (*)(uint8_t*)>(0x14050cc70)(character)); });  // "DistanceToGround"
    GuardedCrashEntry([&] { doubleEntry(0x14206fc40, game::Field<float>(character, 0x1A88)); });                    // "GetPlayerBlendTimeSec"
    GuardedCrashEntry([&] {
      auto* position = (*reinterpret_cast<float* (***)(uint8_t*)>(character))[0x188 / 8](character);
      vectorEntry(0x14206fc68, position);  // "GetPosition"
    });
    GuardedCrashEntry([&] {
      alignas(16) float facing[4];
      game::Call<void (*)(uint8_t*, float*)>(0x14050daa0)(character, facing);
      vectorEntry(0x14206fc88, facing);  // "GetFacing"
    });
    GuardedCrashEntry([&] {
      alignas(16) float look[4];
      game::Call<void (*)(uint8_t*, float*)>(0x14050e2e0)(character, look);
      vectorEntry(0x14206fc98, look);  // "GetLookDirection"
    });
    GuardedCrashEntry([&] { vectorEntry(0x14206fcb0, game::Call<float* (*)(uint8_t*)>(0x140511070)(character)); });  // "GetVelocity"
    GuardedCrashEntry([&] { vectorEntry(0x14206fcc0, reinterpret_cast<float*>(character + 0xF80)); });              // "GetGroundNormal"
    GuardedCrashEntry([&] { vectorEntry(0x14206fcd0, reinterpret_cast<float*>(character + 0xF90)); });              // "GetGroundPosition"
  } else {
    GuardedCrashEntry([&] { valueEntry(0x14035c250, 0x14206f7b8, reinterpret_cast<const void*>(0x14206f798)); });  // "Character Pointer", "ProxiedCharacter is nullptr!"
    GuardedCrashEntry([&] { textEntry(0x14206f7d0, game::Field<const char*>(client, 0x38C08)); });                 // "Character name"
  }

  // Renderer / graphics device.
  auto* display = *reinterpret_cast<uint8_t**>(0x142b19788);
  if (!display) {
    GuardedCrashEntry([&] { intEntry(0x14206fce8, 0); });  // "GraphicsDllWrapper is nullptr!"
  } else {
    GuardedCrashEntry([&] { intEntry(0x14206fd08, game::Field<int>(display, 0x314)); });                                 // "Render level"
    GuardedCrashEntry([&] { intEntry(0x14206fd18, game::Call<int (*)(void*)>(0x1404d5aa0)(display)); });                // "Shadow quality"
    GuardedCrashEntry([&] { intEntry(0x14206fd28, game::Call<int (*)(void*)>(0x1404d5d30)(display)); });                // "Width"
    GuardedCrashEntry([&] { intEntry(0x14206fd30, game::Call<int (*)(void*)>(0x1404d5820)(display)); });                // "Height"
    GuardedCrashEntry([&] { intEntry(0x14206fd38, game::Call<int (*)(void*)>(0x1404d5830)(display)); });                // "Hz"
    GuardedCrashEntry([&] {
      bool fullscreen = game::Call<bool (*)(void*)>(0x1404d6d30)(display);
      textEntry(0x14206da18, reinterpret_cast<const char*>(fullscreen ? 0x1420632f0 : 0x1420632f8));  // "Fullscreen", "true"/"false"
    });
    if (game::Field<void*>(display, 0x50)) {
      GuardedCrashEntry([&] { vectorEntry(0x14206fd40, game::Call<float* (*)(void*)>(0x1404d5400)(display)); });  // "Camera position"
      GuardedCrashEntry([&] { vectorEntry(0x14206fd50, reinterpret_cast<float*>(display + 0x250)); });           // "Camera orientation"
      GuardedCrashEntry([&] {
        float angle = game::Field<float>(display, 0x30C);
        valueEntry(0x14035c590, 0x14206fd68, &angle);  // "ViewAngleHorz"
      });
    } else {
      GuardedCrashEntry([&] { valueEntry(0x14035c180, 0x14206fd40, reinterpret_cast<const void*>(0x14206fd78)); });  // "Camera position", "nullptr"
    }
    uint8_t* engine = game::Field<uint8_t*>(display, 0x40);
    void* device = nullptr;
    if (!engine) {
      GuardedCrashEntry([&] { intEntry(0x14206fd80, 0); });  // "Deep::Engine* is nullptr!"
    } else {
      device = game::Field<void*>(engine, 8);
    }
    if (!device) GuardedCrashEntry([&] { intEntry(0x14206fda0, 0); });  // "GraphicsDriver::DeviceInterface* is nullptr!"
    auto* adapter = game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b19780), 0x38888);
    auto adapterEntry = [&](uint64_t writerFn, uint64_t name, int offset) {
      GuardedCrashEntry([&] { valueEntry(writerFn, name, adapter + offset); });
    };
    auto deviceIntEntry = [&](uint64_t writerFn, uint64_t name, int slot) {
      GuardedCrashEntry([&] {
        int value = (*reinterpret_cast<int (***)(void*)>(device))[slot / 8](device);
        valueEntry(writerFn, name, &value);
      });
    };
    if (adapter) {
      adapterEntry(0x14035c4c0, 0x14206fdd0, 0x480);  // "DriverVersionHigh"
      adapterEntry(0x14035c4c0, 0x14206fde8, 0x484);  // "DriverVersionLow"
    }
    if (device) {
      deviceIntEntry(0x14035c4c0, 0x14206fe00, 0x328);  // "VendorID"
      deviceIntEntry(0x14035c4c0, 0x14206fe10, 0x320);  // "DeviceID"
    }
    if (adapter) {
      adapterEntry(0x14035c4c0, 0x14206fe20, 0x490);  // "SubsystemID"
      adapterEntry(0x14035c4c0, 0x14206fe30, 0x494);  // "RevisionID"
      adapterEntry(0x14035c9c0, 0x14206fe40, 0);      // "DriverName"
      adapterEntry(0x14035c8f0, 0x14206fe50, 0x440);  // "DeviceName"
      adapterEntry(0x14035c9c0, 0x14206fe60, 0x220);  // "DeviceDesc"
      adapterEntry(0x14035c4c0, 0x14206fe70, 0x4B0);  // "DesktopFormat"
      adapterEntry(0x14035c590, 0x14206fe80, 0x4A0);  // "MaxVertexShaderVersion"
      adapterEntry(0x14035c590, 0x14206fe98, 0x4A4);  // "MaxPixelShaderVersion"
      adapterEntry(0x14035cd00, 0x14206feb0, 0x517);  // "DX11Available"
    }
    if (device) {
      deviceIntEntry(0x14035c3f0, 0x14206fec0, 0x148);  // "MaxMrts"
      deviceIntEntry(0x14035c3f0, 0x14206fec8, 0x2F0);  // "GetTextureQuality"
      deviceIntEntry(0x14035c3f0, 0x14206fee0, 0x2E8);  // "GetVideoMemoryAvailableAtStartup"
    }
    if (auto* world = *reinterpret_cast<uint8_t**>(0x142b19918)) {
      auto worldActor = [&](int slot) { return (*reinterpret_cast<uint8_t* (***)(uint8_t*)>(world))[slot / 8](world); };
      uint8_t* drawing = worldActor(0x1C0);
      uint8_t* drawn = worldActor(0x1C8);
      uint8_t* updating = worldActor(0x1D0);
      // "%s @ x, y, z" with the actor position kept in a function-local static (thread-safe init).
      auto actorEntry = [&](uint8_t* actor, uint64_t name, uint64_t guard, uint64_t storage) {
        if (!actor || !game::Field<void*>(actor, 0x398)) return;
        GuardedCrashEntry([&] {
          auto* guardWord = reinterpret_cast<int*>(guard);
          unsigned tlsIndex = *reinterpret_cast<unsigned*>(0x143c466a8);
          auto* tls = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
          if (*guardWord > game::Field<int>(tls[tlsIndex], 0x10)) {
            game::Call<void (*)(int*)>(0x140d100ac)(guardWord);  // _Init_thread_header
            if (*guardWord == -1) {
              std::memset(reinterpret_cast<void*>(storage), 0, 16);
              game::Call<void (*)(int*)>(0x140d1004c)(guardWord);  // _Init_thread_footer
            }
          }
          alignas(16) uint8_t scratch[16];
          std::memcpy(reinterpret_cast<void*>(storage), game::Call<void* (*)(uint8_t*, void*)>(0x141443390)(actor, scratch), 16);
          auto* position = reinterpret_cast<float*>(storage);
          printfEntry(name, reinterpret_cast<const char*>(0x14206ff08), game::Field<const char*>(game::Field<uint8_t*>(actor, 0x398), 0x40),
                      static_cast<double>(position[0]), static_cast<double>(position[1]), static_cast<double>(position[2]));  // "%s @ %f, %f, %f"
        });
      };
      actorEntry(drawing, 0x14206ff18, 0x142b19d40, 0x142b19d30);   // "Last Actor Drawing"
      actorEntry(drawn, 0x14206ff30, 0x142b19d60, 0x142b19d50);     // "Last Actor Drawn"
      actorEntry(updating, 0x14206ff48, 0x142b19d80, 0x142b19d70);  // "Last Actor Updating"
    }
  }

  // Morpheme animation runtime.
  if (auto* morpheme = *reinterpret_cast<uint8_t**>(0x142b19d88)) {
    GuardedCrashEntry([&] { intEntry(0x14206ff60, (*reinterpret_cast<int (***)(void*)>(morpheme))[1](morpheme)); });  // "Morpheme PersistantMemoryUsage"
    unsigned used = 0;
    unsigned total = 0;
    auto usageEntry = [&](uint64_t name) {
      float percent = total ? static_cast<float>(used) / static_cast<float>(total) * *reinterpret_cast<float*>(0x1425ba0a0) : 0.0f;
      printfEntry(name, reinterpret_cast<const char*>(0x14206ff80), used, total, static_cast<double>(percent));  // "Used=%u Total=%u (%.2f%%)"
    };
    GuardedCrashEntry([&] {
      total = 0;
      used = (*reinterpret_cast<unsigned (***)(void*, unsigned*)>(morpheme))[2](morpheme, &total);
      usageEntry(0x14206ffa0);  // "Morpheme TemporaryMemoryUsage Watermark"
    });
    GuardedCrashEntry([&] {
      game::Call<void (*)(void*, unsigned*, unsigned*)>(0x1413a0b20)(*reinterpret_cast<void**>(0x142b19a90), &used, &total);
      usageEntry(0x14206ffc8);  // "Morpheme PhysicsManager TempMemoryUsage Watermark"
    });
    GuardedCrashEntry([&] { valueEntry(0x14035c820, 0x142070000, morpheme + 0xA478); });  // "Morpheme Last Removed"
    auto* network = game::Call<uint8_t* (*)(uint8_t*)>(0x14139e0b0)(morpheme);
    auto* definition = network ? game::Field<uint8_t*>(network, 0x10) : nullptr;
    if (definition) {
      GuardedCrashEntry([&] { textEntry(0x142070018, game::Field<const char*>(definition, 0x18)); });  // "Morpheme Last Network Updated"
      // Active requests: a request list {vtable 0x14206f580, head +8, +0x10, count +0x18, pool +0x20}.
      alignas(16) uint8_t requests[0x1000] = {};
      game::Field<uint64_t>(requests, 0) = 0x14206f580;
      game::Call<void (*)(void*)>(0x1403946b0)(requests + 0x20);
      alignas(16) uint8_t collector[0x20] = {};
      game::Field<uint64_t>(collector, 0) = 0x1420637e0;
      game::Call<void (*)(void*, void*)>(0x1403b3830)(collector, requests);
      game::Call<void (*)(void*, void*)>(0x141398100)(network, collector);
      using NameFn = soeutil::IString* (*)(void*, soeutil::IString*);
      for (auto* node = game::Field<uint8_t*>(requests, 8); node; node = game::Field<uint8_t*>(node, 8)) {
        soeutil::IString name;
        GuardedCrashEntry([&] {
          printfEntry(0x142070038, percentS, game::Call<NameFn>(0x140474ab0)(node, &name)->data);  // "Morpheme Active Request"
        });
        name.vtable = soeutil::IStringVtable();
        soeutil::StringRelease(&name);
      }
      auto netVtable = [&] { return *reinterpret_cast<void***>(network); };
      int nodeCount = reinterpret_cast<int (*)(void*)>(netVtable()[0x68 / 8])(network);
      for (int i = 0; i < nodeCount; ++i) {
        GuardedCrashEntry([&] {
          const char* nodeName = reinterpret_cast<const char* (*)(void*, int)>(netVtable()[0x70 / 8])(network, i);
          printfEntry(0x142070050, percentS, nodeName);  // "Morpheme Active Node"
        });
      }
      int variableCount = game::Call<int (*)(uint8_t*)>(0x141381230)(network);
      auto variableType = [&](uint64_t id) { return game::Call<int (*)(uint8_t*, uint64_t)>(0x141381280)(network, id); };
      for (int i = 0; i < variableCount; ++i) {
        uint64_t id = 0;
        game::Call<void (*)(uint8_t*, uint64_t*, int)>(0x141381260)(network, &id, i);
        const char* variable = reinterpret_cast<const char*>(0x142070070);  // "Morpheme Variable"
        if (variableType(id) == 0) {
          soeutil::IString name;
          GuardedCrashEntry([&] {
            const char* text = game::Call<NameFn>(0x140474ab0)(&id, &name)->data;
            float value = reinterpret_cast<float (*)(void*, uint64_t, float, int)>(netVtable()[0xE8 / 8])(network, id, 0.0f, 0);
            game::Call<PrintfFn>(0x140316650)(writer, variable, reinterpret_cast<const char*>(0x142070068), text, static_cast<double>(value));  // "%s = %f"
          });
          name.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&name);
        } else if (variableType(id) == 3) {
          soeutil::IString name;
          GuardedCrashEntry([&] {
            alignas(16) float value[4];
            reinterpret_cast<void (*)(void*, float*, uint64_t, const void*)>(netVtable()[0xF8 / 8])(network, value, id,
                                                                                                  reinterpret_cast<const void*>(0x142b06ac0));
            const char* text = game::Call<NameFn>(0x140474ab0)(&id, &name)->data;
            game::Call<PrintfFn>(0x140316650)(writer, variable, reinterpret_cast<const char*>(0x142070088), text, static_cast<double>(value[0]),
                                              static_cast<double>(value[1]), static_cast<double>(value[2]));  // "%s = %f %f %f"
          });
          name.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&name);
        } else if (variableType(id) == 4) {
          soeutil::IString name;
          GuardedCrashEntry([&] {
            alignas(16) float fallback[4];
            std::memcpy(fallback, reinterpret_cast<const void*>(0x142b06ac0), sizeof(fallback));
            alignas(16) float value[4];
            reinterpret_cast<void (*)(void*, float*, uint64_t, const float*)>(netVtable()[0x108 / 8])(network, value, id, fallback);
            const char* text = game::Call<NameFn>(0x140474ab0)(&id, &name)->data;
            game::Call<PrintfFn>(0x140316650)(writer, variable, reinterpret_cast<const char*>(0x142070098), text, static_cast<double>(value[0]),
                                              static_cast<double>(value[1]), static_cast<double>(value[2]),
                                              static_cast<double>(value[3]));  // "%s = %f %f %f %f"
          });
          name.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&name);
        }
      }
      game::Call<void (*)(void*)>(0x1403a7370)(requests);  // request list destructor
    }
  }

  // Machine information.
  uint64_t memory[6];
  if (game::Call<bool (*)(uint64_t*)>(0x141341e10)(memory)) {
    const char* totalAvail = reinterpret_cast<const char*>(0x1420700b0);  // "Total %I64d, Avail %I64d"
    GuardedCrashEntry([&] { printfEntry(0x1420700d0, totalAvail, memory[0], memory[1]); });  // "Physical Memory"
    GuardedCrashEntry([&] { printfEntry(0x1420700e0, totalAvail, memory[4], memory[5]); });  // "Virtual Memory"
    GuardedCrashEntry([&] { printfEntry(0x1420700f0, totalAvail, memory[2], memory[3]); });  // "Page File"
  }
  soeutil::IString osName{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  soeutil::IString osVersion{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  game::Call<void (*)(soeutil::IString*)>(0x14032e460)(&osName);
  game::Call<void (*)(soeutil::IString*)>(0x14032e590)(&osVersion);
  GuardedCrashEntry([&] { printfEntry(0x142070110, reinterpret_cast<const char*>(0x142070100), osName.data, osVersion.data); });  // "%s Version %s"
  GuardedCrashEntry([&] {
    uint8_t is64Bit = game::Call<uint8_t (*)(uint8_t*)>(0x14047ba80)(client);
    valueEntry(0x14035cd00, 0x142070118, &is64Bit);  // "64-bit Processor"
  });
  soeutil::StringFixed<64> processor;
  soeutil::InitFixed(processor, reinterpret_cast<void**>(0x142049d00));
  game::Call<void (*)(soeutil::IString*)>(0x141349700)(&processor);
  GuardedCrashEntry([&] { textEntry(0x142070130, processor.data); });  // "Processor"
  soeutil::StringFixed<64> systemName;
  soeutil::InitFixed(systemName, reinterpret_cast<void**>(0x142049d00));
  if (game::Call<bool (*)(soeutil::IString*)>(0x14032e0c0)(&systemName))
    GuardedCrashEntry([&] { textEntry(0x142070140, systemName.data); });  // "System Name"
  else
    GuardedCrashEntry([&] { valueEntry(0x14035c180, 0x142070140, reinterpret_cast<const void*>(0x1420664f0)); });  // "Unknown"
  systemName.vtable = reinterpret_cast<void**>(0x142049ce0);
  soeutil::StringRelease(&systemName);
  processor.vtable = reinterpret_cast<void**>(0x142049ce0);
  soeutil::StringRelease(&processor);
  osVersion.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&osVersion);
  osName.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&osName);
}


// StringFixed<64> built the game's way for input lookups (IStringFixed vtable
// while assigning, then the StringFixed<64> vtable).
void InitInputName(soeutil::StringFixed<64>& text, const char* value) {
  soeutil::InitFixed(text, reinterpret_cast<void**>(0x142049ce0));
  soeutil::StringAssign(&text, value);
  text.vtable = reinterpret_cast<void**>(0x142049d00);
}
void ReleaseInputName(soeutil::StringFixed<64>& text) {
  text.vtable = reinterpret_cast<void**>(0x142049ce0);
  soeutil::StringRelease(&text);
}

// Input binding state for (category, action) from the controls at +0x388A0
// (0x14060c080); the binding's +0xE0 bit 0 means "pressed this frame".
uint8_t* InputBinding(uint8_t* game, const char* category, const char* action) {
  soeutil::StringFixed<64> actionName;
  InitInputName(actionName, action);
  soeutil::StringFixed<64> categoryName;
  InitInputName(categoryName, category);
  auto* binding = game::Call<uint8_t* (*)(void*, soeutil::IString*, soeutil::IString*, bool, void*, bool)>(0x14060c080)(
      game::Field<void*>(game, 0x388A0), &categoryName, &actionName, true, nullptr, true);
  ReleaseInputName(categoryName);
  ReleaseInputName(actionName);
  return binding;
}

// Same lookup, releasing the names through the StringFixed<64> destructor
// (0x1402baa30) as some call sites do.
uint8_t* InputBindingDestructed(uint8_t* game, const char* category, const char* action) {
  soeutil::StringFixed<64> actionName;
  InitInputName(actionName, action);
  soeutil::StringFixed<64> categoryName;
  InitInputName(categoryName, category);
  auto* binding = game::Call<uint8_t* (*)(void*, soeutil::IString*, soeutil::IString*, bool, void*, bool)>(0x14060c080)(
      game::Field<void*>(game, 0x388A0), &categoryName, &actionName, true, nullptr, true);
  game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&categoryName);
  game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&actionName);
  return binding;
}

// 0x140433680 (slot 90): HandleInputActions - poll the "Generic" input
// actions (HUD toggles, group invites, quick chat, minimap zoom, escape,
// map/inventory/builder toggles, ...) and run their UI scripts / events.
void GameClientHandleInputActions(uint8_t* game) {
  const char* generic = reinterpret_cast<const char*>(0x14206ec50);  // "Generic"
  auto pressed = [&](uint64_t action) { return (InputBinding(game, generic, reinterpret_cast<const char*>(action))[0xE0] & 1) != 0; };
  auto runScript = [](uint64_t command) {
    game::Call<bool (*)(void*, const char*, void*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(command), nullptr, nullptr);
  };
  auto fireConsoleEvent = [](uint64_t name) {
    if (void* console = *reinterpret_cast<void**>(0x143bd4830)) {
      const char* text = reinterpret_cast<const char*>(name);
      soeutil::IString event{soeutil::IStringVtable(), const_cast<char*>(text), static_cast<int>(std::strlen(text)), -1};
      game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(console, &event, nullptr, nullptr);
      event.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&event);
    }
  };
  auto fireAssignedEvent = [](uint64_t name) {
    if (!*reinterpret_cast<void**>(0x143bd4830)) return;
    soeutil::IString event{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
    game::Call<void (*)(soeutil::IString*, const char*, int)>(0x1402ee880)(&event, reinterpret_cast<const char*>(name), -1);
    game::Call<void (*)(void*, soeutil::IString*, void*, void*)>(0x1409511d0)(*reinterpret_cast<void**>(0x143bd4830), &event, nullptr, nullptr);
    event.vtable = soeutil::IStringVtable();
    soeutil::StringRelease(&event);
  };
  auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  int inputSettingA = game::Field<int>(game::Field<uint8_t*>(game, 0x31418), 0x8044);
  int inputSettingB = game::Field<int>(game::Field<uint8_t*>(game, 0x31418), 0x8048);
  bool genericActive;
  {
    soeutil::StringFixed<64> category;
    InitInputName(category, generic);
    genericActive = game::Call<bool (*)(void*, soeutil::IString*)>(0x140611910)(game::Field<void*>(game, 0x388A0), &category);
    ReleaseInputName(category);
  }
  if (!genericActive) goto finish;
  {
    auto* recorder = *reinterpret_cast<uint8_t**>(0x142b19b98);
    bool connected = recorder && game::Field<void*>(recorder, 8) &&
                     game::Call<bool (*)(void*)>(0x14063bdd0)(game::Field<void*>(recorder, 8));
    void* ui = game::Field<void*>(game, 0x38AC8);
    bool focused = GetForegroundWindow() == game::Field<HWND>(game, 0x387F0) &&
                   !game::Call<void* (*)(void*, int)>(0x140cf41c0)(ui, game::Call<int (*)(void*)>(0x140cf3dc0)(ui));
    if (!focused) goto unfocused;
    if (!connected) goto offline;
    if (pressed(0x14206ec58)) {  // "ToggleHudIndicators"
      runScript(0x14206ec70);    // "HudHandler:ToggleHUDIndicators"
      fireConsoleEvent(0x14206ec90);  // "EVENT_TOGGLE_HUD_INDICATORS"
    }
    for (auto [action, accept] : {std::pair<uint64_t, bool>{0x14206ecb0, true}, {0x14206ed00, false}}) {  // Accept/DeclineGroupInvite
      if (!pressed(action)) continue;
      if (uint8_t* self = player()) game::Call<void (*)(uint8_t*, bool)>(0x1405f9190)(self + 0x9418, accept);
      runScript(0x14206ecc8);         // "GameEvents:HideGroupInvite"
      fireConsoleEvent(0x14206ece8);  // "EVENT_HIDE_GROUP_INVITE"
    }
    {  // VoiceMacro: toggles the quick-chat menu; while open, QuickChat1..10 pick a macro.
      bool voiceMacro;
      {
        soeutil::StringFixed<64> actionName;
        InitInputName(actionName, reinterpret_cast<const char*>(0x14206ed18));  // "VoiceMacro"
        soeutil::StringFixed<64> categoryName;
        InitInputName(categoryName, generic);
        auto* binding = game::Call<uint8_t* (*)(void*, soeutil::IString*, soeutil::IString*, bool, void*, bool)>(0x14060c080)(
            game::Field<void*>(game, 0x388A0), &categoryName, &actionName, true, nullptr, true);
        uint8_t state = binding[0xE0];
        game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&categoryName);  // StringFixed<64> destructor
        game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&actionName);
        voiceMacro = (state & 1) != 0;
      }
      if (voiceMacro) {
        if (game[0x3884B])
          game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);  // close
        else
          game::Call<void (*)(uint8_t*)>(0x1404704a0)(game);  // open
      }
      if (game[0x3884B]) {
        const char* quickChat = reinterpret_cast<const char*>(0x14206ed38);  // "QuickChat"
        soeutil::IString name{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
        for (int macro = 1; macro <= 10; ++macro) {
          soeutil::StringFormat(&name, reinterpret_cast<const char*>(0x14206ed28), macro);  // "QuickChat%d"
          bool chosen;
          {
            soeutil::StringFixed<64> actionName;
            InitInputName(actionName, name.data);
            soeutil::StringFixed<64> categoryName;
            InitInputName(categoryName, quickChat);
            auto* binding = game::Call<uint8_t* (*)(void*, soeutil::IString*, soeutil::IString*, bool, void*, bool)>(0x14060c080)(
                game::Field<void*>(game, 0x388A0), &categoryName, &actionName, true, nullptr, true);
            chosen = (binding[0xE0] & 1) != 0;
            ReleaseInputName(categoryName);
            game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&actionName);
          }
          if (!chosen) continue;
          {
            soeutil::StringFixed<64> actionName;
            InitInputName(actionName, name.data);
            soeutil::StringFixed<64> categoryName;
            InitInputName(categoryName, quickChat);
            game::Call<void (*)(void*, soeutil::IString*, soeutil::IString*)>(0x14060aef0)(game::Field<void*>(game, 0x388A0), &categoryName,
                                                                                         &actionName);
            game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&categoryName);
            game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&actionName);
          }
          game::Call<void (*)(void*, int)>(0x140949db0)(game::Field<void*>(game, 0x38958), macro);
          soeutil::IString handler{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          using FindHandlerFn = bool (*)(void*, const char*, soeutil::IString*);
          if (game::Call<FindHandlerFn>(0x14048a5c0)(UiRoot(), reinterpret_cast<const char*>(0x14206e460), &handler)) {  // "ChatHandler"
            game::Call<void (*)(soeutil::IString*, const char*)>(0x1402bd730)(&handler, reinterpret_cast<const char*>(0x14206ed48));  // ":OnVoiceChatMacroSelect"
            ScriptArgs args{reinterpret_cast<void**>(0x14206c548), nullptr, nullptr};
            if (auto* slot = game::Call<int* (*)(ScriptArgs*, int)>(0x140418710)(&args, 0)) *slot = 0;
            game::Call<void (*)(void*, int)>(0x14046d7b0)(args.begin, macro);
            game::Call<bool (*)(void*, const char*, ScriptArgs*, void*)>(0x140488cc0)(UiRoot(), handler.data, &args, nullptr);
            game::Call<void (*)(ScriptArgs*)>(0x1403a06c0)(&args);
          }
          game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);
          handler.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&handler);
          break;
        }
        name.vtable = soeutil::IStringVtable();
        soeutil::StringRelease(&name);
      }
    }
    {  // Minimap zoom (held or tapped) scales the map zoom at +0x38B58 (+0xEDC).
      float step = *reinterpret_cast<float*>(0x14207283c);
      auto zoom = [&](uint8_t* binding, uint64_t heldScale, uint64_t tapScale) {
        auto* map = game::Field<uint8_t*>(game, 0x38B58);
        if (!map || !binding) return;
        float value = game::Field<float>(map, 0xEDC);
        using TestFn = bool (*)(uint8_t*, float);
        if ((game::Field<unsigned>(binding, 0xE0) >> 1) & 1 && game::Call<TestFn>(0x140428200)(binding, step) &&
            game::Call<TestFn>(0x140428130)(binding, step)) {
          value *= *reinterpret_cast<float*>(heldScale);
        } else if (binding[0xC8] && !game::Call<TestFn>(0x140428130)(binding, step)) {
          value *= *reinterpret_cast<float*>(tapScale);
        } else {
          return;
        }
        game::Call<void (*)(void*, float)>(0x14067cc50)(game::Field<void*>(game, 0x38B58), value);
      };
      zoom(InputBinding(game, generic, reinterpret_cast<const char*>(0x14206ed60)), 0x14207285c, 0x142072864);  // "IncrementMinimapZoom"
      zoom(InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ed78)), 0x142072880, 0x142072870);  // "DecrementMinimapZoom"
    }
  offline:
    if (pressed(0x14206ed90)) {       // "CancelZoneQueue"
      runScript(0x14206eda0);         // "ServerQueueHandler:CancelQueue"
      fireAssignedEvent(0x14206edc0);  // "EVENT_CANCEL_SERVER_QUEUE"
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ede0))[0xE0] & 1) {  // "StartCommandText"
      game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);
      game::Call<void (*)(uint8_t*)>(0x140471350)(game);
    }
  unfocused:
    {
      // Mouse position (+0x3D538/+0x3D53C, valid when +0x3D541) relative to the
      // screen size read from +0x31418 drives two corner "hot spots".
      float farEdge = *reinterpret_cast<float*>(0x14207284c);
      float nearEdge = *reinterpret_cast<float*>(0x142072844);
      auto mouseX = [&] { return static_cast<float>(game::Field<int>(game, 0x3D538)); };
      auto mouseY = [&] { return static_cast<float>(game::Field<int>(game, 0x3D53C)); };
      if (!game::Call<bool (*)(void*)>(0x140cf5530)(game::Field<void*>(game, 0x38AC8))) {
        uint8_t* binding = InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206edf8));  // "ToggleDebugConsole"
        bool toggle = (binding[0xE0] & 1) ||
                      (game[0x3D541] && mouseX() > static_cast<float>(inputSettingA) * farEdge && mouseY() > static_cast<float>(inputSettingB) * farEdge);
        if (toggle) {
          game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);
          game::Call<void (*)(uint8_t*)>(0x140474b50)(game);
        }
      }
      uint8_t* escapeBinding = InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ee0c));  // "Escape"
      bool escape = (escapeBinding[0xE0] & 1) ||
                    (game[0x3D541] && mouseX() > static_cast<float>(inputSettingA) * nearEdge && mouseX() < static_cast<float>(inputSettingA) * farEdge &&
                     mouseY() > static_cast<float>(inputSettingB) * nearEdge && mouseY() < static_cast<float>(inputSettingB) * farEdge);
      if (escape) {
        bool closedMenu = false;
        if (game::Call<bool (*)(uint8_t*)>(0x14040d960)(game) && game[0x3884B]) {
          game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);
          closedMenu = true;
        }
        void* mode = game::Field<void*>(game, 0x388A8);
        if (mode && (*reinterpret_cast<int (***)(void*)>(mode))[0](mode) == 0x1F) {
          auto& shown = *reinterpret_cast<uint8_t*>(0x142b19e39);
          if (!shown) {
            game::Call<void (*)()>(0x140355520)();
            shown = 1;
          }
          game::Call<void (*)(void*, bool)>(0x140770140)(*reinterpret_cast<void**>(0x142b19ad0), true);
        } else if (!closedMenu) {
          void* current = game::Field<void*>(game, 0x388A8);  // not null-checked in the original
          if ((*reinterpret_cast<bool (***)(void*)>(current))[0xA8 / 8](current)) {
            fireAssignedEvent(0x14206ee18);  // "EVENT_ON_ESCAPE"
            if (!game[0x38EB2]) {
              soeutil::StringFixed<256> state;
              soeutil::InitFixed(state, reinterpret_cast<void**>(0x142049e08));
              game::Call<void (*)(uint8_t*, soeutil::IString*)>(0x1403f5ff0)(game, &state);
              ScriptArgs args{reinterpret_cast<void**>(0x14206c548), nullptr, nullptr};
              void* value = game::Call<void* (*)(ScriptArgs*, int)>(0x1403b4810)(&args, 0);
              game::Call<void (*)(void*, const char*)>(0x14046be30)(value, state.data);
              game::Call<bool (*)(void*, const char*, ScriptArgs*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(0x14206ee28), &args,
                                                                                       nullptr);  // "GameEvents:OnEscape"
              game::Call<void (*)(ScriptArgs*)>(0x1403a06c0)(&args);
              game::Call<void (*)(soeutil::IString*)>(0x140305cf0)(&state);  // StringFixed<256> destructor
            }
          }
        }
      }
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x1420547f0))[0xE0] & 1) {  // "HideUi"
      runScript(0x14206ee40);          // "GameEvents:OnHideUi"
      fireAssignedEvent(0x14206ee58);  // "EVENT_TOGGLE_UI"
    }
    if (!game::Call<bool (*)(uint8_t*)>(0x14040d960)(game) && game::Call<bool (*)(void*)>(0x140cf5530)(game::Field<void*>(game, 0x38AC8)))
      goto afterGameplay;
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ee68))[0xE0] & 1)  // "ToggleFullscreen"
      game::Call<void (*)(uint8_t*)>(0x140474c40)(game);
    if (!connected) goto afterGameplay;
    {
      float farEdge = *reinterpret_cast<float*>(0x14207284c);
      float nearEdge = *reinterpret_cast<float*>(0x142072844);
      uint8_t* binding = InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ee80));  // "OpenMap"
      bool openMap = (binding[0xE0] & 1) || (game[0x3D541] && static_cast<float>(game::Field<int>(game, 0x3D538)) > static_cast<float>(inputSettingA) * farEdge &&
                                             static_cast<float>(game::Field<int>(game, 0x3D53C)) < static_cast<float>(inputSettingB) * nearEdge);
      void* mode = game::Field<void*>(game, 0x388A8);
      if (openMap && (*reinterpret_cast<bool (***)(void*)>(mode))[0xA8 / 8](mode)) {  // mode not null-checked in the original
        void* proxy = *reinterpret_cast<void**>(0x142b19b38);
        if (game::Call<void* (*)(void*)>(0x14071e830)(proxy)) {
          void* character = game::Call<void* (*)(void*)>(0x14071e830)(*reinterpret_cast<void**>(0x142b19b38));
          game::Call<void (*)(void*)>(0x140920360)(character);
          if (game::Call<bool (*)(void*)>(0x140427f00)(*reinterpret_cast<void**>(0x142b19780)) ||
              (!game[0x38EB2] && game::Call<bool (*)(void*)>(0x14091d9a0)(character))) {
            fireAssignedEvent(0x14206db50);  // "EVENT_SET_MENU_STATE"
            runScript(0x14206ee88);          // "GameEvents:OnMapToggle"
            fireAssignedEvent(0x14206eea0);  // "EVENT_TOGGLE_MAP"
            if (uint8_t* self = player()) game::Call<void (*)(uint8_t*)>(0x140637570)(self);
          }
        }
      }
    }
    {
      auto* profile = *reinterpret_cast<uint8_t**>(0x142b19ba0);
      auto* self = game::Call<uint8_t* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860));
      if (profile && self) {
        auto heldItem = [&] { return (*reinterpret_cast<uint8_t* (***)(void*)>(self))[0x5B8 / 8](self); };
        uint8_t* binding = InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206eef0));  // "ToggleInventory"
        bool toggle = (binding[0xE0] & 1) || (heldItem() && profile[0xB008]);
        if (toggle) {
          uint8_t* vehicle = game::Field<uint8_t*>(self, 0x3920);
          if (vehicle && game::Field<int>(vehicle, 0x2C) == 2 && game::Call<void* (*)(void*)>(0x14050f340)(self) &&
              game::Call<void* (*)(void*)>(0x14050c160)(game::Call<void* (*)(void*)>(0x14050f340)(self)) == self)
            game::Call<void (*)(void*)>(0x1406375e0)(profile);
          if (game::Call<bool (*)(void*)>(0x140519500)(self) && !game::Call<void* (*)(void*)>(0x14050f300)(self) &&
              !(heldItem()[0x38] & 0x10)) {  // held item not null-checked in the original
            profile[0xB008] = 1;
          } else {
            if (game[0x38EB2] && heldItem()) {
              heldItem();
              *reinterpret_cast<uint8_t*>(0x142a00d50) = 0;
            }
            void* mode = game::Field<void*>(game, 0x388A8);
            if ((*reinterpret_cast<bool (***)(void*)>(mode))[0xA8 / 8](mode)) {
              runScript(0x14206eeb8);          // "GameEvents:OnInventoryToggle"
              fireAssignedEvent(0x14206eed8);  // "EVENT_TOGGLE_INVENTORY"
              profile[0xB008] = 0;
              if (uint8_t* p = player()) game::Call<void (*)(uint8_t*)>(0x140637560)(p);
            }
          }
        }
      } else {
        runScript(0x14206eeb8);          // "GameEvents:OnInventoryToggle"
        fireAssignedEvent(0x14206eed8);  // "EVENT_TOGGLE_INVENTORY"
      }
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ef00))[0xE0] & 1) {  // "ToggleBuilder"
      if (void* builder = *reinterpret_cast<void**>(0x142b19c98)) {
        void* target = (*reinterpret_cast<void* (***)(void*)>(builder))[0x28 / 8](builder);
        if (game::Call<int (*)(void*)>(0x1403f6080)(target) == 1) {
          runScript(0x14206ef10);          // "GameEvents:OnBuilderToggle"
          fireAssignedEvent(0x14206ef30);  // "EVENT_TOGGLE_BUILDER"
        }
      }
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ef48))[0xE0] & 1) {  // "ToggleWeaponStance"
      if (void* character = game::Call<void* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860)))
        (*reinterpret_cast<void (***)(void*)>(character))[0x460 / 8](character);
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206ef60))[0xE0] & 1) {  // "ToggleHUDResources"
      game::Call<void (*)(void*, const char*, void*, void*)>(0x140cf39b0)(game::Field<void*>(game, 0x38AC8),
                                                                          reinterpret_cast<const char*>(0x14206ef78), nullptr, nullptr);  // "ToggleResources"
      fireAssignedEvent(0x14206ef88);  // "EVENT_TOGGLE_RESOURCES"
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206efa0))[0xE0] & 1) {  // "ToggleUI"
      bool hidden = !game[0x38EB3];
      game[0x38EB3] = hidden;
      if (game[0x38EB2]) game::Call<void (*)(void*, bool)>(0x140954630)(*reinterpret_cast<void**>(0x143bd4830), hidden);
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206efb0))[0xE0] & 1)  // "ToggleVideoCapture"
      runScript(0x14206efc8);  // "VideoHandler:ToggleVideoCapture"
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206efe8))[0xE0] & 1)  // "PointAndReport"
      game::Call<void (*)(uint8_t*)>(0x1404318e0)(game);
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206eff8))[0xE0] & 1)  // "PointAndInvite"
      game::Call<void (*)(uint8_t*)>(0x1404316e0)(game);
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f008))[0xE0] & 1) {  // "ToggleMuteAll"
      auto* options = *reinterpret_cast<uint8_t**>(0x142b199f0);
      uint8_t muted = options[0x2E9A];
      game::Call<void (*)(void*, bool)>(0x140ab51f0)(options, !muted);
      if (!muted)
        game::Call<void (*)(uint8_t*)>(0x14042ae90)(game);
      else
        game::Call<void (*)(uint8_t*)>(0x140466000)(game);
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f018))[0xE0] & 1)  // "ToggleInstantActionTimer"
      runScript(0x14206f038);  // "GameEvents:OnInstantActionToggle"
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f060))[0xE0] & 1)  // "ToggleVehicleManagementView"
      runScript(0x14206f080);  // "HudHandler:ToggleVehicleManagementView"
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f0a8))[0xE0] & 1)  // "CycleChatTabs"
      game::Call<void (*)(uint8_t*)>(0x1403e2b30)(game);
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f0b8))[0xE0] & 1) {  // "CycleMainWindowTabs"
      game::Call<void (*)(void*, const char*, void*, void*)>(0x140cf39b0)(game::Field<void*>(game, 0x38AC8),
                                                                          reinterpret_cast<const char*>(0x14206f0b8), nullptr, nullptr);
      fireAssignedEvent(0x14206f0d0);  // "EVENT_CYCLE_WINDOW_TABS"
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f0e8))[0xE0] & 1) {  // "StartChatText"
      game::Call<void (*)(uint8_t*)>(0x14040dfc0)(game);
      game::Call<void (*)(uint8_t*, int)>(0x140471070)(game, 0);
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f0f8))[0xE0] & 1) {  // "AutoJoinSquad"
      if (!game::Call<bool (*)(uint8_t*)>(0x1405f7000)(player() + 0x9418)) {  // player not null-checked in the original
        game::Call<void (*)(uint8_t*)>(0x1405f8bb0)(player() + 0x9418);
      } else if (void* squads = *reinterpret_cast<void**>(0x142b19c70)) {
        game::Call<void (*)(void*)>(0x140a885e0)(squads);
      }
    }
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f108))[0xE0] & 1) {  // "Reply"
      if (void* chat = game::Field<void*>(game, 0x388B8)) game::Call<void (*)(void*)>(0x1409a7880)(chat);
    }
    InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f110));  // "TogglePerformance" (result unused)
    if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f128))[0xE0] & 1) {  // "ToggleNightVision"
      void* character = game::Call<void* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860));  // not null-checked
      if (void* vision = (*reinterpret_cast<void* (***)(void*)>(character))[0x288 / 8](character))
        game::Call<void (*)(void*, uint32_t)>(0x140931e10)(vision, 0x2be7f704);
    }
    {  // Render distance +/- step, clamped, pushed to the renderer, options and console.
      float step = *reinterpret_cast<float*>(0x1420728c4);
      auto setRenderDistance = [&](float value) {
        auto* render = *reinterpret_cast<void**>(0x142b19788);
        float current = game::Call<float (*)(void*)>(0x1404d5900)(render);
        if (!(value < current || value > current)) return;  // ucomiss/je: equal or unordered skips
        game::Call<void (*)(void*, float)>(0x1404da040)(render, value);
        if (void* options = *reinterpret_cast<void**>(0x142b199f0)) game::Call<void (*)(void*, float)>(0x140ab5850)(options, value);
        if (*reinterpret_cast<void**>(0x143bd4830)) {
          soeutil::IString event{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          game::Call<void (*)(soeutil::IString*, const char*, int)>(0x1402ee880)(&event, reinterpret_cast<const char*>(0x14206f158), -1);  // "EVENT_RENDER_DISTANCE_CHANGED"
          game::Call<void (*)(void*, soeutil::IString*, float*)>(0x1403584f0)(*reinterpret_cast<void**>(0x143bd4830), &event, &value);
          event.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&event);
        }
      };
      if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f140))[0xE0] & 1) {  // "IncreaseRenderDistance"
        if (void* render = *reinterpret_cast<void**>(0x142b19788)) {
          float value = game::Call<float (*)(void*)>(0x1404d5900)(render) + step;
          float maximum = *reinterpret_cast<float*>(0x142188038);
          if (value > maximum) value = maximum;
          setRenderDistance(value);
        }
      }
      if (InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f178))[0xE0] & 1) {  // "DecreaseRenderDistance"
        if (void* render = *reinterpret_cast<void**>(0x142b19788)) {
          float value = game::Call<float (*)(void*)>(0x1404d5900)(render) - step;
          float minimum = *reinterpret_cast<float*>(0x14218803c);
          if (!(value >= minimum)) value = minimum;
          setRenderDistance(value);
        }
      }
    }
    game::Call<void (*)(uint8_t*)>(0x14043beb0)(game);
    {  // Proximity voice chat: the first held channel key (table at 0x142a007c0) picks the channel.
      uint8_t* self = player();
      uint8_t* voice = self ? game::Field<uint8_t*>(self, 0x99F0) : nullptr;
      if (!voice || !voice[0x1B8]) goto afterGameplay;
      struct VoiceChannelKey {
        const char* action;
        int channel;
      };
      void* channel = nullptr;
      for (auto* key = reinterpret_cast<VoiceChannelKey*>(0x142a007c0); key->action; ++key) {
        uint8_t* binding = InputBindingDestructed(game, generic, key->action);
        if (!binding[0xC8] || channel) continue;
        if ((binding[0xE0] & 1) && key->channel == 1 && (*reinterpret_cast<uint8_t**>(0x142b199f0))[0x2EBD]) {
          soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          void* strings = *reinterpret_cast<void**>(0x142b19798);
          (*reinterpret_cast<void (***)(void*, const char*, soeutil::IString*)>(strings))[0x18 / 8](
              strings, reinterpret_cast<const char*>(0x14206f190), &text);  // "UserOptionProximityChatDisabled"
          void* chat = *reinterpret_cast<void**>(0x142b19b88);
          int color = game::Call<int (*)()>(0x1416dfd10)();
          int style = game::Call<int (*)()>(0x1416dfdf0)();
          (*reinterpret_cast<void (***)(void*, const char*, int, int, int, bool, void*, bool)>(chat))[0x28 / 8](chat, text.data, 0, style, color,
                                                                                                             false, nullptr, true);
          text.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&text);
        }
        channel = game::Call<void* (*)(void*, int)>(0x14075f3d0)(voice, key->channel);
      }
      uint8_t* custom = InputBindingDestructed(game, generic, reinterpret_cast<const char*>(0x14206f1b0));  // "VoiceChatCustom"
      if (custom[0xC8] && !channel) channel = game::Call<void* (*)(void*, void*)>(0x14075f3f0)(voice, voice + 0x120);
      if (channel) {
        game::Call<void (*)(void*, void*)>(0x140766ed0)(voice, channel);
        game::Call<void (*)(void*, bool)>(0x1407663f0)(voice, false);
      } else if ((*reinterpret_cast<uint8_t**>(0x142b199f0))[0x3286]) {
        game::Call<void (*)(void*, bool)>(0x1407663f0)(voice, true);
      }
    }
  afterGameplay:;
  }
finish:;
  if ((*reinterpret_cast<bool (***)(uint8_t*)>(game))[0x90 / 8](game) || (player() && player()[0x109D9])) {
    bool demoActive;
    {
      soeutil::StringFixed<64> demo;
      InitInputName(demo, reinterpret_cast<const char*>(0x14206f1c0));  // "Demo"
      demoActive = game::Call<bool (*)(void*, soeutil::IString*)>(0x140611910)(game::Field<void*>(game, 0x388A0), &demo);
      game::Call<void (*)(soeutil::IString*)>(0x1402baa30)(&demo);
    }
    if (demoActive && (game::Field<unsigned>(InputBindingDestructed(game, reinterpret_cast<const char*>(0x14206f1c0),
                                                                    reinterpret_cast<const char*>(0x14206f1c8)),  // "ReviveMe"
                                             0xE0) >> 1) & 1)
      (*reinterpret_cast<void (***)(uint8_t*, const char*)>(game))[0x158 / 8](game, reinterpret_cast<const char*>(0x14206f1d8));
  }
  game::Field<uint16_t>(game, 0x3D541) = 0;
  {
    void* overlay = *reinterpret_cast<void**>(0x142b19b10);
    game[0x38DEC] = (!overlay || !game::Call<bool (*)(void*)>(0x14065c150)(overlay)) && GetForegroundWindow() == game::Field<HWND>(game, 0x387F0);
  }
}

// Packet 0x11 header {vtable, opcode byte (in an int preset to 0x11), short
// sub-type (in an int)} as the local player's pre-dispatch sees it.
struct Packet11Header {
  void** vtable;
  int opcode;
  int pad0C;
  int subType;
  int pad14;
};
static_assert(sizeof(Packet11Header) == 0x18);

// 0x140404980 (slot 93): packet 0x11 - the local player may consume it first
// (0x14062fb40); otherwise read the sub-packet and apply it (stats, abilities,
// loyalty points, rewards UI, squad, jobs, ...).
bool GameClientHandlePacket11(uint8_t* game, const uint8_t* data, int length) {
  Packet11Header header{reinterpret_cast<void**>(0x142065370), 0x11, 0, 0, 0};
  if (!data) return false;
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 1;
  bool failed = false;
  if (cursor <= end) {
    header.opcode = (header.opcode & ~0xFF) | data[0];
  } else {
    header.opcode &= ~0xFF;
    failed = true;
    cursor = end;
  }
  if (cursor + 2 > end) return false;
  header.subType = *reinterpret_cast<const int16_t*>(cursor);
  if (failed) return false;
  auto player = [&] { return game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80); };
  if (player() && game::Call<bool (*)(uint8_t*, Packet11Header*, const uint8_t*, int)>(0x14062fb40)(player(), &header, data, length)) return true;

  alignas(16) uint8_t storage[Packet83::kPacket83Bytes];
  using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
  using ReaderFn = void (*)(uint8_t*, ClientReader*);
  auto readWithReader = [&](Packet83& packet, uint64_t read) {
    ClientReader reader{data, length, data, end, 0};
    game::Call<ReaderFn>(read)(packet.At(0), &reader);
    return !static_cast<uint8_t>(reader.failed) && static_cast<int>(reader.end - reader.cursor) <= 0;
  };
  auto read = [&](Packet83& packet, uint64_t fn) { return game::Call<ReadFn>(fn)(packet.At(0), data, length, false); };
  auto blobReader = [](Packet83& packet, int pointerOffset, int lengthOffset) {
    auto* blob = packet.Get<const uint8_t*>(pointerOffset);
    int blobLength = packet.Get<int>(lengthOffset);
    return ClientReader{blob, blobLength, blob, blob + blobLength, 0};
  };
  auto character = [&] { return game::Call<uint8_t* (*)(void*)>(0x14071e830)(game::Field<void*>(game, 0x38860)); };
  // Apply to the local player when the id matches, else to that entity.
  auto applyToOwner = [&](uint64_t id, uint64_t playerFn, uint64_t entityFn, void* arg) {
    uint8_t* self = player();
    if (!self) return false;
    if (id == game::Field<uint64_t>(self, 0x18)) {
      game::Call<void (*)(uint8_t*, void*)>(playerFn)(self, arg);
    } else {
      uint64_t key = id;
      if (void* entity = game::Call<void* (*)(void*, uint64_t*)>(0x14071ee60)(game::Field<void*>(game, 0x38860), &key))
        game::Call<void (*)(void*, void*)>(entityFn)(entity, arg);
    }
    return true;
  };
  auto releasePacketString = [](Packet83& packet, int offset) {
    auto* text = reinterpret_cast<soeutil::IString*>(packet.At(offset));
    text->vtable = soeutil::IStringVtable();
    soeutil::StringRelease(text);
  };

  switch (header.subType) {
    case 0x48: {  // localized server name -> global string 0x142b178a8, refresh 0x142b17840
      Packet83 packet(0x142065620, 0x48, storage, 0x11);
      packet.Put<soeutil::IString>(0x18, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      if (read(packet, 0x14038d4d0)) {
        soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
        void* strings = *reinterpret_cast<void**>(0x142b19798);
        (*reinterpret_cast<void (***)(void*, const char*, soeutil::IString*)>(strings))[0x18 / 8](strings, packet.Get<const char*>(0x20), &text);
        auto* current = reinterpret_cast<soeutil::IString*>(0x142b178a8);
        if (!(static_cast<int>(*reinterpret_cast<uint64_t*>(0x142b178b8)) == text.length &&
              std::memcmp(current->data, text.data, text.length) == 0)) {
          game::Call<void (*)(soeutil::IString*, soeutil::IString*)>(0x1402bd560)(current, &text);
          game::Call<void (*)(void*)>(0x140cff570)(reinterpret_cast<void*>(0x142b17840));
        }
        text.vtable = soeutil::IStringVtable();
        soeutil::StringRelease(&text);
      }
      releasePacketString(packet, 0x18);
      break;
    }
    case 0x49: {  // rewards / grinder disabled flags -> HUD
      Packet83 packet(0x142065628, 0x49, storage, 0x11);
      if (readWithReader(packet, 0x14036d460)) {
        ScriptArgList args{reinterpret_cast<void**>(0x14206c548), nullptr, 0, 0};
        if (auto* type = game::Call<int* (*)(ScriptArgList*, int)>(0x140418710)(&args, 0)) *type = 0;
        game::Call<void (*)(void*, bool)>(0x14046d940)(args.values, packet.Get<uint8_t>(0x18));
        game::Call<bool (*)(void*, const char*, ScriptArgList*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(0x14206ea08), &args,
                                                                                    nullptr);  // "HudHandler:setRewardsDisabled"
        if (args.count <= 0) {
          if (auto* type = game::Call<int* (*)(ScriptArgList*, int)>(0x140418710)(&args, 0)) *type = 0;
        }
        game::Call<void (*)(void*, bool)>(0x14046d940)(args.values, packet.Get<uint8_t>(0x19));
        game::Call<bool (*)(void*, const char*, ScriptArgList*, void*)>(0x140488cc0)(UiRoot(), reinterpret_cast<const char*>(0x14206ea28), &args,
                                                                                    nullptr);  // "HudHandler:setGrinderDisabled"
        game::Call<void (*)(ScriptArgList*)>(0x1403a06c0)(&args);
      }
      break;
    }
    case 0x1F: {  // squad data -> squad manager 0x142b19c70
      Packet83 packet(0x142065498, 0x1F, storage, 0x11);
      packet.Put<uint64_t>(0x18, 0x142065478);
      packet.Put<uint64_t>(0x38, 0x142065478);
      if (readWithReader(packet, 0x14036cae0))
        game::Call<void (*)(void*, uint8_t*)>(0x140a88220)(*reinterpret_cast<void**>(0x142b19c70), packet.At(0));
      game::Call<void (*)(uint8_t*)>(0x1403a7400)(packet.At(0x38));
      game::Call<void (*)(uint8_t*)>(0x1403a7400)(packet.At(0x18));
      break;
    }
    case 0x1E: {  // character's +0x280 object vfunc 0x980 with the packet's values
      Packet83 packet(0x142065470, 0x1E, storage, 0x11);
      packet.Put<int>(0x1C, *reinterpret_cast<int*>(0x142b186ac));
      if (readWithReader(packet, 0x14036b9e0)) {
        if (uint8_t* self = character()) {
          void* target = (*reinterpret_cast<void* (***)(void*)>(self))[0x280 / 8](self);
          if (target) {
            using ApplyFn = void (*)(void*, int, float, float, uint8_t, uint8_t, float, int);
            (*reinterpret_cast<ApplyFn**>(target))[0x980 / 8](target, packet.Get<int>(0x20), packet.Get<float>(0x24), packet.Get<float>(0x28),
                                                              packet.Get<uint8_t>(0x2C), packet.Get<uint8_t>(0x2D), packet.Get<float>(0x30),
                                                              packet.Get<int>(0x34));
          }
        }
      }
      break;
    }
    case 1: {  // progress (current/max) notification on the character
      Packet83 packet(0x142065378, 1, storage, 0x11);
      if (!readWithReader(packet, 0x14036c140)) return false;
      {
        if (uint8_t* self = character()) {
          int ownerId = game::Field<int>(self, 0x1AA4);
          int current = packet.Get<int>(0x18);
          int maximum = packet.Get<int>(0x1C);
          int percent = maximum == 0 ? 100 : current * 100 / maximum;
          void* notice = nullptr;
          if (void* memory = game::Call<void* (*)(size_t)>(0x140839910)(0x90)) {
            int id = ownerId;
            alignas(16) uint8_t handle[16];
            uint8_t* source = self + 0x630;
            void* name = (*reinterpret_cast<void* (***)(void*, void*)>(source))[0x68 / 8](source, handle);
            notice = game::Call<void* (*)(void*, int*, void*, int, int, int)>(0x1408397e0)(memory, &id, name, current, maximum, percent);
          }
          // not null-checked in the original
          bool flag = (*reinterpret_cast<bool (***)(void*)>(notice))[0x40 / 8](notice);
          float delay = game::Call<float (*)(uint8_t*, bool)>(0x1404e3f70)(self, flag);
          (*reinterpret_cast<void (***)(uint8_t*, float, void*, int, float)>(self))[0x1E0 / 8](self, delay, notice, 1, 0.0f);
        }
      }
      break;
    }
    case 0xB: {
      Packet83 packet(0x142065380, 0xB, storage, 0x11);
      if (!readWithReader(packet, 0x14036c440)) return false;
              game::Call<void (*)(void*, uint8_t*, int, int)>(0x140a07880)(game::Field<void*>(game, 0x38B78), character(), packet.Get<int>(0x18),
                                                                     packet.Get<int>(0x1C));
      break;
    }
    case 2: {
      Packet83 packet(0x142065388, 2, storage, 0x11);
      packet.Put<uint64_t>(0x18, *reinterpret_cast<uint64_t*>(0x142b181f8));
      if (!readWithReader(packet, 0x14036c1f0)) return false;
      {
        ClientReader blob = blobReader(packet, 0x20, 0x28);
        applyToOwner(packet.Get<uint64_t>(0x18), 0x140630da0, 0x1405145f0, &blob);
      }
      break;
    }
    case 3: {
      alignas(16) uint8_t state[0x60];
      game::Call<void (*)(uint8_t*)>(0x1416d7a10)(state);
      Packet83 packet(0x142065390, 3, storage, 0x11);
      packet.Put<uint64_t>(0x18, *reinterpret_cast<uint64_t*>(0x142b181f8));
      packet.Put<uint8_t*>(0x20, state);
      bool ok = read(packet, 0x14038d6b0);
      if (ok) applyToOwner(packet.Get<uint64_t>(0x18), 0x1406313a0, 0x140514700, state);
      game::Call<void (*)(uint8_t*)>(0x1416d7a60)(state);
      if (!ok) return false;
      break;
    }
    case 4: {
      Packet83 packet(0x142065398, 4, storage, 0x11);
      packet.Put<uint64_t>(0x20, *reinterpret_cast<uint64_t*>(0x142b17e70));
      if (!readWithReader(packet, 0x14036c2e0)) return false;
      {
        if (applyToOwner(packet.Get<uint64_t>(0x18), 0x1406311e0, 0x1405146b0, packet.At(0x20))) {
          if (void* window = game::Field<void*>(game, 0x38900)) game::Call<void (*)(void*, int)>(0x1409e4be0)(window, 0);
        }
      }
      break;
    }
    case 5: {  // stat list: apply each, refresh the stat whose id matches config hash 0x85c3f4d2
      Packet83 packet(0x1420653c0, 5, storage, 0x11);
      packet.Put<uint64_t>(0x18, 0x142064078);
      bool ok = read(packet, 0x14038a4e0);
      if (ok && player()) {
        uint8_t* self = character();
        for (auto* stat = packet.Get<uint8_t*>(0x20); stat; stat = game::Field<uint8_t*>(stat, 0x10)) {
          game::Call<void (*)(uint8_t*, uint8_t*)>(0x140638ec0)(player(), stat);
          int trackedId = 0;
          for (auto* node = game::Field<uint8_t*>(*reinterpret_cast<uint8_t**>(0x142b197a0), 0x26F0); node; node = game::Field<uint8_t*>(node, 0x20)) {
            if (game::Field<uint32_t>(node, 0x18) == 0x85c3f4d2) {
              trackedId = static_cast<int>(game::Field<double>(node, 0));
              break;
            }
          }
          if (game::Field<int>(stat, 0) != trackedId) continue;
          auto value = [&] {
            return game::Field<int>(stat, 4) == 1 ? game::Field<float>(stat, 0xC) + game::Field<float>(stat, 8)
                                                  : static_cast<float>(game::Field<int>(stat, 0xC) + game::Field<int>(stat, 8));
          };
          if (void* mode = game::Field<void*>(game, 0x388A8)) (*reinterpret_cast<void (***)(void*, float)>(mode))[0xB8 / 8](mode, value());
          if (self) game::Field<float>(self, 0xDA8) = value();
        }
        if (packet.Get<int>(0x30) > 0) {
          game::Call<void (*)(uint8_t*)>(0x140638fb0)(player());
          if (self) (*reinterpret_cast<void (***)(uint8_t*)>(self))[0x778 / 8](self);
        }
      }
      packet.Put<uint64_t>(0, 0x1420653c0);
      game::Call<void (*)(uint8_t*)>(0x1403a71b0)(packet.At(0x18));
      if (!ok) return false;
      break;
    }
    case 0xA: {  // teleport / location update: position +0x20, rotation +0x30, flags +0x40..+0x42
      Packet83 packet(0x1420653c8, 0xA, storage, 0x11);
      if (!readWithReader(packet, 0x14036cfe0)) return false;
      void* proximity = *reinterpret_cast<void**>(0x142b19a10);
      if (game::Call<bool (*)(void*)>(0x140427f00)(*reinterpret_cast<void**>(0x142b19780)) && proximity) {
        // Collect the tracked entries, then re-register each by its id.
        ScriptArgList entries{reinterpret_cast<void**>(0x14206e9c8), nullptr, 0, 0};
        struct Collector {
          void** vtable;
          ScriptArgList* target;
          uint8_t pad10[0x28];
          Collector* self;
        };
        static_assert(sizeof(Collector) == 0x40);
        Collector collector{};
        collector.vtable = reinterpret_cast<void**>(0x1420727e0);
        collector.target = &entries;
        collector.self = &collector;
        game::Call<void (*)(void*, Collector*)>(0x1413f4060)(proximity, &collector);
        // (the inlined array-grow path in the original loop is unreachable: i < count)
        for (int i = 0; i < entries.count; ++i) {
          uint64_t id = game::Field<uint64_t>(reinterpret_cast<uint8_t**>(entries.values)[i], 0x18);
          uint64_t handle[2] = {};
          auto* resolved = game::Call<uint64_t* (*)(uint64_t*, uint64_t*)>(0x14039c860)(handle, &id);
          (*reinterpret_cast<void (***)(void*, uint64_t)>(proximity))[0x28 / 8](proximity, *resolved);
        }
        game::Call<void (*)(ScriptArgList*)>(0x14039fca0)(&entries);
      }
      uint8_t* state = game::Field<uint8_t*>(game, 0x314A8);
      if (state[0x1B8]) {
        state[0x1B8] = 0;
        game[0x38EF1] = 1;
        game::Call<void (*)(void*, const char*, ...)>(0x1402bd7f0)(game + 0x38EF8, reinterpret_cast<const char*>(0x14206ea50),
                                                                   static_cast<double>(packet.Get<float>(0x20)), static_cast<double>(packet.Get<float>(0x24)),
                                                                   static_cast<double>(packet.Get<float>(0x28)));  // "Finished waiting for location update ..."
      }
      uint8_t* self = character();
      if (packet.Get<uint8_t>(0x40) && self) {
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t*)>(0x140535390)(self, packet.At(0x20), packet.At(0x30));
        alignas(16) float velocity[4];
        std::memcpy(velocity, reinterpret_cast<void*>(0x142b06ac0), sizeof(velocity));
        game::Call<void (*)(uint8_t*, float*)>(0x1405365a0)(self, velocity);
        game::Field<int>(self, 0x1A84) = 0;
        (*reinterpret_cast<void (***)(uint8_t*, int)>(self))[0x430 / 8](self, 0);
        (*reinterpret_cast<void (***)(uint8_t*, int)>(self))[0x420 / 8](self, 0);
        game::Call<void (*)(uint8_t*)>(0x140514d90)(self);
        game::Field<int>(self, 0xFC0) = 0;
        if (packet.Get<uint8_t>(0x41) != self[0x5B0]) self[0x5B0] = packet.Get<uint8_t>(0x41);
        if (packet.Get<uint8_t>(0x42)) game[0x38EF1] = 1;
        alignas(16) float facing[4];
        auto* direction = game::Call<float* (*)(uint8_t*, float*)>(0x14050daa0)(self, facing);
        float heading = game::Call<float (*)(float, float)>(0x140d55d44)(direction[0], direction[2]);  // atan2f
        void* mode = game::Field<void*>(game, 0x388A8);
        (*reinterpret_cast<void (***)(void*, float)>(mode))[0x128 / 8](mode, heading);
        if (void* minimap = game::Field<void*>(game, 0x38A18)) game::Call<void (*)(void*)>(0x140997e20)(minimap);
        game::Call<void (*)(void*)>(0x1409f1310)(game::Field<void*>(game, 0x38978));
        auto* camera = game::Call<float* (*)(void*)>(0x1404d5400)(game::Field<void*>(game, 0x38890));
        float dx = packet.Get<float>(0x20) - camera[0];
        float dz = packet.Get<float>(0x28) - camera[2];
        float dw = packet.Get<float>(0x2C) - camera[3];
        float distanceSquared = (dz * dz + dw * dw) + (dx * dx + 0.0f);
        void* streamer = game::Field<void*>(game, 0x3B6F8);
        if (distanceSquared >= game::Call<float (*)(void*)>(0x141868560)(streamer)) game::Call<void (*)(void*)>(0x141868420)(streamer);
        float radius = static_cast<float>(static_cast<uint64_t>(game::Call<uint32_t (*)(void*)>(0x1418685d0)(streamer)));
        if (!game::Call<bool (*)(void*, uint8_t*, float)>(0x1418683f0)(streamer, packet.At(0x20), radius))
          game::Call<void (*)(void*, uint8_t*)>(0x1418688b0)(streamer, packet.At(0x20));
        (*reinterpret_cast<void (***)(uint8_t*)>(self))[0x7A0 / 8](self);
      }
      int zero = 0;
      game::Call<void (*)(void*, uint8_t*, uint8_t*, int*)>(0x14047e720)(game::Field<void*>(game, 0x388A8), packet.At(0x20), packet.At(0x30), &zero);
      break;
    }
    case 0x23: {  // entity transform: id +0x18, position +0x20, direction +0x30, flags +0x40/+0x41
      Packet83 packet(0x1420653d0, 0x23, storage, 0x11);
      if (!readWithReader(packet, 0x14036d220)) return false;
      uint64_t id = packet.Get<uint64_t>(0x18);
      uint8_t* entity = game::Call<uint8_t* (*)(uint8_t*, uint64_t*)>(0x1403f83f0)(game, &id);
      if (!packet.Get<uint8_t>(0x40) || !entity) return true;
      alignas(16) float up[4];
      std::memcpy(up, reinterpret_cast<void*>(0x142b06af0), sizeof(up));
      auto body = [&] {
        uint8_t* physics = entity + 0x20;
        return (*reinterpret_cast<uint8_t* (***)(void*)>(physics))[0xB0 / 8](physics);
      };
      uint8_t* target = body();
      struct Transform {
        float position[4];
        float direction[4];
        float up[4];
      };
      alignas(16) Transform transform{};
      std::memcpy(transform.position, packet.At(0x20), 16);
      float v[4];
      std::memcpy(v, packet.At(0x30), 16);
      float magnitude = std::sqrt((v[2] * v[2] + v[3] * v[3]) + (v[0] * v[0] + v[1] * v[1]));
      if (magnitude > *reinterpret_cast<float*>(0x142047918)) {
        for (int i = 0; i < 4; ++i) transform.direction[i] = v[i] / magnitude;
      } else {
        std::memcpy(transform.direction, reinterpret_cast<void*>(0x142b06a70), 16);
      }
      std::memcpy(transform.up, up, 16);
      (*reinterpret_cast<void (***)(void*, Transform*, int)>(target))[0x30 / 8](target, &transform, 0);
      alignas(16) float zero[4];
      std::memcpy(zero, reinterpret_cast<void*>(0x142b06ac0), 16);
      target = body();
      (*reinterpret_cast<void (***)(void*, float*, int)>(target))[0x40 / 8](target, zero, 0);
      alignas(16) float zero2[4];
      std::memcpy(zero2, reinterpret_cast<void*>(0x142b06ac0), 16);
      target = body();
      (*reinterpret_cast<void (***)(void*, float*, int)>(target))[0x50 / 8](target, zero2, 0);
      entity[0x5B0] = packet.Get<uint8_t>(0x41);
      void* vehicle = (*reinterpret_cast<void* (***)(uint8_t*)>(entity))[0x298 / 8](entity);
      if (!vehicle) return true;
      uint8_t* self = character();
      void* mode = game::Field<void*>(game, 0x388A8);
      if (mode && (*reinterpret_cast<int (***)(void*)>(mode))[0](mode) == 0x29 && game::Call<void* (*)(uint8_t*)>(0x14050f340)(self) == vehicle)
        game::Call<void (*)(void*)>(0x1406e6be0)(game::Field<void*>(game, 0x388A8));
      break;
    }
    case 6: {  // add a quest/objective object (0x370) to the player's list at +0xADD0
      Packet83 packet(0x1420653a0, 6, storage, 0x11);
      if (!readWithReader(packet, 0x14036b840)) return false;
      uint8_t* object = nullptr;
      if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x370)) object = game::Call<uint8_t* (*)(void*, int)>(0x14039aeb0)(memory, 0);
      ClientReader blob = blobReader(packet, 0x18, 0x20);
      game::Call<void (*)(uint8_t*, ClientReader*)>(0x140368860)(object, &blob);
      if (game::Call<void* (*)(uint8_t*, int)>(0x1404c0470)(player() + 0xADD0, game::Field<int>(object, 0x138))) {
        game::Call<void (*)(uint8_t*, int)>(0x1403c2100)(object, 1);  // already known: delete
        break;
      }
      game::Call<void (*)(uint8_t*, uint8_t*)>(0x1404bfba0)(player() + 0xADD0, object);
      if (game::Call<bool (*)(uint8_t*)>(0x1404c0950)(object)) {
        if (void* tracker = game::Field<void*>(game, 0x38B78)) game::Call<void (*)(void*, uint8_t*)>(0x140a03b10)(tracker, object);
      }
      break;
    }
    case 7: {
      Packet83 packet(0x1420653a8, 7, storage, 0x11);
      if (!read(packet, 0x140389920)) return false;
      game::Call<void (*)(uint8_t*, int)>(0x1404c0b30)(player() + 0xADD0, packet.Get<int>(0x18));
      break;
    }
    case 8: {  // objective progress update (0x50-byte entry keyed by +0x34)
      Packet83 packet(0x1420653b0, 8, storage, 0x11);
      packet.Put<uint64_t>(0x20, 0x142064ae8);
      if (!readWithReader(packet, 0x14036b540)) return false;
      int key = packet.Get<int>(0x34);
      uint8_t* existing = game::Call<uint8_t* (*)(uint8_t*, int)>(0x1404c0470)(player() + 0xADD0, key);
      bool wasIncomplete = existing && !game::Call<bool (*)(uint8_t*)>(0x1404c0950)(existing);
      uint8_t* entry = game::Call<uint8_t* (*)(size_t)>(0x1402fc0f0)(0x50);
      if (entry) {
        game::Field<int>(entry, 0) = packet.Get<int>(0x18);
        game::Field<uint64_t>(entry, 8) = 0x142064ae8;
        game::Field<int>(entry, 0x10) = packet.Get<int>(0x28);
        game::Field<int>(entry, 0x14) = packet.Get<int>(0x2C);
        game::Field<int>(entry, 0x18) = packet.Get<int>(0x30);
        game::Field<int>(entry, 0x1C) = key;
        game::Field<int>(entry, 0x20) = packet.Get<int>(0x38);
        game::Field<int>(entry, 0x24) = packet.Get<int>(0x3C);
        entry[0x28] = packet.Get<uint8_t>(0x40);
        game::Field<uint64_t>(entry, 0x40) = 0;
        game::Field<uint64_t>(entry, 0x48) = 0;
        game::Field<uint64_t>(entry, 0x30) = 0;
        game::Field<int>(entry, 0x38) = 0;
      }
      game::Call<void (*)(uint8_t*, int, uint8_t*)>(0x1404bfcd0)(player() + 0xADD0, key, entry);
      if (wasIncomplete && existing && game::Call<bool (*)(uint8_t*)>(0x1404c0950)(existing)) {
        if (void* tracker = game::Field<void*>(game, 0x38B78)) game::Call<void (*)(void*, uint8_t*)>(0x140a03b10)(tracker, existing);
      }
      game::Call<void (*)(void*, uint8_t*, uint8_t*)>(0x140a03ea0)(game::Field<void*>(game, 0x38B78), existing, entry);
      if (existing && game::Call<bool (*)(uint8_t*)>(0x1404c0920)(existing))
        game::Call<void (*)(void*, uint8_t*)>(0x140a03780)(game::Field<void*>(game, 0x38B78), existing);
      break;
    }
    case 9: {
      Packet83 packet(0x1420653b8, 9, storage, 0x11);
      if (!readWithReader(packet, 0x14036b790)) return false;
              game::Call<void (*)(uint8_t*, int, int)>(0x1404c0c00)(player() + 0xADD0, packet.Get<int>(0x18), packet.Get<int>(0x1C));
      break;
    }
    case 0x1D: {  // two player counters (+0x1E8/+0x1EC); player not null-checked in the original
      Packet83 packet(0x1420653d8, 0x1D, storage, 0x11);
      if (!readWithReader(packet, 0x14036cf30)) return false;
      game::Field<int>(player(), 0x1E8) = packet.Get<int>(0x18);
      game::Field<int>(player(), 0x1EC) = packet.Get<int>(0x1C);
      break;
    }
    case 0x3C: {  // three player lists (+0x20, +0x38, +0x50) then refresh +0x3340
      Packet83 packet(0x1420655e0, 0x3C, storage, 0x11);
      packet.Put<uint64_t>(0x20, 0x142063770);
      packet.Put<uint64_t>(0x38, 0x142063770);
      packet.Put<uint64_t>(0x50, 0x1420655c0);
      bool ok = readWithReader(packet, 0x14036cbd0);
      if (ok && player()) {
        game::Field<int>(player(), 0x198) = packet.Get<int>(0x18);
        game::Call<void (*)(uint8_t*, uint8_t*)>(0x1406387e0)(player(), packet.At(0x20));
        game::Call<void (*)(uint8_t*, uint8_t*)>(0x140638740)(player(), packet.At(0x38));
        game::Call<void (*)(uint8_t*, uint8_t*)>(0x1406386a0)(player(), packet.At(0x50));
        uint8_t* self = player();
        game::Call<void (*)(uint8_t*)>(0x1404c6920)(self ? self + 0x3340 : nullptr);
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae3d0)(packet.At(0));
      if (!ok) return false;
      break;
    }
    case 0xD: {  // job level up: update the job, show "JobLevelUp" with (id, name, +0x18, level, +0x1C)
      Packet83 packet(0x1420653e0, 0xD, storage, 0x11);
      if (!readWithReader(packet, 0x14036b3f0)) break;
      ClientReader blob = blobReader(packet, 0x18, 0x20);
      uint8_t* job = nullptr;
      game::Call<void (*)(uint8_t*, ClientReader*, uint8_t**)>(0x14035cf50)(player(), &blob, &job);
      ScriptArgList args{reinterpret_cast<void**>(0x14206e9e8), nullptr, 0, 0};
      soeutil::IString name{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      void* strings = *reinterpret_cast<void**>(0x142b19798);
      (*reinterpret_cast<void (***)(void*, int, soeutil::IString*)>(strings))[0x10 / 8](strings, game::Field<int>(job, 8), &name);
      using SlotFn = uint8_t* (*)(ScriptArgList*, int);
      using SetIntFn = void (*)(uint8_t*, int);
      int value = game::Field<int>(job, 4);
      game::Call<SetIntFn>(0x14046d7b0)(game::Call<SlotFn>(0x1403b4810)(&args, 0), value);
      game::Call<void (*)(uint8_t*, soeutil::IString*)>(0x14046d690)(game::Call<SlotFn>(0x1403b4810)(&args, 1), &name);
      value = game::Field<int>(job, 0x18);
      game::Call<SetIntFn>(0x14046d7b0)(game::Call<SlotFn>(0x1403b4810)(&args, 2), value);
      value = game::Call<int (*)(uint8_t*, int)>(0x1403f8650)(player() + 0xDD70, game::Field<int>(job, 4));
      game::Call<SetIntFn>(0x14046d7b0)(game::Call<SlotFn>(0x1403b4810)(&args, 3), value);
      value = game::Field<int>(job, 0x1C);
      game::Call<SetIntFn>(0x14046d7b0)(game::Call<SlotFn>(0x1403b4810)(&args, 4), value);
      game::Call<void (*)(void*, int, const char*, ScriptArgList*, bool, int, int)>(0x140a04370)(
          game::Field<void*>(game, 0x38B78), 2, reinterpret_cast<const char*>(0x14206ea98), &args, false, 0, 0);  // "JobLevelUp"
      (*reinterpret_cast<void (***)(uint8_t*, int)>(game))[0x190 / 8](game, game::Field<int>(job, 4));
      for (int offset : {0xAD70, 0xAD78}) {
        void* view = game::Field<void*>(player(), offset);
        (*reinterpret_cast<void (***)(void*)>(view))[0x88 / 8](view);
      }
      name.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&name);
      game::Call<void (*)(ScriptArgList*)>(0x1403a0980)(&args);
      break;
    }
    case 0x16: {  // job update
      Packet83 packet(0x1420653e8, 0x16, storage, 0x11);
      if (!read(packet, 0x14038a470)) break;
      ClientReader blob = blobReader(packet, 0x18, 0x20);
      uint8_t* job = nullptr;
      game::Call<void (*)(uint8_t*, ClientReader*, uint8_t**)>(0x14035cfd0)(player(), &blob, &job);
      (*reinterpret_cast<void (***)(uint8_t*, int)>(game))[0x190 / 8](game, game::Field<int>(job, 4));
      for (int offset : {0xAD70, 0xAD78}) {
        void* view = game::Field<void*>(player(), offset);
        (*reinterpret_cast<void (***)(void*)>(view))[0x88 / 8](view);
      }
      break;
    }
    case 0x13: {  // job change: equip the job item, refresh level-dependent entity state
      uint8_t* packet = storage;  // 0x88-byte packet built by its own constructor
      game::Call<void (*)(uint8_t*)>(0x14039b590)(packet);
      if (!game::Call<ReadFn>(0x1403895e0)(packet, data, length, false)) {
        game::Call<void (*)(uint8_t*)>(0x1403adf20)(packet);
        return false;
      }
      auto* blobData = game::Field<const uint8_t*>(packet, 0x18);
      int blobLength = game::Field<int>(packet, 0x20);
      ClientReader blob{blobData, blobLength, blobData, blobData + blobLength, 0};
      uint8_t* job = nullptr;
      game::Call<void (*)(uint8_t*, ClientReader*, uint8_t**)>(0x14035cf50)(player(), &blob, &job);
      uint8_t* self = character();
      if (self) {
        if (void* window = game::Field<void*>(game, 0x38900)) game::Call<void (*)(void*, int)>(0x1409e4be0)(window, game::Field<int>(job, 4));
        int itemId = game::Field<int>(packet, 0x4C);
        uint8_t* item = game::Call<uint8_t* (*)(uint8_t*, int*, bool)>(0x1405040c0)(self, &itemId, true);
        alignas(16) uint8_t holder[0x20];
        auto* held = (*reinterpret_cast<uint8_t** (***)(uint8_t*, void*)>(self))[0x58 / 8](self, holder);
        uint8_t* equipped = *held;
        game::Call<void (*)(void*)>(0x1403a95c0)(holder);
        if (equipped && !(equipped[0x489] & 4)) item[0xE0] |= 8;
        item[0xE0] |= 0x80;
        (*reinterpret_cast<void (***)(uint8_t*)>(self))[0x498 / 8](self);
        game::Call<void (*)(uint8_t*, uint8_t*, uint8_t*)>(0x14053d9b0)(self, item, packet + 0x28);
        using AppearanceFn = void (*)(uint8_t*, uint8_t*, uint8_t*);
        if (game::Field<int>(self, 0x1AC8) != game::Field<int>(packet, 0x50)) {
          int model = game::Call<int (*)(void*, int)>(0x1416d35a0)(game::Field<void*>(game, 0x3D488), 0);
          (*reinterpret_cast<void (***)(uint8_t*, int, void*, void*)>(self))[0x678 / 8](self, model, nullptr, nullptr);
          (*reinterpret_cast<AppearanceFn**>(self))[0x4B0 / 8](self, packet + 0x58, packet + 0x70);
          int unused = 0;
          (*reinterpret_cast<void (***)(uint8_t*, int, int*)>(self))[0x3B0 / 8](self, game::Field<int>(packet, 0x50), &unused);
          soeutil::IString second{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          soeutil::StringAssign(&second, reinterpret_cast<const char*>(0x142046fcb));
          soeutil::IString first{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          soeutil::StringAssign(&first, reinterpret_cast<const char*>(0x142046fcb));
          int outA = 0, outB = 0;
          uint8_t* owner = player();
          using EquipFn = void (*)(uint8_t*, uint8_t*, uint8_t*, uint8_t*, soeutil::IString*, soeutil::IString*, int, int*, int*, int, int, bool);
          (*reinterpret_cast<EquipFn**>(self))[0x4A0 / 8](self, item, owner + 0xABE8, owner + 0xAC18, &first, &second, 0, &outB, &outA, model, 0,
                                                         false);
          first.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&first);
          second.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&second);
        }
        int level = 0;
        game::Call<void (*)(uint8_t*, int*)>(0x14062e9c0)(player(), &level);
        game::Call<void (*)(uint8_t*, int)>(0x1406383a0)(player(), game::Field<int>(job, 4));
        for (int offset : {0xAD70, 0xAD78}) {
          void* view = game::Field<void*>(player(), offset);
          (*reinterpret_cast<void (***)(void*)>(view))[0x88 / 8](view);
        }
        using SetLevelFn = void (*)(void*, int*);
        if (game::Field<int>(job, 0x30) != level) {
          int jobLevel = game::Field<int>(job, 0x30);
          uint8_t* actor = self + 0x20;
          (*reinterpret_cast<SetLevelFn**>(actor))[0x148 / 8](actor, &jobLevel);
          game::Call<void (*)(uint8_t*)>(0x14062bfa0)(player());
          game::Call<void (*)(uint8_t*)>(0x140630be0)(player());
          for (auto* entity = game::Call<uint8_t* (*)(void*)>(0x14071e7a0)(game::Field<void*>(game, 0x38860)); entity;
               entity = game::Call<uint8_t* (*)(void*, uint8_t*)>(0x14071e880)(game::Field<void*>(game, 0x38860), entity)) {
            int entityLevel = game::Field<int>(entity, 0x228);
            uint8_t* entityActor = entity + 0x20;
            (*reinterpret_cast<SetLevelFn**>(entityActor))[0x148 / 8](entityActor, &entityLevel);
          }
          game::Call<void (*)(void*)>(0x140656700)(*reinterpret_cast<void**>(0x142b19c60));
        }
        (*reinterpret_cast<void (***)(uint8_t*, int)>(self))[0x100 / 8](self, game::Field<int>(job, 4));
        (*reinterpret_cast<AppearanceFn**>(self))[0x4B0 / 8](self, packet + 0x58, packet + 0x70);
        if (void* nameplates = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19cc0), 0x20)) {
          alignas(16) uint8_t handle[16];
          uint8_t* source = self + 0x630;
          void* name = (*reinterpret_cast<void* (***)(void*, void*)>(source))[0x68 / 8](source, handle);
          game::Call<void (*)(void*, void*, int)>(0x14079eb10)(nameplates, name, 0x2A);
        }
        if ((game::Field<uint64_t>(self, 0x1AD8) >> 0x25) & 1) {
          game::Call<void (*)(uint8_t*)>(0x140520c80)(self);
          (*reinterpret_cast<void (***)(uint8_t*)>(self))[0x3F8 / 8](self);
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403adf20)(packet);
      break;
    }
    case 0x12: {
      Packet83 packet(0x1420653f8, 0x12, storage, 0x11);
      if (!read(packet, 0x140389aa0)) return false;
      game::Call<void (*)(uint8_t*, int)>(0x14062bbd0)(player(), packet.Get<int>(0x18));
      if (character()) {
        if (void* window = game::Field<void*>(game, 0x38900)) game::Call<void (*)(void*, int)>(0x1409e4be0)(window, 0);
      }
      for (int offset : {0xAD70, 0xAD78}) {
        void* view = game::Field<void*>(player(), offset);
        (*reinterpret_cast<void (***)(void*)>(view))[0x88 / 8](view);
      }
      break;
    }
    case 0xE: {
      Packet83 packet(0x142065400, 0xE, storage, 0x11);
      if (!read(packet, 0x140389730)) return false;
      if (uint8_t* self = player()) {
        void* result = game::Call<void* (*)(uint8_t*, int, uint64_t, int)>(0x140629770)(self, packet.Get<int>(0x18), packet.Get<uint64_t>(0x20),
                                                                                        packet.Get<int>(0x28));
        uint64_t id = game::Field<uint64_t>(player(), 0x18);
        game::Call<void (*)(void*, uint64_t*, void*)>(0x1409f92c0)(game::Field<void*>(game, 0x38B78), &id, result);
      }
      break;
    }
    case 0xF: {
      Packet83 packet(0x142065408, 0xF, storage, 0x11);
      if (!read(packet, 0x14038a050)) return false;
      if (uint8_t* self = player()) {
        uint64_t id = game::Field<uint64_t>(self, 0x18);
        game::Call<void (*)(void*, uint64_t*, int)>(0x140a04c90)(game::Field<void*>(game, 0x38B78), &id, packet.Get<int>(0x18));
        game::Call<void (*)(uint8_t*, int)>(0x140636610)(player(), packet.Get<int>(0x18));
      }
      break;
    }
    case 0x11: {
      Packet83 packet(0x142065410, 0x11, storage, 0x11);
      if (!read(packet, 0x140389810)) return false;
      game::Call<void (*)(uint8_t*, int)>(0x140638ae0)(player(), packet.Get<int>(0x18));
      break;
    }
    case 0x14: {  // ability unlocked notification ("AbilityReceived": title, ability name, +0x20)
      Packet83 packet(0x142065418, 0x14, storage, 0x11);
      std::memcpy(packet.At(0x18), reinterpret_cast<void*>(0x142072990), 16);
      if (!read(packet, 0x1403896c0)) return false;
      ScriptArgList args{reinterpret_cast<void**>(0x14206cc48), nullptr, 0, 0};
      void* strings = *reinterpret_cast<void**>(0x142b19798);
      soeutil::IString title{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      (*reinterpret_cast<void (***)(void*, const char*, soeutil::IString*)>(strings))[0x18 / 8](
          strings, reinterpret_cast<const char*>(0x14206eaa8), &title);  // "NotificationAbilityUnlocked"
      soeutil::IString ability{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      strings = *reinterpret_cast<void**>(0x142b19798);
      (*reinterpret_cast<void (***)(void*, int, soeutil::IString*)>(strings))[0x10 / 8](strings, packet.Get<int>(0x1C), &ability);
      using SlotFn = uint8_t* (*)(ScriptArgList*, int);
      game::Call<void (*)(uint8_t*, soeutil::IString*)>(0x14046d690)(game::Call<SlotFn>(0x1403b4810)(&args, 0), &title);
      game::Call<void (*)(uint8_t*, soeutil::IString*)>(0x14046d690)(game::Call<SlotFn>(0x1403b4810)(&args, 1), &ability);
      game::Call<void (*)(uint8_t*, int)>(0x14046d7b0)(game::Call<SlotFn>(0x1403b4810)(&args, 2), packet.Get<int>(0x20));
      game::Call<void (*)(void*, int, const char*, ScriptArgList*, bool, int, int)>(0x140a04370)(
          game::Field<void*>(game, 0x38B78), 2, reinterpret_cast<const char*>(0x14206eac8), &args, false, 0, 0);  // "AbilityReceived"
      ability.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&ability);
      title.vtable = soeutil::IStringVtable();
      soeutil::StringRelease(&title);
      game::Call<void (*)(ScriptArgList*)>(0x1403a08d0)(&args);
      break;
    }
    case 0x15: {  // text message for the game client (0x14042c1e0)
      Packet83 packet(0x142065420, 0x15, storage, 0x11);
      packet.Put<soeutil::IString>(0x18, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      bool ok = read(packet, 0x140389e90);
      if (ok) game::Call<void (*)(uint8_t*, const char*)>(0x14042c1e0)(game, packet.Get<const char*>(0x20));
      game::Call<void (*)(uint8_t*)>(0x1403ae270)(packet.At(0));
      if (!ok) return false;
      break;
    }
    case 0x19: {
      Packet83 packet(0x142065428, 0x19, storage, 0x11);
      if (!read(packet, 0x140389b40)) return false;
      if (packet.Get<uint8_t>(0x18)) game::Call<void (*)(void*)>(0x1407218c0)(game::Field<void*>(game, 0x38860));
      game[0x3883C] = 1;
      break;
    }
    case 0x35: {
      Packet83 packet(0x142065430, 0x35, storage, 0x11);
      if (!read(packet, 0x140389e10)) return false;
      game::Call<void (*)(void*)>(0x1407218c0)(game::Field<void*>(game, 0x38860));
      uint64_t now;
      game::Field<uint64_t>(game, 0x38840) = *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&now);
      break;
    }
    case 0x1A: {  // read only
      Packet83 packet(0x142065438, 0x1A, storage, 0x11);
      if (!read(packet, 0x14038a1a0)) return false;
      break;
    }
    case 0x1C: {  // id list {int id, bool flag} -> 0x1406297b0 (flag) / 0x140637490
      Packet83 packet(0x142065460, 0x1C, storage, 0x11);
      packet.Put<uint64_t>(0x18, 0x142065440);
      bool ok = read(packet, 0x140389f90);
      if (ok && player()) {
        for (int i = 0; i < packet.Get<int>(0x28); ++i) {
          uint8_t* entry = packet.Get<uint8_t*>(0x20) + i * 8;
          int id = game::Field<int>(entry, 0);
          if (entry[4])
            game::Call<void (*)(uint8_t*, int*)>(0x1406297b0)(player(), &id);
          else
            game::Call<void (*)(uint8_t*, int*)>(0x140637490)(player(), &id);
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae2d0)(packet.At(0));
      if (!ok) return false;
      break;
    }
    case 0x20:
    case 0x21:
    case 0x22: {  // character float value with an "apply now" flag (character not null-checked)
      struct FloatUpdate {
        uint64_t vtable, read, flagged, normal;
      };
      static constexpr FloatUpdate kUpdates[] = {{0x1420654a0, 0x14038d780, 0x1404fd230, 0x14052ef10},
                                                 {0x1420654a8, 0x14038d860, 0x1404fd750, 0x14052f640},
                                                 {0x1420654b0, 0x14038d7f0, 0x1404fd640, 0x14052f5e0}};
      const FloatUpdate& update = kUpdates[header.subType - 0x20];
      Packet83 packet(update.vtable, header.subType, storage, 0x11);
      if (!read(packet, update.read)) return header.subType != 0x20;  // 0x21/0x22 report handled anyway
      uint8_t* self = character();
      game::Call<void (*)(uint8_t*, float)>(packet.Get<uint8_t>(0x1C) ? update.flagged : update.normal)(self, packet.Get<float>(0x18));
      break;
    }
    case 0x24: {
      Packet83 packet(0x1420654b8, 0x24, storage, 0x11);
      packet.Put<int>(0x18, *reinterpret_cast<int*>(0x142b186ac));
      if (!read(packet, 0x140389d30)) return false;
      int id = packet.Get<int>(0x18);
      if (uint8_t* entity = game::Call<uint8_t* (*)(uint8_t*, int*)>(0x1403f8380)(game, &id)) entity[0x5B0] = packet.Get<uint8_t>(0x1C);
      break;
    }
    case 0x25:  // raw packet to the local player
      if (uint8_t* self = player()) game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x140631950)(self, data, length);
      break;
    case 0x26:  // raw packet to the state's +0x969C8 handler
      game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x1404a4d20)(game::Field<uint8_t*>(game, 0x314A8) + 0x969C8, data, length);
      break;
    case 0x29: {
      Packet83 packet(0x1420654c0, 0x29, storage, 0x11);
      packet.Put<uint64_t>(0x18, *reinterpret_cast<uint64_t*>(0x142b181f8));
      if (!read(packet, 0x14038d460)) return false;
      if (game::Call<bool (*)(uint8_t*, int)>(0x1405958b0)(player() + 0xD568, 8)) {  // player not null-checked
        game::Call<void (*)(void*, int)>(0x1404cf240)(*reinterpret_cast<void**>(0x142b19cd8), packet.Get<int>(0x20));
        uint64_t id = packet.Get<uint64_t>(0x18);
        game::Call<void (*)(void*, int, uint64_t*, void*)>(0x1404cf300)(*reinterpret_cast<void**>(0x142b19cd8), 3, &id, nullptr);
      }
      break;
    }
    case 0x2A: {  // loyalty points
      Packet83 packet(0x1420654c8, 0x2A, storage, 0x11);
      using LogFn = void (*)(void*, const char*, ...);
      if (!read(packet, 0x14038d160)) {
        game::Call<LogFn>(0x1402baba0)(*reinterpret_cast<void**>(0x142116320), reinterpret_cast<const char*>(0x14206eb10),
                                       packet.Get<int>(0x18));  // "ClientLoyaltyPointsUpdatePacket. FAILED to unserialize ..."
        return false;
      }
      int points = packet.Get<int>(0x18);
      if (uint8_t* self = player()) game::Call<void (*)(uint8_t*, int)>(0x140638a80)(self, points);
      game::Call<LogFn>(0x1402bab70)(*reinterpret_cast<void**>(0x142116320), reinterpret_cast<const char*>(0x14206ead8),
                                     points);  // "ClientLoyaltyPointsUpdatePacket. Loyalty Points: ..."
      break;
    }
    case 0x2E: {
      Packet83 packet(0x1420654e0, 0x2E, storage, 0x11);
      if (read(packet, 0x14038d990)) {
        if (uint8_t* self = player()) self[0x109D8] = 1;
      }
      break;
    }
    case 0x3A:
      game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x14040b510)(game, data, length);
      break;
    case 0x2B:
      if (player()) {
        uint64_t now;
        game::Call<void (*)(uint8_t*, uint64_t*)>(0x140638a60)(player(), game::Call<uint64_t* (*)(uint64_t*)>(0x14032fe90)(&now));
      }
      break;
    case 0x2C: {
      Packet83 packet(0x1420654d0, 0x2C, storage, 0x11);
      if (!read(packet, 0x14038d200)) return false;
      if (uint8_t* self = character()) game::Call<void (*)(uint8_t*, int, uint8_t)>(0x1404683b0)(self + 0x8C8, 0x30, packet.Get<uint8_t>(0x18));
      break;
    }
    case 0x2D: {  // audio event -> audio system
      Packet83 packet(0x1420654d8, 0x2D, storage, 0x11);
      packet.Put<int>(0x18, *reinterpret_cast<int*>(0x142b188f4));
      packet.Put<uint64_t>(0x1C, 2);
      if (!read(packet, 0x14038de30)) return false;
      game::Call<void (*)(void*, uint8_t*)>(0x1408200e0)(game::Field<void*>(game, 0x389E0), packet.At(0));
      break;
    }
    case 0x2F: {  // localized string id + text -> 0x14046d3c0
      Packet83 packet(0x1420654e8, 0x2F, storage, 0x11);
      packet.Put<soeutil::IString>(0x20, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      if (read(packet, 0x14038da10)) {
        soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
        void* strings = *reinterpret_cast<void**>(0x142b19798);
        (*reinterpret_cast<void (***)(void*, int, soeutil::IString*)>(strings))[0x10 / 8](strings, packet.Get<int>(0x18), &text);
        game::Call<void (*)(uint8_t*, const char*, const char*, int)>(0x14046d3c0)(game, packet.Get<const char*>(0x28), text.data, packet.Get<int>(0x1C));
        text.vtable = soeutil::IStringVtable();
        soeutil::StringRelease(&text);
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae490)(packet.At(0));
      break;
    }
    case 0x30:  // return to character select
      if (game::Field<void*>(game::Field<uint8_t*>(game, 0x38B80), 0x10) && *reinterpret_cast<void**>(0x142b19b98)) {
        game::Call<void (*)(uint8_t*, int)>(0x140474de0)(game, 0x1D);
      } else {
        game::Call<void (*)(void*, const char*, ...)>(0x1402baba0)(nullptr, reinterpret_cast<const char*>(0x14206eb60));  // "Could not return to character select, ..."
        *reinterpret_cast<int*>(0x142b176c4) = 5;
        game::Call<void (*)(uint8_t*, int)>(0x140474de0)(game, 0x23);
      }
      break;
    case 0x31: {  // 0x440-byte UI packet: feed one window, refresh two others
      uint8_t* packet = storage;
      game::Call<void (*)(uint8_t*)>(0x14039b600)(packet);
      if (game::Call<ReadFn>(0x14038d8d0)(packet, data, length, false)) {
        using FindFn = uint8_t* (*)(void*, void*);
        if (uint8_t* window = game::Call<FindFn>(0x1403591b0)(UiRoot(), *reinterpret_cast<void**>(0x142a35fe0)))
          game::Call<void (*)(uint8_t*, uint8_t*)>(0x140adb690)(window, packet);
        for (auto [find, key] : {std::pair<uint64_t, uint64_t>{0x140359220, 0x142a36d88}, {0x140359290, 0x142a36d90}}) {
          if (uint8_t* window = game::Call<FindFn>(find)(UiRoot(), *reinterpret_cast<void**>(key))) {
            uint8_t* view = window + 0x80;
            (*reinterpret_cast<void (***)(void*)>(view))[0x28 / 8](view);
          }
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae330)(packet);
      break;
    }
    case 0x32: {
      Packet83 packet(0x142065588, 0x32, storage, 0x11);
      packet.Put<soeutil::IString>(0x18, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      if (read(packet, 0x14038da80)) game::Call<void (*)(uint8_t*, const char*, void*, bool)>(0x14046f660)(game, packet.Get<const char*>(0x20), nullptr, true);
      game::Call<void (*)(uint8_t*)>(0x1403ae580)(packet.At(0));
      break;
    }
    case 0x34: {
      Packet83 packet(0x142065590, 0x34, storage, 0x11);
      if (read(packet, 0x14038d2a0)) game::Call<void (*)(uint8_t*, int, int)>(0x14046ad40)(game, packet.Get<int>(0x18), packet.Get<int>(0x1C));
      break;
    }
    case 0x36:
      game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x14030f236)(game, data, length);
      break;
    case 0x38: {  // server-driven console variables: value at var+0x68, 0x140cff570 notifies on change
      Packet83 packet(0x1420655a8, 0x38, storage, 0x11);
      std::memcpy(packet.At(0x30), reinterpret_cast<void*>(0x142b06ac0), 16);
      if (!read(packet, 0x14038d3f0)) break;
      auto setInt = [](uint64_t variable, int value) {
        int& current = *reinterpret_cast<int*>(variable + 0x68);
        if (current == value) return;
        current = value;
        game::Call<void (*)(void*)>(0x140cff570)(reinterpret_cast<void*>(variable));
      };
      auto setFloat = [](uint64_t variable, float value) {
        float& current = *reinterpret_cast<float*>(variable + 0x68);
        if (!(current < value || current > value)) return;  // ucomiss/je: equal or unordered keeps it
        current = value;
        game::Call<void (*)(void*)>(0x140cff570)(reinterpret_cast<void*>(variable));
      };
      setInt(0x142b17fa0, packet.Get<int>(0x18));
      setInt(0x142b17eb0, packet.Get<int>(0x1C));
      setInt(0x142b17700, packet.Get<int>(0x20));
      setInt(0x142b17db0, packet.Get<int>(0x24));
      setInt(0x142b18470, packet.Get<int>(0x28));
      setInt(0x142b18040, packet.Get<int>(0x2C));
      setFloat(0x142b17c80, packet.Get<float>(0x30));
      setFloat(0x142b18740, packet.Get<float>(0x38));
      setInt(0x142b17f20, packet.Get<int>(0x48));
      setInt(0x142b17910, packet.Get<uint8_t>(0x4C) >> 7);
      break;
    }
    case 0x4C: {  // game client vfunc 0xD8(true, id, two strings)
      Packet83 packet(0x142065638, 0x4C, storage, 0x11);
      packet.Put<soeutil::IString>(0x20, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      packet.Put<soeutil::IString>(0x38, soeutil::IString{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0});
      if (read(packet, 0x14038a240)) {
        (*reinterpret_cast<void (***)(uint8_t*, bool, int, const char*, const char*)>(game))[0xD8 / 8](
            game, true, packet.Get<int>(0x18), packet.Get<const char*>(0x28), packet.Get<const char*>(0x40));
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae4f0)(packet.At(0));
      break;
    }
    case 0x4E: {
      Packet83 packet(0x142065640, 0x4E, storage, 0x11);
      packet.Put<uint64_t>(0x18, 0x142063770);
      if (read(packet, 0x14038a390)) {
        if (void* panel = game::Field<void*>(*reinterpret_cast<uint8_t**>(0x142b19cc0), 0x190)) {
          game::Call<void (*)(void*, uint8_t)>(0x1407a9e80)(panel, packet.Get<uint8_t>(0x30));
          game::Call<void (*)(void*, uint8_t*)>(0x1407aa890)(panel, packet.At(0x18));
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae5f0)(packet.At(0));
      break;
    }
    case 0x4F: {  // localized text -> string console variable 0x142b18170 (value IString at +0x68)
      Packet83 packet(0x142065648, 0x4F, storage, 0x11);
      packet.Put<uint64_t>(0x18, 0x14204aea0);  // StringFixed<512>
      packet.Put<char*>(0x20, soeutil::EmptyStringData());
      if (read(packet, 0x14038a660)) {
        auto setVariable = [](const char* value) {
          soeutil::IString text{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          soeutil::StringAssign(&text, value);
          auto* current = reinterpret_cast<soeutil::IString*>(0x142b181d8);
          if (!(static_cast<int>(*reinterpret_cast<uint64_t*>(0x142b181e8)) == text.length &&
                std::memcmp(current->data, text.data, text.length) == 0)) {
            game::Call<void (*)(soeutil::IString*, soeutil::IString*)>(0x1402bd560)(current, &text);
            game::Call<void (*)(void*)>(0x140cff570)(reinterpret_cast<void*>(0x142b18170));
          }
          text.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&text);
        };
        if (packet.Get<int>(0x28) > 0) {
          soeutil::IString localized{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
          void* strings = *reinterpret_cast<void**>(0x142b19798);
          (*reinterpret_cast<void (***)(void*, const char*, soeutil::IString*)>(strings))[0x18 / 8](strings, packet.Get<const char*>(0x20), &localized);
          setVariable(localized.data);
          localized.vtable = soeutil::IStringVtable();
          soeutil::StringRelease(&localized);
        } else {
          setVariable(reinterpret_cast<const char*>(0x142046fcb));
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403ae6b0)(packet.At(0));
      break;
    }
    case 0x4B: {  // player value update by kind (+0x18) for our id; kind 1 also updates the group
      Packet83 packet(0x142065630, 0x4B, storage, 0x11);
      packet.Put<uint64_t>(0x20, *reinterpret_cast<uint64_t*>(0x142b181f8));
      if (!read(packet, 0x140389650)) return true;
      uint8_t* self = player();
      uint64_t selfId = game::Field<uint64_t>(self, 0x18);  // read before the null check, as in the original
      uint64_t id = packet.Get<uint64_t>(0x20);
      int value = packet.Get<int>(0x1C);
      static constexpr uint64_t kSetters[] = {0x140638b10, 0x140638b00, 0x140638b30, 0x140638b40, 0x140638b20};
      int kind = packet.Get<int>(0x18);
      if (kind < 0 || kind > 4) return true;
      if (selfId == id && self) game::Call<void (*)(uint8_t*, int)>(kSetters[kind])(self, value);
      if (kind == 1 && game::Call<bool (*)(uint8_t*)>(0x1405f7000)(player() + 0x9418)) {
        uint64_t member = id;
        game::Call<void (*)(uint8_t*, uint64_t*, int)>(0x1405fb630)(player() + 0x9418, &member, value);
      }
      return true;
    }
    case 0xC:
    case 0x10:
    case 0x17:
    case 0x18:
    case 0x1B:
      break;
    default:
      return false;
  }
  return true;  // handled (failed reads return false above)
}

// Small game-client vtable slots that only forward to another function.
// 0x14047bde0 (slot 1): log through slot 91 (vfunc 0x2D8).
void GameClientLogForward(uint8_t* game, int level, const char* channel, const char* format, void* args) {
  (*reinterpret_cast<void (***)(uint8_t*, int, const char*, const char*, void*)>(game))[0x2D8 / 8](game, level, channel, format, args);
}
// 0x1404742f0 (slot 35): update the state's +0x96A18 object (0x14166a8a0).
void GameClientUpdateState96A18(uint8_t* game) {
  game::Call<void (*)(uint8_t*)>(0x14166a8a0)(game::Field<uint8_t*>(game, 0x314A8) + 0x96A18);
}
// 0x140474570 (slot 44): hand a message to the +0x388C8 handler (0x1409e0740).
void GameClientPostToHandler388C8(uint8_t* game, void* message) {
  game::Call<void (*)(void*, void*)>(0x1409e0740)(game::Field<void*>(game, 0x388C8), message);
}
// 0x140428d40 (slot 52): log a message as "%s".
void GameClientLogText(uint8_t* /*game*/, const char* text) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(nullptr, reinterpret_cast<const char*>(0x142046fb8), text);  // "%s"
}
// 0x1403f5fd0 (slot 59): one of two constants depending on the game mode at +0x3899C.
float GameClientModeConstant(uint8_t* game) {
  return game::Field<int>(game, 0x3899C) == 4 ? *reinterpret_cast<float*>(0x1420728c4) : *reinterpret_cast<float*>(0x1420728c8);
}
// 0x1403f9ff0 (slot 82): format the window title "H1Z1 v%s Live".
void GameClientFormatTitle(uint8_t* /*game*/, soeutil::IString* out) {
  game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x14206cf50),
                                                                          *reinterpret_cast<const char**>(0x1429fbd88));
}
// 0x14046da00 (slot 83): set the main window's title (SetWindowTextA via its IAT slot).
BOOL GameClientSetWindowTitle(uint8_t* game, soeutil::IString* title) {
  auto setWindowText = *reinterpret_cast<BOOL(WINAPI**)(HWND, const char*)>(0x1440a0468);
  return setWindowText(game::Field<HWND>(game, 0x387F0), title->data);
}
// 0x1404081d0 (slot 88): vfunc 0x20 of the +0x388C8 handler (arguments passed through).
uint64_t GameClientHandler388C8Vfunc20(uint8_t* game, uint64_t a, uint64_t b, uint64_t c) {
  void* handler = game::Field<void*>(game, 0x388C8);
  return (*reinterpret_cast<uint64_t (***)(void*, uint64_t, uint64_t, uint64_t)>(handler))[0x20 / 8](handler, a, b, c);
}
// 0x140408970 / 0x14040bb80 (slots 95, 96): raw packets for the local
// player's group (player +0x9418, handlers 0x1405f2070 / 0x1405f5830).
void GameClientGroupPacketA(uint8_t* game, const uint8_t* data, int length) {
  game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x1405f2070)(
      game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80) + 0x9418, data, length);
}
void GameClientGroupPacketB(uint8_t* game, const uint8_t* data, int length) {
  game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x1405f5830)(
      game::Field<uint8_t*>(game::Field<uint8_t*>(game, 0x314A8), 0xF80) + 0x9418, data, length);
}
// 0x1403b5120 (slot 116): adjustor thunk to the scalar deleting destructor (0x1403c1290).
void* GameClientDeletingDestructorThunk(uint8_t* self, unsigned flags) {
  return game::Call<void* (*)(uint8_t*, unsigned)>(0x1403c1290)(self - 8, flags);
}
// 0x14042fb50 (slot 118): recorder state 5 (0x14063d8e0).
void GameClientRecorderState5(uint8_t* /*game*/) {
  game::Call<void (*)(void*, int)>(0x14063d8e0)(*reinterpret_cast<void**>(0x142b19b98), 5);
}
// 0x1402f44d0 (slot 120): delete an object through its deleting destructor.
void DeleteThroughVtable(void* object) {
  if (object) (*reinterpret_cast<void (***)(void*, int)>(object))[0](object, 1);
}

REBUILD_FUNCTION(GameClient_HandleZonePacket, 0x140430a20, GameClientHandleZonePacket);
REBUILD_FUNCTION(GameClient_OnZoneConnected, 0x140430490, GameClientOnZoneConnected);
REBUILD_FUNCTION(GameClient_DeletingDestructor, 0x1403c1290, GameClientDeletingDestructor);
REBUILD_FUNCTION(GameClient_OnCloseButton, 0x14045a4f0, GameClientOnCloseButton);
REBUILD_FUNCTION(GameClient_Slot26, 0x140472d40, GameClientSlot26);
REBUILD_FUNCTION(GameClient_Slot54, 0x1402ecb90, GameClientSlot54);
REBUILD_FUNCTION(GameClient_Slot62, 0x1403fd2f0, GameClientSlot62);
REBUILD_FUNCTION(GameClient_Slot122, 0x141669a00, GameClientSlot122);
REBUILD_FUNCTION(GameClient_Slot123, 0x141669bb0, GameClientSlot123);
REBUILD_FUNCTION(GameClient_Slot124, 0x141669b80, GameClientSlot124);
REBUILD_FUNCTION(GameClient_Slot33, 0x14040e9f0, GameClientSlot33);
REBUILD_FUNCTION(GameClient_Slot25, 0x140410a00, GameClientSlot25);
REBUILD_FUNCTION(GameClient_Slot47, 0x1404690c0, GameClientSlot47);
REBUILD_FUNCTION(GameClient_Slot79, 0x1403e6aa0, GameClientSlot79);
REBUILD_FUNCTION(GameClient_Slot61, 0x1403fd270, GameClientSlot61);
REBUILD_FUNCTION(GameClient_Slot51, 0x140467880, GameClientSlot51);
REBUILD_FUNCTION(GameClient_Slot84, 0x140433600, GameClientSlot84);
REBUILD_FUNCTION(GameClient_Slot119, 0x1403c1020, RefHolderDeletingDestructor);
REBUILD_FUNCTION(GameClient_Shutdown, 0x140473f60, GameClientShutdown);
REBUILD_FUNCTION(GameClient_CreateZoneClient, 0x1403dd800, GameClientCreateZoneClient);
REBUILD_FUNCTION(GameClient_SendMountRequest, 0x1404677e0, GameClientSendMountRequest);
REBUILD_FUNCTION(AssetHandler_Update, 0x1403e9c70, AssetHandlerUpdate);
REBUILD_FUNCTION(GameClient_SetLocale, 0x140410940, GameClientSetLocale);
REBUILD_FUNCTION(GameClient_Slot13, 0x14034e6f0, GameClientSlot13);
REBUILD_FUNCTION(GameClient_Slot72, 0x1404089c0, GameClientSlot72);
REBUILD_FUNCTION(GameClient_Slot71, 0x140408a90, GameClientSlot71);
REBUILD_FUNCTION(GameClient_Slot87, 0x1403dc8c0, GameClientSlot87);
REBUILD_FUNCTION(GameClient_SetLoginInfo, 0x14040ec00, GameClientSetLoginInfo);
REBUILD_FUNCTION(GameClient_Slot45, 0x14046ceb0, GameClientSlot45);
REBUILD_FUNCTION(GameClient_Slot46, 0x14046c250, GameClientSlot46);
REBUILD_FUNCTION(GameClient_CheckIdle, 0x1403d33f0, GameClientCheckIdle);
REBUILD_FUNCTION(GameClient_OnInit, 0x1403fd180, GameClientOnInit);
REBUILD_FUNCTION(GameClient_HandleEvent, 0x140408340, GameClientHandleEvent);
REBUILD_FUNCTION(GameClient_HandlePacket93, 0x14040b790, GameClientHandlePacket93);
REBUILD_FUNCTION(GameClient_ShutdownUi, 0x140470b70, GameClientShutdownUi);
REBUILD_FUNCTION(GameClient_HandleInput, 0x14043d480, GameClientHandleInput);
REBUILD_FUNCTION(GameClient_Slot74, 0x14040b600, GameClientSlot74);
REBUILD_FUNCTION(GameClient_HandlePacket6F, 0x140408b70, GameClientHandlePacket6F);
REBUILD_FUNCTION(GameClient_HandlePacket42, 0x140409d50, GameClientHandlePacket42);
REBUILD_FUNCTION(GameClient_HandlePacketE2, 0x14040bee0, GameClientHandlePacketE2);
REBUILD_FUNCTION(GameClient_Log, 0x1404307d0, GameClientLog);
REBUILD_FUNCTION(GameClient_LoadOptions, 0x14040e7a0, GameClientLoadOptions);
REBUILD_FUNCTION(GameClient_StartLogging, 0x1404106d0, GameClientStartLogging);
REBUILD_FUNCTION(GameClient_WaitForCharacterLogin, 0x1403d64a0, GameClientWaitForCharacterLogin);
REBUILD_FUNCTION(GameClient_ForwardToHandler388C8, 0x14043b860, GameClientForwardToHandler388C8);
REBUILD_FUNCTION(GameClient_HandlePacket17, 0x14040bba0, GameClientHandlePacket17);
REBUILD_FUNCTION(GameClient_SetChatText, 0x140468e00, GameClientSetChatText);
REBUILD_FUNCTION(GameClient_DisconnectFromServer, 0x1403e7a50, GameClientDisconnectFromServer);
REBUILD_FUNCTION(GameClient_RefreshJobBrowser, 0x14046fa20, GameClientRefreshJobBrowser);
REBUILD_FUNCTION(GameClient_ConnectToGateway, 0x14046e660, GameClientConnectToGateway);
REBUILD_FUNCTION(GameClient_OnLoginFailed, 0x14042c3f0, GameClientOnLoginFailed);
REBUILD_FUNCTION(GameClient_HandlePacket41, 0x140409ee0, GameClientHandlePacket41);
REBUILD_FUNCTION(GameClient_PresentJob, 0x140431ca0, GameClientPresentJob);
REBUILD_FUNCTION(GameClient_Initialize, 0x140432650, GameClientInitialize);
REBUILD_FUNCTION(GameClient_HandlePacket83, 0x14040cd70, GameClientHandlePacket83);
REBUILD_FUNCTION(GameClient_Init, 0x14040ed60, GameClientInit);
REBUILD_FUNCTION(GameClient_DrawStatusOverlay, 0x1403e7ea0, GameClientDrawStatusOverlay);
REBUILD_FUNCTION(GameClient_CreateAssetSystem, 0x1403db810, GameClientCreateAssetSystem);
REBUILD_FUNCTION(GameClient_Update, 0x14043c0e0, GameClientUpdate);
REBUILD_FUNCTION(GameClient_ShutdownSystems, 0x1403e57f0, GameClientShutdownSystems);
REBUILD_FUNCTION(GameClient_ShutdownGame, 0x1403e42c0, GameClientShutdownGame);
REBUILD_FUNCTION(GameClient_GiveTime, 0x1403fa350, GameClientGiveTime);
REBUILD_FUNCTION(GameClient_CreateAppServices, 0x1403d8a00, GameClientCreateAppServices);
REBUILD_FUNCTION(GameClient_WriteCrashInfo, 0x14042c8a0, GameClientWriteCrashInfo);
REBUILD_FUNCTION(GameClient_HandleInputActions, 0x140433680, GameClientHandleInputActions);
REBUILD_FUNCTION(GameClient_HandlePacket11, 0x140404980, GameClientHandlePacket11);
REBUILD_FUNCTION(GameClient_LogForward, 0x14047bde0, GameClientLogForward);
REBUILD_FUNCTION(GameClient_UpdateState96A18, 0x1404742f0, GameClientUpdateState96A18);
REBUILD_FUNCTION(GameClient_PostToHandler388C8, 0x140474570, GameClientPostToHandler388C8);
REBUILD_FUNCTION(GameClient_LogText, 0x140428d40, GameClientLogText);
REBUILD_FUNCTION(GameClient_ModeConstant, 0x1403f5fd0, GameClientModeConstant);
REBUILD_FUNCTION(GameClient_FormatTitle, 0x1403f9ff0, GameClientFormatTitle);
REBUILD_FUNCTION(GameClient_SetWindowTitle, 0x14046da00, GameClientSetWindowTitle);
REBUILD_FUNCTION(GameClient_Handler388C8Vfunc20, 0x1404081d0, GameClientHandler388C8Vfunc20);
REBUILD_FUNCTION(GameClient_GroupPacketA, 0x140408970, GameClientGroupPacketA);
REBUILD_FUNCTION(GameClient_GroupPacketB, 0x14040bb80, GameClientGroupPacketB);
REBUILD_FUNCTION(GameClient_DeletingDestructorThunk, 0x1403b5120, GameClientDeletingDestructorThunk);
REBUILD_FUNCTION(GameClient_RecorderState5, 0x14042fb50, GameClientRecorderState5);
REBUILD_FUNCTION(DeleteThroughVtable, 0x1402f44d0, DeleteThroughVtable);
REBUILD_FUNCTION(GameClient_Slot5, 0x1403f51c0, GameClientSlot5);
REBUILD_FUNCTION(GameClient_Slot6, 0x1403f5270, GameClientSlot6);
REBUILD_FUNCTION(GameClient_Slot7, 0x1403f5320, GameClientSlot7);
REBUILD_FUNCTION(GameClient_Slot8, 0x1403f54a0, GameClientSlot8);
REBUILD_FUNCTION(GameClient_Slot15, 0x1402ecd00, GameClientSlot15);
REBUILD_FUNCTION(GameClient_Slot16, 0x1402ecd60, GameClientSlot16);
REBUILD_FUNCTION(GameClient_Slot22, 0x1402ecce0, GameClientSlot22);
REBUILD_FUNCTION(GameClient_Slot23, 0x1402eccd0, GameClientSlot23);
REBUILD_FUNCTION(PureVirtualCrash, 0x140309030, GameClientPureVirtual);
REBUILD_FUNCTION(GameClient_Slot53, 0x1402ece90, GameClientSlot53);
REBUILD_FUNCTION_TOO_SMALL(GameClient_Slot14, 0x14046ea60, GameClientSlot14);
REBUILD_FUNCTION(GameClient_HandleChannel2Packet, 0x14040b750, GameClientHandleChannel2Packet);

}  // namespace rebuild::game_net
