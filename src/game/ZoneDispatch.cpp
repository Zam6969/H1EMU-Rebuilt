// The zone opcode dispatcher (0x1403fe210, ~16 KB, ~175 cases): routes each
// zone packet to the game subsystem that owns it. Rebuilt opcode by opcode -
// cases not rebuilt yet are handed to the original function through its
// trampoline, which runs its own copy of the common exit.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::game_net {
namespace {

using DispatchFn = bool (*)(uint8_t*, uint8_t*, const uint8_t*, int, int);
DispatchFn g_originalDispatch = nullptr;

constexpr size_t kClientState = 0x314A8;
constexpr size_t kLocalPlayer = 0xF80;  // in the client state block

// Game client members holding the subsystem objects packets are routed to.
void* Member(uint8_t* game, size_t index) { return game::Field<void*>(game, index * 8); }

void* GlobalObject(uintptr_t address) { return *reinterpret_cast<void**>(address); }

using RouteFn = void (*)(void*, const uint8_t*, int);
void Route(uintptr_t fn, void* object, const uint8_t* data, int length) {
  game::Call<RouteFn>(fn)(object, data, length);
}

template <typename Ret>
Ret VirtualRoute(void* object, size_t slot, const uint8_t* data, int length) {
  using Fn = Ret (*)(void*, const uint8_t*, int);
  return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object, data, length);
}

// Common exit of every case: record the packet against the channel's slot.
bool Finish(uint8_t* state, int channel, const uint8_t* data, int length, bool result) {
  uint8_t* record = state + (static_cast<int64_t>(channel) * 3 + 0x25B62) * 4;
  game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x1404685a0)(record, data, length);
  return result;
}

}  // namespace

// 0x1403fe210: DispatchZonePacket(header, data, length, channel)
bool GameClientDispatchZonePacket(uint8_t* game, uint8_t* header, const uint8_t* data, int length, int channel) {
  uint8_t* state = game::Field<uint8_t*>(game, kClientState);
  uint8_t* player = game::Field<uint8_t*>(state, kLocalPlayer);
  int opcode = *reinterpret_cast<int*>(header + 8);
  bool result = true;
  if (static_cast<unsigned>(opcode - 3) > 0xF5) {
    // Outside the built-in opcode range: offered to the extension handler.
    void* extension = GlobalObject(0x142b19cc0);
    using ExtensionFn = bool (*)(void*, uint8_t*, const uint8_t*, int);
    result = extension && game::Call<ExtensionFn>(0x1407b28b0)(extension, header, data, length);
    return Finish(state, channel, data, length, result);
  }
  switch (opcode) {
    case 0x0C:
      result = VirtualRoute<bool>(Member(game, 0x711F), 1, data, length);
      break;
    case 0x0E: Route(0x1409abb90, Member(game, 0x711A), data, length); break;
    case 0x0F: Route(0x140402bd0, game, data, length); break;
    case 0x10: VirtualRoute<void>(Member(game, 0x712E), 3, data, length); break;
    case 0x11: VirtualRoute<void>(game, 0x2E8 / 8, data, length); break;
    case 0x13: VirtualRoute<void>(game, 0x2F8 / 8, data, length); break;
    case 0x14: VirtualRoute<void>(game, 0x310 / 8, data, length); break;
    case 0x17: result = VirtualRoute<bool>(game, 0x318 / 8, data, length); break;
    case 0x1A: Route(0x1409fe5f0, Member(game, 0x716F), data, length); break;
    case 0x1B: Route(0x1406318f0, player, data, length); break;
    case 0x1C: Route(0x1407e29d0, Member(game, 0x713E), data, length); break;
    case 0x1D: Route(0x1407d8b00, GlobalObject(0x142b19a30), data, length); break;
    case 0x20: Route(0x14093aa20, Member(game, 0x7127), data, length); break;
    case 0x22:
      if (player) Route(0x140631940, player, data, length);
      break;
    case 0x26:
      if (player) Route(0x140631910, player, data, length);
      result = false;
      break;
    case 0x27: Route(0x1408223d0, Member(game, 0x713C), data, length); break;
    case 0x28: Route(0x140944840, Member(game, 0x712B), data, length); break;
    case 0x2A: Route(0x140645320, Member(game, 0x712A), data, length); break;
    case 0x2B:
      if (player) Route(0x14062f750, player, data, length);
      break;
    case 0xF1:
      Route(0x1406028b0, static_cast<uint8_t*>(GlobalObject(0x142b19ba0)) + 0xF9C0, data, length);
      result = false;
      break;
    case 0xF2:
      game::Call<void (*)(const uint8_t*, int)>(0x1404ea450)(data, length);
      result = false;
      break;
    case 0xF3:
      if (player) Route(0x140606ec0, player + 0x10950, data, length);
      break;
    case 0xF4: Route(0x140af0170, Member(game, 0x85C9), data, length); break;
    case 0xF6:
      if (auto* local = static_cast<uint8_t*>(GlobalObject(0x142b19ba0))) Route(0x14061d6b0, local + 0x10A08, data, length);
      result = false;
      break;
    case 0xF7:
      Route(0x14040b1e0, game, data, length);
      result = false;
      break;
    case 0xF8:
      if (player) Route(0x140576e00, player + 0xCA28, data, length);
      break;
    default:
      return g_originalDispatch(game, header, data, length, channel);  // not rebuilt yet
  }
  return Finish(state, channel, data, length, result);
}

REBUILD_FUNCTION_WITH_ORIGINAL(GameClient_DispatchZonePacket, 0x1403fe210, GameClientDispatchZonePacket,
                               g_originalDispatch);

}  // namespace rebuild::game_net
