// Thread-safe statistics getters on UdpConnection / UdpManager (each takes
// the object's guard around the read). Used by ClientServerCore's
// connection-terminated log line.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpRefCount.h"

namespace rebuild::udp {
namespace {

UdpPlatformGuardObject* GuardAt(uint8_t* object, size_t offset) {
  return reinterpret_cast<UdpPlatformGuardObject*>(object + offset);
}

struct Guarded {
  explicit Guarded(UdpPlatformGuardObject* g) : guard(g) { guard->Enter(); }
  ~Guarded() { guard->Leave(); }
  UdpPlatformGuardObject* guard;
};

constexpr size_t kConnectionGuard = 0x2E0;
constexpr size_t kConnectionLastSend = 0x238;     // clock stamp
constexpr size_t kConnectionLastReceive = 0x240;  // clock stamp
constexpr size_t kConnectionOutgoingBytesLastSecond = 0x308;
constexpr size_t kConnectionIncomingBytesLastSecond = 0x30C;

int ConnectionElapsed(uint8_t* connection, int64_t stamp) {
  return game::Call<int (*)(uint8_t*, int64_t)>(0x14030d440)(connection, stamp);
}

}  // namespace

// 0x1415f4fe0
int ConnectionOutgoingBytesLastSecond(uint8_t* connection) {
  Guarded lock(GuardAt(connection, kConnectionGuard));
  game::Call<void (*)(uint8_t*)>(0x140346630)(connection);  // UpdateSendBuckets
  return game::Field<int>(connection, kConnectionOutgoingBytesLastSecond);
}

// 0x1415f4610
int ConnectionIncomingBytesLastSecond(uint8_t* connection) {
  Guarded lock(GuardAt(connection, kConnectionGuard));
  game::Call<void (*)(uint8_t*)>(0x140346510)(connection);  // UpdateReceiveBuckets
  return game::Field<int>(connection, kConnectionIncomingBytesLastSecond);
}

// 0x1415f4a70: ms since the last packet was received.
int ConnectionLastReceive(uint8_t* connection) {
  Guarded lock(GuardAt(connection, kConnectionGuard));
  return ConnectionElapsed(connection, game::Field<int64_t>(connection, kConnectionLastReceive));
}

// 0x1415f4ad0: ms since the last packet was sent.
int ConnectionLastSend(uint8_t* connection) {
  Guarded lock(GuardAt(connection, kConnectionGuard));
  return ConnectionElapsed(connection, game::Field<int64_t>(connection, kConnectionLastSend));
}

// 0x1415f4a10: ms since the manager last delivered an event (0 if never).
int ManagerLastEventAge(uint8_t* manager) {
  Guarded lock(GuardAt(manager, 0x310));
  int64_t stamp = game::Field<int64_t>(manager, 0x2D0);
  if (stamp == 0) return 0;
  return game::Call<int (*)(uint8_t*, int64_t)>(0x14033f0a0)(manager, stamp);  // ClockElapsed
}

// 0x140341130: UdpManager::GetStats. Copies the running statistics block,
// then fills the live counters.
void ManagerGetStats(uint8_t* manager, uint8_t* stats) {
  Guarded lock(GuardAt(manager, 0x310));
  std::memcpy(stats, manager + 0x348, 0xB0);  // 64-bit counters
  std::memcpy(stats + 0xB0, manager + 0x3F8, 0x20);
  *reinterpret_cast<int*>(stats + 0xC4) = game::Field<int>(manager, 0x60C);
  *reinterpret_cast<int*>(stats + 0xC0) = game::Field<int>(manager, 0x5EC);
  {
    Guarded poolLock(GuardAt(manager, 0x318));
    *reinterpret_cast<int*>(stats + 0xAC) = game::Field<int>(manager, 0x264);
  }
  *reinterpret_cast<int*>(stats + 0xA8) = game::Field<int>(manager, 0x23C);
  *reinterpret_cast<int*>(stats + 0xB0) = game::Field<int>(manager, 0x65C);
  *reinterpret_cast<int*>(stats + 0xB4) = game::Field<int>(manager, 0x200);
  int64_t start = game::Field<int64_t>(manager, 0x418);
  int64_t clock;
  {
    Guarded clockLock(GuardAt(manager, 0x2E8));
    clock = game::Field<int64_t>(manager, 0x2E0);
  }
  int64_t elapsed = clock - start;
  *reinterpret_cast<int*>(stats + 0xC8) = elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
}

REBUILD_FUNCTION(UdpConnection_OutgoingBytesLastSecond, 0x1415f4fe0, ConnectionOutgoingBytesLastSecond);
REBUILD_FUNCTION(UdpConnection_IncomingBytesLastSecond, 0x1415f4610, ConnectionIncomingBytesLastSecond);
REBUILD_FUNCTION(UdpConnection_LastReceive, 0x1415f4a70, ConnectionLastReceive);
REBUILD_FUNCTION(UdpConnection_LastSend, 0x1415f4ad0, ConnectionLastSend);
REBUILD_FUNCTION(UdpManager_LastEventAge, 0x1415f4a10, ManagerLastEventAge);
REBUILD_FUNCTION(UdpManager_GetStats, 0x140341130, ManagerGetStats);

}  // namespace rebuild::udp
