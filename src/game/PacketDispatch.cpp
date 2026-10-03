// The game's two top-level packet dispatchers: ClientServerCore::BaseApi
// vtable slot 18 ("handle an application packet"), as overridden by the
// login and gateway APIs. Opcode names match H1emu's LoginUdp_11 and gateway
// packet definitions; the client only handles the server->client opcodes.
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::game_net {
namespace {

// Login::ExternalLoginUdpApi opcodes (first byte).
enum LoginOpcode : int {
  kLoginReply = 0x02,
  kForceDisconnect = 0x04,
  kCharacterCreateReply = 0x06,
  kCharacterLoginReply = 0x08,
  kCharacterDeleteReply = 0x0A,
  kCharacterSelectInfoReply = 0x0C,
  kServerListReply = 0x0E,
  kServerUpdate = 0x0F,
  kTunnelAppPacketServerToClient = 0x11,
  kCharacterTransferReply = 0x13,
};

// Gateway::ExternalGatewayApi opcodes (low 5 bits; top 3 bits = channel).
enum GatewayOpcode : int {
  kGatewayLoginReply = 0x02,
  kGatewayForceDisconnect = 0x04,
  kTunnelPacketToExternalConnection = 0x05,
  kChannelIsRoutable = 0x07,
  kConnectionIsNotRoutable = 0x08,
};

// Each login opcode: Unserialize<Packet> + call the member handler.
// {unserialize-and-dispatch template instance, handler}
struct LoginRoute {
  int opcode;
  uintptr_t dispatch;
  uintptr_t handler;
};
constexpr LoginRoute kLoginRoutes[] = {
    {kLoginReply, 0x141633c40, 0x14163dd30},
    {kForceDisconnect, 0x141633b30, 0x14163dd00},
    {kCharacterCreateReply, 0x141633080, 0x14163dba0},
    {kCharacterLoginReply, 0x1416334b0, 0x14163dc20},
    {kCharacterDeleteReply, 0x141633260, 0x14163dbe0},
    {kCharacterSelectInfoReply, 0x141633720, 0x14163dc60},
    {kServerListReply, 0x141633de0, 0x14163ddc0},
    {kServerUpdate, 0x141634020, 0x14163dde0},
    {kTunnelAppPacketServerToClient, 0x141634280, 0x14163de00},
    {kCharacterTransferReply, 0x141633990, 0x14163dca0},
};

// MSVC pointer-to-member as passed to the gateway dispatch templates.
struct MemberFn {
  uintptr_t function;
  int thisAdjust;
  int padding;
};

}  // namespace

// 0x14163efa0: Login::ExternalLoginUdpApi::HandlePacket
void LoginHandlePacket(void* api, const uint8_t* data, int length) {
  // A 1-byte opcode; an empty packet reads as opcode 0 (ignored).
  int opcode = data + 1 <= data + length ? static_cast<int8_t>(data[0]) : 0;
  for (const LoginRoute& route : kLoginRoutes) {
    if (route.opcode == opcode) {
      using Fn = void (*)(void*, const uint8_t*, int, uintptr_t);
      game::Call<Fn>(route.dispatch)(api, data, length, route.handler);
      return;
    }
  }
}

// 0x14162d9a0: Gateway::ExternalGatewayApi::HandlePacket
void GatewayHandlePacket(uint8_t* api, const uint8_t* data, int length) {
  const uint8_t* end = data + length;
  uint8_t header = data + 1 <= end ? data[0] : 0;
  int opcode = header & 0x1F;
  using DispatchFn = void (*)(void*, const uint8_t*, int, MemberFn*);
  switch (opcode) {
    case kGatewayLoginReply: {
      MemberFn handler = {0x14162d340, 0, 0};
      game::Call<DispatchFn>(0x14162bed0)(api, data, length, &handler);
      break;
    }
    case kGatewayForceDisconnect: {
      MemberFn handler = {0x14162d320, 0, 0};
      game::Call<DispatchFn>(0x14162bd80)(api, data, length, &handler);
      break;
    }
    case kTunnelPacketToExternalConnection:
      // Game packets tunnelled through the gateway.
      game::Call<void (*)(void*, const uint8_t*, int)>(0x14162d460)(api, data, length);
      break;
    case kChannelIsRoutable: {
      MemberFn handler = {0x14162d2c0, 0, 0};
      game::Call<DispatchFn>(0x14162bca0)(api, data, length, &handler);
      break;
    }
    case kConnectionIsNotRoutable: {
      // Inline unserialize: {packet vtable, opcode, channel}.
      struct {
        void** vtable;
        int opcode;
        int channel;
      } packet = {reinterpret_cast<void**>(0x1424be278), 0, 0};
      bool ok = data + 1 <= end;
      uint8_t byte = ok ? data[0] : 0;
      packet.channel = byte >> 5;
      packet.opcode = byte & 0x1F;
      if (ok) {
        game::Call<void (*)(void*, void*)>(0x14162d300)(api, &packet);
      } else {
        using LogFn = void (*)(void*, void*, const char*, ...);
        game::Call<LogFn>(0x14165c7e0)(
            api + 0xD40, game::Field<void*>(game::Field<uint8_t*>(api, 0x2D0), 0x250),
            reinterpret_cast<const char*>(0x1424be570),  // game's "unserialize failed" format
            length);
      }
      break;
    }
    default:
      break;
  }
}

REBUILD_FUNCTION(Login_ExternalLoginUdpApi_HandlePacket, 0x14163efa0, LoginHandlePacket);
REBUILD_FUNCTION(Gateway_ExternalGatewayApi_HandlePacket, 0x14162d9a0, GatewayHandlePacket);

}  // namespace rebuild::game_net
