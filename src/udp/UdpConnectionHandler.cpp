// UdpConnection -> UdpConnectionHandler callbacks, and Disconnect.
#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

// UdpConnectionHandler vtable slots.
enum HandlerSlot {
  kOnRoutePacket = 1,
  kOnConnectComplete = 2,
  kOnTerminated = 3,
  kOnCrcReject = 4,
  kOnPacketCorrupt = 5,
};

constexpr int kStatusDisconnectPending = 3;
constexpr int kDisconnectReasonApplicationReleased = 14;
constexpr int kDisconnectReasonNoCallback = 4;  // reason that suppresses the Terminated callback

struct HandlerScope {
  explicit HandlerScope(UdpConnection* c)
      : guard(ConnField<UdpPlatformGuardObject>(c, O::kHandlerGuard)),
        handler(ConnField<UdpRefCount*>(c, O::kHandler)) {
    guard.Enter();
    handler = ConnField<UdpRefCount*>(c, O::kHandler);
  }
  ~HandlerScope() { guard.Leave(); }
  template <class Fn, class... Args>
  void Call(int slot, Args... args) {
    reinterpret_cast<Fn>(handler->vtable[slot])(handler, args...);
  }
  UdpPlatformGuardObject& guard;
  UdpRefCount* handler;
};

// Not rebuilt yet.
void CallbackTerminated(UdpManager* m, UdpConnection* c) {
  game::Call<void (*)(UdpManager*, UdpConnection*)>(0x14033eee0)(m, c);
}

}  // namespace

// 0x1403471e0. Ends the connection now, or (with a positive flush timeout
// on an established connection) moves it to disconnect-pending so queued
// reliable data can drain first.
void ConnectionDisconnect(UdpConnection* c, int flushTimeout, int reason) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  int& status = ConnField<int>(c, UdpConnectionInternals::kStatus);
  int& disconnectReason = ConnField<int>(c, 0x1C0);
  if (disconnectReason == 0) disconnectReason = reason;
  int flush = status != kStatusNegotiating ? flushTimeout : 0;

  if (UdpManager* manager = ConnField<UdpManager*>(c, O::kManager)) {
    if (flush < 1) {
      bool otherSideTerminated = ConnField<uint8_t>(c, 0x1C9) != 0;
      if (!otherSideTerminated && (status == kStatusConnected || status == kStatusDisconnectPending))
        SendTerminatePacket(c, ConnField<uint32_t>(c, O::kConnectCode), static_cast<uint16_t>(disconnectReason));
      ConnField<int64_t>(c, O::kCachedTime) = ConnectionClock(c);
      ConnField<UdpManager*>(c, O::kManager) = nullptr;
      status = kStatusDisconnected;
      if (reason != kDisconnectReasonNoCallback) manager->VirtualAddRef();
      ManagerRemoveConnection(manager, c);
      if (reason != kDisconnectReasonNoCallback) {
        CallbackTerminated(manager, c);
        manager->VirtualRelease();
      }
    } else {
      FlushChannels(c);
      ConnField<int64_t>(c, 0x2D0) = ConnectionClock(c);  // flush started
      ConnField<int>(c, 0x2D8) = flush;
      if (!ConnField<uint8_t>(c, O::kInGiveTime) && ConnField<UdpManager*>(c, O::kManager))
        ManagerScheduleConnection(ConnField<UdpManager*>(c, O::kManager), c, 0);
      if (status != kStatusDisconnectPending) {
        status = kStatusDisconnectPending;
        ManagerAddDisconnecting(ConnField<UdpManager*>(c, O::kManager), c);
      }
    }
  }
  guard.Leave();
}

// 0x140347f70. Without a handler nobody would ever read the data, so the
// connection is dropped.
void ConnectionOnRoutePacket(UdpConnection* c, const uint8_t* data, int length) {
  HandlerScope scope(c);
  if (!scope.handler)
    ConnectionDisconnect(c, 0, kDisconnectReasonApplicationReleased);
  else
    scope.Call<void (*)(UdpRefCount*, UdpConnection*, const uint8_t*, int)>(kOnRoutePacket, c, data, length);
}

// 0x140347de0
void ConnectionOnConnectComplete(UdpConnection* c) {
  HandlerScope scope(c);
  if (scope.handler) scope.Call<void (*)(UdpRefCount*, UdpConnection*)>(kOnConnectComplete, c);
}

// 0x140348000
void ConnectionOnTerminated(UdpConnection* c) {
  HandlerScope scope(c);
  if (scope.handler) scope.Call<void (*)(UdpRefCount*, UdpConnection*)>(kOnTerminated, c);
}

// 0x140347e50
void ConnectionOnCrcReject(UdpConnection* c, const uint8_t* data, int length) {
  HandlerScope scope(c);
  if (scope.handler)
    scope.Call<void (*)(UdpRefCount*, UdpConnection*, const uint8_t*, int)>(kOnCrcReject, c, data, length);
}

// 0x140347ee0
void ConnectionOnPacketCorrupt(UdpConnection* c, const uint8_t* data, int length, int reason) {
  HandlerScope scope(c);
  if (scope.handler)
    scope.Call<void (*)(UdpRefCount*, UdpConnection*, const uint8_t*, int, int)>(kOnPacketCorrupt, c, data,
                                                                                  length, reason);
}

REBUILD_FUNCTION(UdpConnection_Disconnect, 0x1403471e0, ConnectionDisconnect);
REBUILD_FUNCTION(UdpConnection_OnRoutePacket, 0x140347f70, ConnectionOnRoutePacket);
REBUILD_FUNCTION(UdpConnection_OnConnectComplete, 0x140347de0, ConnectionOnConnectComplete);
REBUILD_FUNCTION(UdpConnection_OnTerminated, 0x140348000, ConnectionOnTerminated);
REBUILD_FUNCTION(UdpConnection_OnCrcReject, 0x140347e50, ConnectionOnCrcReject);
REBUILD_FUNCTION(UdpConnection_OnPacketCorrupt, 0x140347ee0, ConnectionOnPacketCorrupt);

}  // namespace rebuild::udp
