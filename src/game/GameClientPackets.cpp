// The game client (singleton at 0x142b19780, vtable 0x1420646e0): entry
// point for zone packets coming out of the gateway tunnel. Packets are
// sequenced and timestamped; each is either dispatched now (0x1403fe210 - the
// zone opcode dispatcher) or queued in a time-ordered delay list.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "core/game.h"
#include "core/hook.h"
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
