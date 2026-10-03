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

bool RouteResult(uintptr_t fn, void* object, const uint8_t* data, int length) {
  return game::Call<bool (*)(void*, const uint8_t*, int)>(fn)(object, data, length);
}

template <typename Ret>
Ret VirtualRoute(void* object, size_t slot, const uint8_t* data, int length) {
  using Fn = Ret (*)(void*, const uint8_t*, int);
  return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot])(object, data, length);
}

// Run a one-time initializer guarded by a static flag byte.
void LazyInit(uintptr_t flag, uintptr_t init) {
  auto* done = reinterpret_cast<bool*>(flag);
  if (!*done) {
    game::Call<void (*)()>(init)();
    *done = true;
  }
}

// Stack packet with one int field (opcode 0x32).
struct ValuePacket {
  void** vtable;
  int opcode;
  int padding;
  int value;
  int padding2;
};
static_assert(offsetof(ValuePacket, value) == 0x10);

// Stack packet with one IString (e.g. KickedFromServer).
struct PacketString {
  void** vtable;
  char* data;
  int length;
  int capacity;
};
struct StringPacket {
  void** vtable;
  int opcode;
  int padding;
  PacketString text;
};
static_assert(offsetof(StringPacket, text) == 0x10 && sizeof(StringPacket) == 0x28);

struct ValueFlagPacket {
  void** vtable;
  int opcode;
  int padding;
  int value;
  bool flag;
  uint8_t reserved[0x43];  // slack: the packet's full size is not pinned down yet
};
static_assert(offsetof(ValueFlagPacket, flag) == 0x14);

struct U64Packet {
  void** vtable;
  uint64_t opcode;  // written as a qword
  uint64_t value;
};
static_assert(offsetof(U64Packet, value) == 0x10);

struct BytesPacket {
  void** vtable;
  int opcode;
  int padding;
  const uint8_t* bytes;
  int size;
  int padding2;
};
static_assert(offsetof(BytesPacket, size) == 0x18);

struct ValueFloatPacket {
  void** vtable;
  int opcode;
  int padding;
  int value;
  float amount;
};
static_assert(offsetof(ValueFloatPacket, amount) == 0x14);

struct FlagPacket {
  void** vtable;
  int opcode;
  int padding;
  bool flag;
};
static_assert(offsetof(FlagPacket, flag) == 0x10);

// Bounds-checked read cursor (same layout as the zone client's).
struct PacketReader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(PacketReader, failed) == 0x20);

constexpr size_t kPacket25Size = 0x40;  // ctor 0x1416ffb60 initialises up to +0x3C

struct Packet97 {
  void** vtable;
  int opcode;
  int padding;
  void** bodyVtable;  // +0x10, dtor 0x1404539a0
  uint8_t body[0x10];
  int value;
  int padding2;
};
static_assert(offsetof(Packet97, value) == 0x28 && sizeof(Packet97) == 0x30);

struct Packet3Strings {
  void** vtable;
  int opcode;
  int padding;
  PacketString text[3];  // +0x10, dtor 0x1403ade00
  int value;
};
static_assert(offsetof(Packet3Strings, value) == 0x58);

constexpr size_t kPacket33Size = 0x450;  // two StringFixed<512> at +0x10 / +0x230
// Packets whose nested constructors are not sized yet get this much room
// (over-allocating stack is harmless; under-allocating is not).
// One buffer shared by every case, so the frame stays small next to the
// original's 0x52E98-byte one.
constexpr size_t kOpaquePacketSize = 0x4000;

constexpr size_t kOpcode3DString = 0x38C90;   // IString (meaning not identified yet)
constexpr size_t kKickReason = 0x3D520;       // IString
constexpr size_t kOpcode69Value = 0x38DB0;    // int; >= 0x12 sets a flag on the extension object's +0x28 child
constexpr size_t kLoginFailed = 0x38838;      // bool
constexpr size_t kInitialDataDone = 0x3883B;  // bool, set by ZoneDoneSendingInitialData
constexpr size_t kOpcode32Value = 0x38BEC;    // int (meaning not identified yet)

