// The game client (singleton at 0x142b19780, vtable 0x1420646e0): entry
// point for zone packets coming out of the gateway tunnel. Packets are
// sequenced and timestamped; each is either dispatched now (0x1403fe210 - the
// zone opcode dispatcher) or queued in a time-ordered delay list.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"

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

REBUILD_FUNCTION(GameClient_HandleZonePacket, 0x140430a20, GameClientHandleZonePacket);

}  // namespace rebuild::game_net
