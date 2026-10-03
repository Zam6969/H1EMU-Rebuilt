// UdpConnection's periodic work (InternalGiveTime) and the small helpers
// it and the dispatcher use.
#include <climits>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpConnection.h"
#include "udp/UdpPlatformDriver.h"
#include "udp/UdpReliableChannel.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

constexpr int kReasonApplication = 2;          // no data timeout
constexpr int kReasonConnectTimeout = 5;
constexpr int kReasonReliableOverflow = 13;

UdpManager* Manager(UdpConnection* c) { return ConnField<UdpManager*>(c, O::kManager); }
int Status(UdpConnection* c) { return ConnField<int>(c, UdpConnectionInternals::kStatus); }

void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}
void PutBe64(uint8_t* p, uint64_t v) {
  PutBe32(p, static_cast<uint32_t>(v >> 32));
  PutBe32(p + 4, static_cast<uint32_t>(v));
}

// Not rebuilt in this file.
void CallbackRoutePacket(UdpManager* m, UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpManager*, UdpConnection*, const uint8_t*, int)>(0x14033ed30)(m, c, data, length);
}
void PortUnreachable(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140348310)(c); }
int ReliableGiveTime(void* channel) { return game::Call<int (*)(void*)>(0x14034ceb0)(channel); }

}  // namespace

// 0x140347d00 / 0x140347d70: the manager clock (refreshed) as 32- and
// 16-bit clock-sync stamps.
uint32_t ManagerLocalSyncStampLong(UdpManager* m) { return static_cast<uint32_t>(ManagerClock(m)); }
uint16_t ManagerLocalSyncStampShort(UdpManager* m) { return static_cast<uint16_t>(ManagerClock(m)); }

// 0x14034bbc0: wrapping distance between two 16-bit stamps.
int SyncStampShortDelta(uint16_t start, uint16_t stop) {
  uint16_t delta = static_cast<uint16_t>(start - stop);
  return delta > 0x7FFF ? 0xFFFF - delta : delta;
}

// 0x140344830: 00 1C sent with a short TTL to keep the NAT mapping alive.
void ManagerSendPortAlive(UdpManager* m, const UdpIpAddress* ip, int port) {
  uint8_t packet[2] = {0x00, 0x1C};
  using Fn = void (*)(UdpPlatformDriver*, const uint8_t*, int, const UdpIpAddress*, int);
  reinterpret_cast<Fn>(m->driver->vtable[kSlotSocketSendPortAlive])(m->driver, packet, 2, ip, port);
}

// 0x140345cd0: ms since the connection was created.
int ConnectionAge(UdpConnection* c) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  int age = ConnectionElapsed(c, ConnField<int64_t>(c, 0x1B0));
  guard.Leave();
  return age;
}

// 0x140349d80: reliable bytes still waiting on all channels.
int ConnectionTotalPendingBytes(UdpConnection* c) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  int total = 0;
  for (size_t slot = 0x1D0; slot <= 0x1E8; slot += 8) {
    if (auto* channel = ConnField<uint8_t*>(c, slot))
      total += game::Field<int>(channel, 0x84) + game::Field<int>(channel, 0x80);
  }
  guard.Leave();
  return total;
}

// 0x140349a60
void ConnectionSetOtherSideTerminated(UdpConnection* c, bool value) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  ConnField<uint8_t>(c, 0x1C9) = value;
  guard.Leave();
}

// 0x140345c30: application data reaches the game only on a connected link.
void ConnectionProcessApplicationPacket(UdpConnection* c, const uint8_t* data, int length) {
  if (Status(c) != kStatusConnected) return;
  UdpManager* manager = Manager(c);
  manager->StatsGuard().Enter();
  game::Field<int64_t>(manager, 0x3C0) += 1;
  manager->StatsGuard().Leave();
  ConnField<int64_t>(c, 0x150) += 1;
  CallbackRoutePacket(Manager(c), c, data, length);
}