// Common exit of every case: record the packet against the channel's slot.
bool Finish(uint8_t* state, int channel, const uint8_t* data, int length, bool result) {
  uint8_t* record = state + (static_cast<int64_t>(channel) * 3 + 0x25B62) * 4;
  game::Call<void (*)(uint8_t*, const uint8_t*, int)>(0x1404685a0)(record, data, length);
  return result;
}

// Opcodes the built-in table has no case for go to the extension handler.
bool OfferToExtension(uint8_t* state, uint8_t* header, const uint8_t* data, int length, int channel) {
  void* extension = GlobalObject(0x142b19cc0);
  using ExtensionFn = bool (*)(void*, uint8_t*, const uint8_t*, int);
  bool result = extension && game::Call<ExtensionFn>(0x1407b28b0)(extension, header, data, length);
  return Finish(state, channel, data, length, result);
}

// The reader-based packet pattern: construct, read through a PacketReader,
// run the handler only on a clean read with no trailing bytes, destroy.
void ReadAndHandle(uint8_t* game, uint8_t* packet, uintptr_t ctor, uintptr_t read, uintptr_t handler, uintptr_t dtor,
                   size_t dtorOffset, const uint8_t* data, int length) {
  game::Call<void (*)(uint8_t*)>(ctor)(packet);
  if (data) {
    PacketReader reader{data, length, data, data + length, 0};
    game::Call<void (*)(uint8_t*, PacketReader*)>(read)(packet, &reader);
    if (!static_cast<uint8_t>(reader.failed) && static_cast<int>(reader.end - reader.cursor) <= 0)
      game::Call<void (*)(uint8_t*, uint8_t*)>(handler)(game, packet);
  }
  game::Call<void (*)(uint8_t*)>(dtor)(packet + dtorOffset);
}

}  // namespace

