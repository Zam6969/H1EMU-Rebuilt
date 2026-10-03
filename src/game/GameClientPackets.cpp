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
