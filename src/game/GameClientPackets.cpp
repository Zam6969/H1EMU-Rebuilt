// The game client (singleton at 0x142b19780, vtable 0x1420646e0): entry
// point for zone packets coming out of the gateway tunnel. Packets are
// sequenced and timestamped; each is either dispatched now (0x1403fe210 - the
// zone opcode dispatcher) or queued in a time-ordered delay list.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
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
  Packet83(uint64_t vtable, int subType, uint8_t* storage) : bytes_(storage) {
    std::memset(bytes_, 0, kPacket83Bytes);
    Put<uint64_t>(0, vtable);
    Put<uint64_t>(8, 0x83);
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