// 0x1403fe210: DispatchZonePacket(header, data, length, channel)
bool GameClientDispatchZonePacket(uint8_t* game, uint8_t* header, const uint8_t* data, int length, int channel) {
  uint8_t* state = game::Field<uint8_t*>(game, kClientState);
  uint8_t* player = game::Field<uint8_t*>(state, kLocalPlayer);
  int opcode = *reinterpret_cast<int*>(header + 8);
  bool result = true;
  alignas(16) uint8_t opaque[kOpaquePacketSize];
  if (static_cast<unsigned>(opcode - 3) > 0xF5) return OfferToExtension(state, header, data, length, channel);
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
    case 0x06:
      Route(0x1409a2ad0, Member(game, 0x7117), data, length);
      break;
    case 0x09:
      VirtualRoute<void>(game, 88, data, length);
      break;
    case 0x2D:
      if (!player) break;
      Route(0x140630ca0, player, data, length);
      break;
    case 0x2E:
      Route(0x140402630, game, data, length);
      result = false;
      break;
    case 0x38:
      Route(0x14040c0f0, game, data, length);
      break;
    case 0x39:
      Route(0x1404709a0, game, data, length);
      break;
    case 0x41:
      VirtualRoute<void>(game, 97, data, length);
      break;
    case 0x42:
      VirtualRoute<void>(game, 100, data, length);
      break;
    case 0x46:
      if (!player) break;
      Route(0x140630c80, player, data, length);
      break;
    case 0x47:
      Route(0x1407d45b0, Member(game, 0x713B), data, length);
      break;
    case 0x49:
      if (!player) break;
      Route(0x140630d50, player, data, length);
      break;
    case 0x4B:
      Route(0x140640db0, Member(game, 0x7129), data, length);
      break;
    case 0x59: case 0x5A:
      if (!player) { result = false; break; }
      Route(0x140630d40, player, data, length);
      result = false;
      break;
    case 0x5E:
      VirtualRoute<void>(Member(game, 0x7119), 6, data, length);
      result = false;
      break;
    case 0x60:
      Route(0x1407e1760, Member(game, 0x7140), data, length);
      break;
    case 0x66:
      if (!player) { result = false; break; }
      Route(0x1404cbaa0, player + 0x5658, data, length);
      result = false;
      break;
    case 0x67:
      if (!player) break;
      Route(0x1404d0720, player + 0x97E8, data, length);
      break;
    case 0x68:
      Route(0x140ae8fd0, GlobalObject(0x142b19c68), data, length);
      result = false;
      break;
    case 0x6A:
      if (!player) break;
      Route(0x1407da3c0, game::Field<void*>(player, 0x99E8), data, length);
      break;
    case 0x6E:
      if (!player) break;
      Route(0x140630ae0, player, data, length);
      break;
    case 0x6F:
      VirtualRoute<void>(game, 102, data, length);
      break;
    case 0x71:
      if (!player) break;
      Route(0x1406318b0, player, data, length);
      break;
    case 0x73:
      Route(0x1404045d0, game, data, length);
      break;
    case 0x7B:
      if (!Member(game, 0x7700)) { result = false; break; }
      result = RouteResult(0x1407cf0f0, Member(game, 0x7700), data, length);
      break;
    case 0x7E:
      result = RouteResult(0x1407e6e60, Member(game, 0x76E1), data, length);
      break;
    case 0x80:
      if (!Member(game, 0x7128)) break;
      Route(0x14093bdc0, Member(game, 0x7128), data, length);
      break;
    case 0x81:
      VirtualRoute<void>(game, 96, data, length);
      break;
    case 0x82:
      Route(0x140631b20, player, data, length);
      break;
    case 0x83:
      VirtualRoute<void>(game, 101, data, length);
      break;
    case 0x85:
      Route(0x1406545c0, GlobalObject(0x142b19c60), data, length);
      break;
    case 0x86:
      Route(0x14040c7e0, game, data, length);
      break;
    case 0x87:
      Route(0x140409d20, game, data, length);
      break;
    case 0x88:
      if (!player) break;
      Route(0x14059b070, player + 0xDD70, data, length);
      break;
    case 0x89:
      Route(0x14040cd20, game, data, length);
      break;
    case 0x8A:
      if (!player) break;
      Route(0x1405a0090, player + 0xE758, data, length);
      break;
    case 0x8C:
      Route(0x140a70700, game::Field<void*>(state, 0x96C08), data, length);
      break;
    case 0x8D:
      Route(0x14040c830, game, data, length);
      break;
    case 0x8E:
      if (!player) break;
      Route(0x14058a4f0, player + 0x10160, data, length);
      break;
    case 0x90:
      if (!Member(game, 0x711B)) break;
      Route(0x1408da410, Member(game, 0x711B), data, length);
      break;
    case 0x92:
      result = game::Call<bool (*)(void*, int, const uint8_t*, int)>(0x14071f500)(Member(game, 0x710C), 0, data, length);
      break;
    case 0x93:
      VirtualRoute<void>(game, 92, data, length);
      break;
    case 0x94:
      if (!Member(game, 0x711C)) break;
      Route(0x1408e00b0, Member(game, 0x711C), data, length);
      break;
    case 0x95:
      if (!player) break;
      Route(0x1405819a0, player + 0xDA78, data, length);
      break;
    case 0x96:
      if (!player) break;
      Route(0x14058d390, player + 0xCBF8, data, length);
      break;
    case 0x9C:
      Route(0x140408990, game, data, length);
      break;
    case 0x9E:
      Route(0x14040a5a0, game, data, length);
      break;
    case 0x9F:
      if (!player) break;
      Route(0x140594400, player + 0xD568, data, length);
      break;
    case 0xA1:
      Route(0x1403fd440, game, data, length);
      break;
    case 0xA2:
      if (!player) break;
      Route(0x140630bb0, player, data, length);
      break;
    case 0xA4:
      if (!GlobalObject(0x142b19c50)) break;
      Route(0x140648e60, GlobalObject(0x142b19c50), data, length);
      break;
    case 0xA5:
      Route(0x140a91c40, Member(game, 0x716C), data, length);
      break;
    case 0xA6:
      if (!player) break;
      Route(0x140576410, player + 0xC8B8, data, length);
      break;
    case 0xA7:
      game::Call<void (*)(const uint8_t*, int)>(0x140adc700)(data, length);
      result = false;
      break;
    case 0xAC:
      if (!player) break;
      Route(0x14057dae0, player + 0xCB20, data, length);
      break;
    case 0xAD:
      Route(0x140408ce0, game, data, length);
      break;
    case 0xB3:
      if (!player) break;
      Route(0x140578c30, player + 0xCA30, data, length);
      break;
    case 0xB4:
      Route(0x140ab8610, GlobalObject(0x142b19a88), data, length);
      break;
    case 0xB7:
      if (!GlobalObject(0x142b19c48)) break;
      Route(0x1407dc2f0, GlobalObject(0x142b19c48), data, length);
      break;
    case 0xBC:
      if (!GlobalObject(0x142b19a98)) { result = false; break; }
      Route(0x140ac16d0, GlobalObject(0x142b19a98), data, length);
      result = false;
      break;
    case 0xBD:
      if (!GlobalObject(0x142b19a80)) { result = false; break; }
      Route(0x140ac29e0, GlobalObject(0x142b19a80), data, length);
      result = false;
      break;
    case 0xBE:
      if (!GlobalObject(0x142b19948)) { result = false; break; }
      Route(0x140799de0, GlobalObject(0x142b19948), data, length);
      result = false;
      break;
    case 0xC0:
      if (!player) break;
      Route(0x1405863f0, player + 0x10159, data, length);
      break;
    case 0xC2:
      if (!GlobalObject(0x142b19c98)) { result = false; break; }
      Route(0x14076e110, GlobalObject(0x142b19c98), data, length);
      result = false;
      break;
    case 0xC6:
      Route(0x14066bde0, game::Field<void*>(state, 0x96C90), data, length);
      break;
    case 0xC7:
      Route(0x1406709f0, game::Field<void*>(state, 0x96CA0), data, length);
      break;
    case 0xC8:
      Route(0x14040be90, game, data, length);
      break;
    case 0xC9:
      Route(0x1405ff9e0, static_cast<uint8_t*>(GlobalObject(0x142b19ba0)) + 0x106C8, data, length);
      result = false;
      break;
    case 0xCD:
      if (!player) break;
      Route(0x1405d0160, player + 0xF838, data, length);
      break;
    case 0xCE:
      Route(0x14040b8d0, game, data, length);
      result = false;
      break;
    case 0xD0:
      if (!player) break;
      Route(0x14057ad40, player + 0xCA60, data, length);
      break;
    case 0xD2:
      Route(0x140673900, game::Field<void*>(state, 0x96CB0), data, length);
      break;
    case 0xD4:
      Route(0x14040aab0, game, data, length);
      result = false;
      break;
    case 0xD9:
      result = game::Call<bool (*)(void*, int, const uint8_t*, int)>(0x14071f410)(Member(game, 0x710C), 0, data, length);
      break;
    case 0xDA:
      Route(0x1404090a0, game, data, length);
      result = false;
      break;
    case 0xDD:
      Route(0x1403fdea0, game, data, length);
      result = false;
      break;
    case 0xE0:
      Route(0x1404087c0, game, data, length);
      result = false;
      break;
    case 0xE2:
      VirtualRoute<void>(game, 94, data, length);
      break;
    case 0xE5:
      Route(0x14040a620, game, data, length);
      result = false;
      break;
    case 0xE7:
      Route(0x14040a7b0, game, data, length);
      break;
    case 0xE8: case 0xE9:
      Route(0x140ada6a0, GlobalObject(0x142b19868), data, length);
      result = false;
      break;
    case 0xEA:
      Route(0x140a9b9e0, GlobalObject(0x142b19870), data, length);
      result = false;
      break;
    case 0x05:  // ZoneDoneSendingInitialData
      game::Field<bool>(game, kInitialDataDone) = true;
      game::Call<void (*)(const char*, const char*)>(0x1402bab70)(reinterpret_cast<const char*>(0x142054710),
                                                                  reinterpret_cast<const char*>(0x14206dfa0));
      if (player) game::Call<void (*)(uint8_t*)>(0x140631f60)(player);
      result = false;
      break;
    case 0x32: {
      ValuePacket packet{reinterpret_cast<void**>(0x142063c98), 0x32, 0, 0};
      using ReadFn = bool (*)(ValuePacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038c4e0)(&packet, data, length, true)) game::Field<int>(game, kOpcode32Value) = packet.value;
      break;
    }
    case 0x51:  // login to the game server failed
      game::Field<bool>(game, kLoginFailed) = true;
      {
        using ShutdownFn = void (*)(uint8_t*, bool, int, const char*, void*);
        reinterpret_cast<ShutdownFn>((*reinterpret_cast<void***>(game))[0xD8 / 8])(
            game, true, 0xE, reinterpret_cast<const char*>(0x14206de30), nullptr);  // "...Forcing a shutdown."
      }
      result = false;
      break;
    case 0xBB:
      Route(0x140abaff0, game::Call<void* (*)()>(0x140359330)(), data, length);
      result = false;
      break;
    case 0xCA:
      LazyInit(0x142b19e39, 0x140355520);
      Route(0x1407747e0, GlobalObject(0x142b19ad0), data, length);
      result = false;
      break;
    case 0xCF:
      LazyInit(0x142b19e38, 0x1403557c0);
      Route(0x140495ee0, GlobalObject(0x142b19ad8), data, length);
      result = false;
      break;
    case 0xEB:
      game::Call<void (*)(void*, int, const uint8_t*, int)>(0x140ae0f70)(game::Call<void* (*)()>(0x140ae0cc0)(), 0, data,
                                                                       length);
      result = false;
      break;
    case 0xEC:
      if (!player) break;
      Route(0x140662370, game::Field<void*>(state, 0x96BE8), data, length);
      break;
    case 0x2F: {  // KickedFromServer
      StringPacket packet{reinterpret_cast<void**>(0x142063c58), 0x2F, 0,
                          {reinterpret_cast<void**>(0x142049dc8), reinterpret_cast<char*>(0x143e09641), 0, 0}};
      using ReadFn = bool (*)(StringPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038b000)(&packet, data, length, true)) {
        game::Call<void (*)(uint8_t*, const char*)>(0x1402bd670)(game + kKickReason, packet.text.data);
        game::Call<void (*)(const char*, const char*, const char*)>(0x1402bab70)(
            reinterpret_cast<const char*>(0x142054710), reinterpret_cast<const char*>(0x14206dfc8),
            packet.text.data);  // "RECEIVED=KickedFromServer: %s"
      }
      game::Call<void (*)(StringPacket*)>(0x1403b0700)(&packet);
      break;
    }
    case 0x69: {
      ValuePacket packet{reinterpret_cast<void**>(0x142063da8), 0x69, 0, 0};
      using ReadFn = bool (*)(ValuePacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038c3d0)(&packet, data, length, true)) {
        game::Field<int>(game, kOpcode69Value) = packet.value;
        auto* target = game::Field<uint8_t*>(GlobalObject(0x142b19cc0), 0x28);
        game::Field<bool>(target, 0x80) = packet.value >= 0x12;
        reinterpret_cast<void (*)(uint8_t*)>((*reinterpret_cast<void***>(target))[5])(target);
      }
      break;
    }
    case 0xAB: {
      ValueFlagPacket packet{};
      packet.vtable = reinterpret_cast<void**>(0x142064390);
      packet.opcode = 0xAB;
      packet.value = *reinterpret_cast<int*>(0x142b186ac);
      packet.flag = false;
      using ReadFn = bool (*)(ValueFlagPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x140389300)(&packet, data, length, false))
        reinterpret_cast<void (*)(uint8_t*, ValueFlagPacket*)>((*reinterpret_cast<void***>(game))[0x250 / 8])(game, &packet);
      break;
    }
    case 0xAF: {
      U64Packet packet{reinterpret_cast<void**>(0x1420643a0), 0xAF, *reinterpret_cast<uint64_t*>(0x142b181f8)};
      using ReadFn = bool (*)(U64Packet*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x140388b40)(&packet, data, length, false))
        game::Call<void (*)(void*, uint64_t*)>(0x14071c9a0)(Member(game, 0x710C), &packet.value);
      break;
    }
    case 0x3D: {
      StringPacket packet{reinterpret_cast<void**>(0x142063cc0), 0x3D, 0,
                          {reinterpret_cast<void**>(0x142049e08), reinterpret_cast<char*>(0x143e09641), 0, 0}};
      using ReadFn = bool (*)(StringPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038ae10)(&packet, data, length, false))
        game::Call<void (*)(uint8_t*, const char*)>(0x1402bd670)(game + kOpcode3DString, packet.text.data);
      game::Call<void (*)(StringPacket*)>(0x1403b0630)(&packet);
      break;
    }
    case 0xB9: {
      // Opcode-only packet: must carry nothing after the opcode byte.
      if (!data || data + 1 > data + length || length - 1 > 0) {
        result = false;
        break;
      }
      auto* client = static_cast<uint8_t*>(GlobalObject(0x142b19780));
      auto* a = game::Field<uint8_t*>(client, 0x389E0);
      auto* b = a ? game::Field<uint8_t*>(a, 0xE8) : nullptr;
      void* c = b ? game::Field<void*>(b, 0x98) : nullptr;
      if (c) game::Call<void (*)(void*)>(0x1407fe680)(c);
      result = false;
      break;
    }
    case 0xE6: {  // server-side byte dump
      BytesPacket packet{reinterpret_cast<void**>(0x142063ff0), 0xE6, 0, nullptr, 0, 0};
      using ReadFn = bool (*)(BytesPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x140388840)(&packet, data, length, false)) {
        game::Call<void (*)(const char*, const char*, int)>(0x1402bab70)(
            reinterpret_cast<const char*>(0x142054700), reinterpret_cast<const char*>(0x14206e8c8),
            packet.size);  // "H1Z1.log": "Received %d bytes from server:"
        auto dump = *reinterpret_cast<void (**)(const uint8_t*, int64_t)>(0x142b176f8);
        dump(packet.bytes, packet.size);
      }
      break;
    }
    case 0xEE: {
      ValuePacket packet{reinterpret_cast<void**>(0x142063ff8), 0xEE, 0, 0};
      using ReadFn = bool (*)(ValuePacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038bae0)(&packet, data, length, false))
        game::Call<void (*)(void*, bool, int)>(0x140737600)(Member(game, 0x7170), packet.value == 1, packet.value);
      result = false;
      break;
    }
    case 0x25: {
      alignas(8) uint8_t packet[kPacket25Size];
      game::Call<void (*)(uint8_t*)>(0x1416ffb60)(packet);  // vtable, opcode 0x25, defaults
      if (!data) {
        result = false;
        break;
      }
      PacketReader reader{data, length, data, data + length, 0};
      game::Call<void (*)(uint8_t*, PacketReader*)>(0x14036eb60)(packet, &reader);
      if (static_cast<uint8_t>(reader.failed) || static_cast<int>(reader.end - reader.cursor) > 0) {
        result = false;  // malformed or trailing bytes
        break;
      }
      game::Call<void (*)(uint8_t*, uint8_t*)>(0x140408280)(game, packet);
      break;
    }
    case 0xAA: {
      ValueFloatPacket packet{reinterpret_cast<void**>(0x142064388), 0xAA, 0, *reinterpret_cast<int*>(0x142b186ac), 0.0f};
      using ReadFn = bool (*)(ValueFloatPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038c7a0)(&packet, data, length, false)) {
        int key = packet.value;
        auto* object = game::Call<uint8_t* (*)(uint8_t*, int*)>(0x1403f8380)(game, &key);
        if (object)
          reinterpret_cast<void (*)(uint8_t*, float)>((*reinterpret_cast<void***>(object))[0x2E8 / 8])(object,
                                                                                                    packet.amount);
      }
      result = false;
      break;
    }
    case 0xB6: {
      FlagPacket packet{reinterpret_cast<void**>(0x142063e18), 0xB6, 0, false};
      using ReadFn = bool (*)(FlagPacket*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038bb60)(&packet, data, length, false)) {
        auto* client = static_cast<uint8_t*>(GlobalObject(0x142b19780));
        auto* a = game::Field<uint8_t*>(client, 0x389E0);
        auto* b = a ? game::Field<uint8_t*>(a, 0xE8) : nullptr;
        void* c = b ? game::Field<void*>(b, 0x98) : nullptr;
        if (c) {
          bool flag = packet.flag;
          game::Call<void (*)(void*, bool)>(0x140800280)(c, flag);
          if (!flag) game::Call<void (*)(uint8_t*)>(0x140828d60)(a);
        }
      }
      result = false;
      break;
    }
    case 0x33: {
      alignas(8) uint8_t packet[kPacket33Size];
      game::Call<void (*)(uint8_t*)>(0x14039d7c0)(packet);
      using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038b810)(packet, data, length, true)) {
        if (void* target = Member(game, 0x71B9)) {
          game::Call<void (*)(void*, const char*, const char*)>(0x14083db50)(
              target, game::Field<const char*>(packet, 0x18), game::Field<const char*>(packet, 0x238));
        }
      }
      game::Call<void (*)(uint8_t*)>(0x1403b0b60)(packet);
      break;
    }
    case 0xA8: {
      uint8_t* packet = opaque;
      game::Field<void*>(packet, 0) = reinterpret_cast<void*>(0x142063e00);
      game::Field<int>(packet, 8) = 0xA8;
      game::Call<void (*)(uint8_t*)>(0x1416ce310)(packet + 0x10);
      using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038be60)(packet, data, length, false))
        game::Call<void (*)(void*, uint8_t*)>(0x1407a17b0)(game::Field<void*>(GlobalObject(0x142b19cc0), 0xC0), packet);
      game::Call<void (*)(uint8_t*)>(0x1416ce3a0)(packet + 0x10);
      result = false;
      break;
    }
    case 0xAE: {
      uint8_t* packet = opaque;
      game::Call<void (*)(uint8_t*)>(0x14039a4e0)(packet);
      using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x140388a70)(packet, data, length, false)) {
        game::Call<void (*)(uint8_t*, uint8_t*)>(0x14040afe0)(game, packet);
      } else {
        result = false;
      }
      break;  // the original runs no destructor here
    }
    case 0xB0: {
      Packet3Strings packet{};
      packet.vtable = reinterpret_cast<void**>(0x142063e08);
      packet.opcode = 0xB0;
      for (PacketString& text : packet.text) text = {reinterpret_cast<void**>(0x142049b50), reinterpret_cast<char*>(0x143e09641), 0, 0};
      packet.value = 1;
      using ReadFn = bool (*)(Packet3Strings*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038b2b0)(&packet, data, length, false)) {
        game::Call<void (*)(void*)>(0x140ab7c50)(GlobalObject(0x142b19ae0));
        game::Call<void (*)(void*, PacketString*)>(0x14046d1d0)(GlobalObject(0x142b19ae0), packet.text);
        game::Call<void (*)(void*)>(0x140ab7c50)(GlobalObject(0x142b19ae0));
        game::Call<void (*)(PacketString*)>(0x1403ade00)(packet.text);
        result = false;
        break;
      }
      game::Call<void (*)(PacketString*)>(0x1403ade00)(packet.text);
      [[fallthrough]];  // the original falls into case 0xB1 when the read fails
    }
    case 0xB1: {
      uint8_t* packet = opaque;
      game::Call<void (*)(uint8_t*)>(0x14039d910)(packet);
      using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038bc20)(packet, data, length, false)) {
        auto* root = static_cast<uint8_t*>(GlobalObject(0x142b19cc0));
        game::Call<void (*)(void*, uint8_t*)>(0x14079fb80)(game::Field<void*>(root, 0x130), packet);
        root = static_cast<uint8_t*>(GlobalObject(0x142b19cc0));
        game::Call<void (*)(void*, uint8_t*)>(0x14079f510)(game::Field<void*>(root, 0x78), packet);
        game::Call<void (*)(uint8_t*)>(0x14059d750)(player + 0xDD70);  // not null-checked in the original
      }
      game::Call<void (*)(uint8_t*)>(0x1416cb040)(packet + 0x10);
      result = false;
      break;
    }
    case 0x97: {
      Packet97 packet{reinterpret_cast<void**>(0x142063df0), 0x97, 0, reinterpret_cast<void**>(0x142063dd0), {}, 0};
      using ReadFn = bool (*)(Packet97*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038b530)(&packet, data, length, false)) {
        game::Call<void (*)(void*, Packet97*)>(0x14079c110)(game::Field<void*>(GlobalObject(0x142b19cc0), 0xB8), &packet);
        game::Call<void (*)(void*, Packet97*, void*)>(0x140677100)(Member(game, 0x716B), &packet, Member(game, 0x715E));
      }
      packet.bodyVtable = reinterpret_cast<void**>(0x142063dd0);
      game::Call<void (*)(void***)>(0x1404539a0)(&packet.bodyVtable);
      result = false;
      break;
    }
    case 0xCB: {
      U64Packet packet{reinterpret_cast<void**>(0x142063c40), 0xCB, game::Field<uint64_t>(game, 0x31498)};
      using ReadFn = bool (*)(U64Packet*, const uint8_t*, int, bool);
      if (game::Call<ReadFn>(0x14038c450)(&packet, data, length, true)) game::Call<void (*)(uint8_t*)>(0x140474500)(game);
      result = false;
      break;
    }
    case 0x79: {  // same packet the zone client builds for channel 2
      uint8_t* packet = opaque;
      game::Call<void (*)(uint8_t*)>(0x14039acb0)(packet);
      using ReadFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
      result = game::Call<ReadFn>(0x1403893c0)(packet, data, length, false);
      if (result) reinterpret_cast<void (*)(uint8_t*, uint8_t*)>((*reinterpret_cast<void***>(game))[0x248 / 8])(game, packet);
      game::Call<void (*)(uint8_t*)>(0x1417f2750)(packet + 0x10);
      break;
    }
    case 0xD6:
      ReadAndHandle(game, opaque, 0x140399f50, 0x140366140, 0x1403fd710, 0x1403b14d0, 0x20, data, length);
      result = false;
      break;
    case 0xD7:
      ReadAndHandle(game, opaque, 0x140399dc0, 0x140365ab0, 0x1403fd4d0, 0x1403ac9f0, 0, data, length);
      result = false;
      break;
    case 0xDB:
      ReadAndHandle(game, opaque, 0x14039a110, 0x140366630, 0x140408fc0, 0x1403acbd0, 0, data, length);
      result = false;
      break;
    case 0xDC:
      ReadAndHandle(game, opaque, 0x14039aa10, 0x1403675a0, 0x140409a60, 0x1403ad0b0, 0, data, length);
      result = false;
      break;
    case 0x76:
      break;
    case 0x63: case 0x70: case 0xC3:
      result = false;
      break;
    case 0x03: case 0x08: case 0x0B: case 0x16: case 0x2C: case 0x30: case 0x35:
    case 0x3E: case 0x3F: case 0x40: case 0x43: case 0x44: case 0x4F: case 0x61: case 0x62: case 0x65:
    case 0x78: case 0x7D: case 0x99:
    case 0xC5: case 0xD5:
    case 0xD8: case 0xDE: case 0xE3:
      return g_originalDispatch(game, header, data, length, channel);  // not rebuilt yet
    default:  // no built-in case
      return OfferToExtension(state, header, data, length, channel);
  }
  return Finish(state, channel, data, length, result);
}

REBUILD_FUNCTION_WITH_ORIGINAL(GameClient_DispatchZonePacket, 0x1403fe210, GameClientDispatchZonePacket,
                               g_originalDispatch);

}  // namespace rebuild::game_net