// 0x140347360. Everything time-driven on a connection; reschedules the
// connection for its earliest deadline.
void ConnectionInternalGiveTime(UdpConnection* c) {
  int next = 600000;
  ConnField<int64_t>(c, 0x158) += 1;
  if (ConnField<uint8_t>(c, 0x1C8)) {
    ConnField<uint8_t>(c, 0x1C8) = 0;
    PortUnreachable(c);
  }

  auto takeMin = [&next](int wait) {
    if (wait < next) next = wait;
  };
  int status = Status(c);
  if (status == kStatusNegotiating) {
    int connectTimeout = ConnField<int>(c, 0x1B8);
    if (connectTimeout > 0 && ConnectionAge(c) > connectTimeout) {
      ConnectionDisconnect(c, 0, kReasonConnectTimeout);
      return;
    }
    int sinceSend = ConnectionElapsed(c, ConnField<int64_t>(c, 0x238));
    UdpManager* manager = Manager(c);
    int attemptDelay = manager->params.At<int>(0x6C);
    if (attemptDelay <= sinceSend) {
      // 00 01 <protocol 3> <connect code> <max raw packet size> <name\0>
      uint8_t connect[0x100];  // the original copies the name unbounded into a large stack buffer
      connect[0] = 0x00;
      connect[1] = 0x01;
      PutBe32(connect + 2, 3);
      PutBe32(connect + 6, ConnField<uint32_t>(c, O::kConnectCode));
      PutBe32(connect + 10, static_cast<uint32_t>(manager->MaxRawPacketSize()));
      const char* name = reinterpret_cast<const char*>(&manager->params.At<uint8_t>(0x181));
      char* out = reinterpret_cast<char*>(connect + 14);
      while (*name) *out++ = *name++;
      *out++ = 0;
      game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x1403495a0)(
          c, connect, static_cast<int>(reinterpret_cast<uint8_t*>(out) - connect));
      sinceSend = 0;
    }
    takeMin(Manager(c)->params.At<int>(0x6C) - sinceSend);
  } else if (status == kStatusConnected || status == 3) {
    UdpManager* manager = Manager(c);
    int syncDelay = manager->params.At<int>(0x50);
    if (syncDelay > 0) {
      int sinceSync = ConnectionElapsed(c, ConnField<int64_t>(c, 0x228));
      int masterPing = ConnField<int>(c, 0x29C);
      int samples = ConnField<int>(c, 0x28C);
      // Sync more often while the link looks bad or is still unmeasured.
      if (syncDelay < sinceSync || (masterPing > 3000 && sinceSync > 2000) ||
          (masterPing > 1000 && sinceSync > 5000) || (samples < 2 && sinceSync > 10000)) {
        int average = samples > 0 ? ConnField<int>(c, 0x288) / samples : 0;
        uint8_t sync[40];
        uint16_t stamp = ManagerLocalSyncStampShort(Manager(c));
        sync[0] = 0x00;
        sync[1] = 0x07;
        sync[2] = static_cast<uint8_t>(stamp >> 8);
        sync[3] = static_cast<uint8_t>(stamp);
        PutBe32(sync + 4, static_cast<uint32_t>(masterPing));
        PutBe32(sync + 8, static_cast<uint32_t>(average));
        PutBe32(sync + 12, static_cast<uint32_t>(ConnField<int>(c, 0x290)));
        PutBe32(sync + 16, static_cast<uint32_t>(ConnField<int>(c, 0x294)));
        PutBe32(sync + 20, static_cast<uint32_t>(ConnField<int>(c, 0x298)));
        PutBe64(sync + 24, ConnField<uint64_t>(c, 0x100) + 1);  // this packet included
        PutBe64(sync + 32, ConnField<uint64_t>(c, 0x108));
        PhysicalSend(c, sync, sizeof(sync), true);
        ConnField<int64_t>(c, 0x228) = ConnectionClock(c);
        sinceSync = 0;
      }
      int wait = Manager(c)->params.At<int>(0x50) - sinceSync;
      next = wait < 600000 ? wait : 600000;
    }

    int pendingReliable = 0;
    for (int channel = 0; channel < 4; ++channel) {
      if (auto* reliable = ConnField<uint8_t*>(c, 0x1D0 + channel * 8)) {
        pendingReliable += game::Field<int>(reliable, 0x84) + game::Field<int>(reliable, 0x80);
        int wait = ReliableGiveTime(reliable);
        if (!Manager(c)) return;
        takeMin(wait);
      }
    }
    int maxOutstanding = Manager(c)->params.At<int>(0x38);
    if (maxOutstanding != 0 && maxOutstanding <= pendingReliable) {
      ConnectionDisconnect(c, 0, kReasonReliableOverflow);
      return;
    }

    if (ConnField<uint8_t*>(c, 0x258) - ConnField<uint8_t*>(c, 0x250) > 2) {
      int held = ConnectionElapsed(c, ConnField<int64_t>(c, 0x230));
      int holdTime = Manager(c)->params.At<int>(0x3C);
      if (held < holdTime)
        takeMin(holdTime - held);
      else
        FlushChannels(c);
    }

    int64_t idle = ConnectionClock(c) - ConnField<int64_t>(c, 0x238);
    int sinceSend = idle > INT_MAX ? INT_MAX : static_cast<int>(idle);
    int keepAlive = ConnField<int>(c, 0x2B8);
    if (keepAlive > 0) {
      if (keepAlive <= sinceSend) {
        uint8_t ping[2] = {0x00, 0x06};
        PhysicalSend(c, ping, 2, true);
        sinceSend = 0;
      }
      takeMin(ConnField<int>(c, 0x2B8) - sinceSend);
    }

    int portAliveDelay = Manager(c)->params.At<int>(0x24);
    if (portAliveDelay > 0) {
      int since = ConnectionElapsed(c, ConnField<int64_t>(c, 0x248));
      if (Manager(c)->params.At<int>(0x24) <= since) {
        ConnField<int64_t>(c, 0x248) = ConnectionClock(c);
        UdpIpAddress ip = ConnField<UdpIpAddress>(c, O::kIp);
        ManagerSendPortAlive(Manager(c), &ip, ConnField<int>(c, O::kPort));
        since = 0;
      }
      takeMin(Manager(c)->params.At<int>(0x24) - since);
    }

    if (Status(c) == 3) {  // disconnect pending: wait for reliable data to drain
      int left = ConnField<int>(c, 0x2D8) - ConnectionElapsed(c, ConnField<int64_t>(c, 0x2D0));
      if (left < 0 || ConnectionTotalPendingBytes(c) == 0) {
        ConnectionDisconnect(c, 0, ConnField<int>(c, 0x1C0));
        return;
      }
      takeMin(left);
    }

    int noDataTimeout = ConnField<int>(c, 0x1BC);
    if (noDataTimeout >= 1) {
      auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
      guard.Enter();
      int silent = ConnectionElapsed(c, ConnField<int64_t>(c, 0x240));
      guard.Leave();
      if (ConnField<int>(c, 0x1BC) <= silent) {
        ConnectionDisconnect(c, 0, kReasonApplication);
        return;
      }
      takeMin(ConnField<int>(c, 0x1BC) - silent);
    }
  }

  if (UdpManager* manager = Manager(c)) {
    if (next < 0) next = 0;
    ManagerScheduleConnection(Manager(c), c, manager->CachedClock() + 5 + next);
  }
}

REBUILD_FUNCTION(UdpManager_LocalSyncStampLong, 0x140347d00, ManagerLocalSyncStampLong);
REBUILD_FUNCTION(UdpManager_LocalSyncStampShort, 0x140347d70, ManagerLocalSyncStampShort);
REBUILD_FUNCTION(UdpMisc_SyncStampShortDelta, 0x14034bbc0, SyncStampShortDelta);
REBUILD_FUNCTION(UdpManager_SendPortAlive, 0x140344830, ManagerSendPortAlive);
REBUILD_FUNCTION(UdpConnection_Age, 0x140345cd0, ConnectionAge);
REBUILD_FUNCTION(UdpConnection_TotalPendingBytes, 0x140349d80, ConnectionTotalPendingBytes);
REBUILD_FUNCTION(UdpConnection_SetOtherSideTerminated, 0x140349a60, ConnectionSetOtherSideTerminated);
REBUILD_FUNCTION(UdpConnection_ProcessApplicationPacket, 0x140345c30, ConnectionProcessApplicationPacket);
REBUILD_FUNCTION(UdpConnection_InternalGiveTime, 0x140347360, ConnectionInternalGiveTime);

}  // namespace rebuild::udp
