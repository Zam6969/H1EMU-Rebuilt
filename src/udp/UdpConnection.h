#pragma once

#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "udp/UdpManager.h"
#include "udp/UdpRefCount.h"

// UdpLibrary::UdpConnection (vtable 0x142051fc8, size 0x450). Being rebuilt
// function by function; the struct is still addressed through named offsets
// (UdpConnectionFields / UdpConnectionInternals and the ones below) until
// enough of it is known to lay it out as a real struct.
namespace rebuild::udp {

enum UdpConnectionStatus {
  kStatusNegotiating = 0,
  kStatusConnected = 1,
  kStatusDisconnected = 2,
};

// UdpLibrary encryption methods, per pass (two passes, ints at +0x1F8).
enum UdpEncryptMethod {
  kEncryptNone = 0,
  kEncryptUserSupplied = 1,
  kEncryptUserSupplied2 = 2,
  kEncryptXorBuffer = 3,
  kEncryptXor = 4,
};

struct ConnectionOffsets {
  static constexpr size_t kHeapIndex = 0x18;
  static constexpr size_t kDecryptPasses = 0x40;   // 2 x {method pmf, this-adjust, pad}, stride 0x18
  static constexpr size_t kEncryptPasses = 0x70;
  static constexpr size_t kIp = 0xA0;
  static constexpr size_t kPort = 0xA4;
  static constexpr size_t kManager = 0xE0;
  static constexpr size_t kConnectCode = 0xE8;
  static constexpr size_t kEncryptCode = 0x1F0;
  static constexpr size_t kEncryptMethods = 0x1F8;  // int[2]
  static constexpr size_t kHandler = 0x2B0;
  static constexpr size_t kInGiveTime = 0x2A8;
  static constexpr size_t kEncryptExpansionBytes = 0x280;
  static constexpr size_t kXorBuffer = 0x278;
  static constexpr size_t kStatusGuard = 0x2E0;
  static constexpr size_t kHandlerGuard = 0x2E8;
  static constexpr size_t kCachedTime = 0x260;  // used when detached from the manager
};

template <class T>
T& ConnField(UdpConnection* connection, size_t offset) {
  return game::Field<T>(connection, offset);
}

void ConnectionInit(UdpConnection* self, UdpManager* manager, const UdpIpAddress* ip, int port);  // 0x140346fd0
void ConnectionSetupEncryption(UdpConnection* self);                // 0x140349ab0
void ConnectionGiveTime(UdpConnection* self, bool fromManager);     // 0x140346e80
void ConnectionDisconnect(UdpConnection* self, int flushTimeout, int reason);  // 0x1403471e0
int64_t ConnectionClock(UdpConnection* self);                       // 0x140345b20
void SendTerminatePacket(UdpConnection* self, uint32_t connectCode, uint16_t reason);  // 0x140349980
void FlushChannels(UdpConnection* self);                            // 0x140346820
uint32_t Crc32(const uint8_t* data, int length, uint32_t seed);     // 0x14034b470
int ConnectionElapsed(UdpConnection* self, int64_t since);          // 0x14030d440
void PhysicalSend(UdpConnection* self, const uint8_t* data, int length, bool writable);  // 0x140348070
UdpConnection* PriorityQueueUpdate(ConnectionPriorityQueue* queue, UdpConnection* c, int64_t time);  // 0x140345830
void ManagerScheduleConnection(UdpManager* manager, UdpConnection* c, int64_t time);  // 0x1403499e0
void ManagerAddDisconnecting(UdpManager* manager, UdpConnection* c);  // 0x1403421d0
void ManagerRemoveConnection(UdpManager* manager, UdpConnection* c);  // 0x1403437b0

}  // namespace rebuild::udp
