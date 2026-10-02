// UdpConnection's receive path: statistics, CRC check, decryption, and the
// error cases (ICMP unreachable, corrupt packets).
#include <climits>

#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

// Corrupt-packet reasons passed to the PacketCorrupt callback.
enum CorruptReason {
  kCorruptDecryptTooShort = 3,
  kCorruptDecryptFailed = 4,
  kCorruptZeroLength = 5,
  kCorruptCrcTooShort = 6,
};

constexpr int kDisconnectReasonUnreachable = 1;
constexpr int kDisconnectReasonCorruptPacket = 15;

struct MethodSlot {
  uintptr_t function;
  int64_t thisAdjust;
  int64_t padding;
};

// Not rebuilt yet: the decrypted-packet handler (opcode dispatch).
void ProcessCookedPacket(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140348390)(c, data, length);
}
void CallbackCrcReject(UdpManager* m, UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpManager*, UdpConnection*, const uint8_t*, int)>(0x14033eb20)(m, c, data, length);
}
void CallbackPacketCorrupt(UdpManager* m, UdpConnection* c, const uint8_t* data, int length, int reason) {
  game::Call<void (*)(UdpManager*, UdpConnection*, const uint8_t*, int, int)>(0x14033ec20)(m, c, data,
                                                                                          length, reason);
}

UdpManager* Manager(UdpConnection* c) { return ConnField<UdpManager*>(c, O::kManager); }
int Status(UdpConnection* c) { return ConnField<int>(c, UdpConnectionInternals::kStatus); }

}  // namespace

// 0x14030d440: ms since `since` by this connection's clock, clamped.
int ConnectionElapsed(UdpConnection* c, int64_t since) {
  int64_t elapsed = ConnectionClock(c) - since;
  return elapsed > INT_MAX ? INT_MAX : static_cast<int>(elapsed);
}

// 0x140346510
void ConnectionUpdateReceiveBuckets(UdpConnection* c) {
  AdvanceBandwidthBuckets(ConnectionClock(c), ConnField<int64_t>(c, 0x300), ConnField<int>(c, 0x30C),
                          &ConnField<int>(c, 0x3B0));
}

// 0x140346f30
void ManagerCountCrcReject(UdpManager* self) {
  self->StatsGuard().Enter();
  self->crcRejects += 1;
  self->StatsGuard().Leave();
}

// 0x140348310. A zero-length receive is Windows reporting an ICMP port
// unreachable for this peer. Depending on params it is ignored, tolerated
// for a retry period, or ends the connection.
void ConnectionPortUnreachable(UdpConnection* c) {
  UdpManager* manager = Manager(c);
  if (!manager->ProcessIcmpErrors()) return;
  if (!manager->ProcessIcmpErrorsDuringNegotiating() && Status(c) == kStatusNegotiating) return;
  if (manager->IcmpErrorRetryPeriod() != 0) {
    int64_t& firstError = ConnField<int64_t>(c, 0x2C0);
    if (firstError == 0) {
      firstError = ConnectionClock(c);
      return;
    }
    if (ConnectionElapsed(c, firstError) < Manager(c)->IcmpErrorRetryPeriod()) return;
  }
  ConnectionDisconnect(c, 0, kDisconnectReasonUnreachable);
}

// 0x140345b80. A corrupt packet on an established connection is reported
// and ends it.
void ConnectionProcessCorruptPacket(UdpConnection* c, const uint8_t* data, int length, int reason) {
  if (Status(c) != kStatusConnected) return;
  ConnField<int64_t>(c, 0x160) += 1;
  UdpManager* manager = Manager(c);
  manager->StatsGuard().Enter();
  manager->corruptPackets += 1;
  manager->StatsGuard().Leave();
  CallbackPacketCorrupt(Manager(c), c, data, length, reason);
  ConnectionDisconnect(c, 0, kDisconnectReasonCorruptPacket);
}

// 0x1403491e0. Every datagram for this connection arrives here.
void ConnectionProcessRawPacket(UdpConnection* c, UdpPacketBuffer* buffer) {
  Ref(c)->VirtualAddRef();
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  const uint8_t* raw = buffer->data;

  if (Manager(c)) {
    if (buffer->length == 0) {
      ConnectionPortUnreachable(c);
    } else {
      if (!(raw[0] == 0 && raw[1] == 0x1D)) ConnField<int64_t>(c, 0x2C8) = 0;
      ConnField<int64_t>(c, 0x2C0) = 0;  // a real packet clears the ICMP error timer
      ConnField<int64_t>(c, 0x240) = ConnectionClock(c);  // last receive
      ConnField<int64_t>(c, 0x108) += 1;                  // packets received
      ConnField<int64_t>(c, 0xF8) += buffer->length;      // bytes received
      ConnectionUpdateReceiveBuckets(c);
      ConnField<int>(c, 0x3B0 + static_cast<size_t>(ConnField<int64_t>(c, 0x300) % 40) * 4) += buffer->length;
      ConnField<int>(c, 0x30C) += buffer->length;

      if (!(raw[0] == 0 && raw[1] == 0x06)) {
        if (!ConnField<uint8_t>(c, O::kInGiveTime) && Manager(c))
          ManagerScheduleConnection(Manager(c), c, 0);

        int length = buffer->length;
        const uint8_t* data = raw;
        bool isProtocolHandshake =
            raw[0] == 0 && (static_cast<uint8_t>(raw[1] - 1) <= 1 || static_cast<uint8_t>(raw[1] - 0x1D) <= 2);
        if (length < 1) {
          ConnectionProcessCorruptPacket(c, raw, length, kCorruptZeroLength);
        } else if (isProtocolHandshake) {
          // Connect (01), confirm (02), unreachable (1D), remap (1E) and
          // terminate (1F) are neither encrypted nor CRC'd.
          ProcessCookedPacket(c, data, length);
        } else if (Status(c) != kStatusNegotiating) {
          bool handled = false;
          int crcBytes = ConnField<int>(c, 0x1F4);
          if (crcBytes > 0) {
            if (length < crcBytes) {
              ConnectionProcessCorruptPacket(c, data, length, kCorruptCrcTooShort);
              handled = true;
            } else {
              const uint8_t* tail = data + (length - crcBytes);
              uint32_t expected = Crc32(data, length - crcBytes, ConnField<uint32_t>(c, O::kEncryptCode));
              uint32_t received = 0;
              switch (crcBytes) {
                case 1: received = tail[0]; expected &= 0xFF; break;
                case 2: received = tail[0] << 8 | tail[1]; expected &= 0xFFFF; break;
                case 3: received = tail[0] << 16 | tail[1] << 8 | tail[2]; expected &= 0xFFFFFF; break;
                case 4:
                  received = static_cast<uint32_t>(tail[0]) << 24 | tail[1] << 16 | tail[2] << 8 | tail[3];
                  break;
              }
              if (received != expected) {
                ConnField<int64_t>(c, 0x110) += 1;  // CRC rejects
                ManagerCountCrcReject(Manager(c));
                CallbackCrcReject(Manager(c), c, raw, buffer->length);
                handled = true;
              } else {
                length -= crcBytes;
              }
            }
          }

          // Undo the encryption passes, last pass first.
          uint8_t buffers[2][0x2000];
          for (int pass = 1; pass >= 0 && !handled; --pass) {
            if (ConnField<int>(c, O::kEncryptMethods + pass * 4) == kEncryptNone) continue;
            uint8_t* out = buffers[pass % 2];
            auto& method = ConnField<MethodSlot>(c, O::kDecryptPasses + pass * sizeof(MethodSlot));
            using CipherFn = int (*)(void*, uint8_t*, const uint8_t*, int);
            auto cipher = reinterpret_cast<CipherFn>(method.function);
            void* self = reinterpret_cast<uint8_t*>(c) + static_cast<int>(method.thisAdjust);
            out[0] = data[0];
            uint8_t* body;
            int produced;
            if (data[0] == 0) {
              if (length <= 1) {
                ConnectionProcessCorruptPacket(c, raw, buffer->length, kCorruptDecryptTooShort);
                handled = true;
                break;
              }
              out[1] = data[1];
              body = out + 2;
              produced = cipher(self, body, data + 2, length - 2);
              if (!Manager(c)) {
                handled = true;
                break;
              }
            } else {
              body = out + 1;
              produced = cipher(self, body, data + 1, length - 1);
            }
            if (produced == -1) {
              ConnectionProcessCorruptPacket(c, raw, buffer->length, kCorruptDecryptFailed);
              handled = true;
              break;
            }
            length = static_cast<int>(body + produced - out);
            data = out;
          }
          if (!handled) ProcessCookedPacket(c, data, length);
        }
      }
    }
  }
  guard.Leave();
  Ref(c)->VirtualRelease();
}

REBUILD_FUNCTION(UdpConnection_Elapsed, 0x14030d440, ConnectionElapsed);
REBUILD_FUNCTION(UdpConnection_UpdateReceiveBuckets, 0x140346510, ConnectionUpdateReceiveBuckets);
REBUILD_FUNCTION(UdpManager_CountCrcReject, 0x140346f30, ManagerCountCrcReject);
REBUILD_FUNCTION(UdpConnection_PortUnreachable, 0x140348310, ConnectionPortUnreachable);
REBUILD_FUNCTION(UdpConnection_ProcessCorruptPacket, 0x140345b80, ConnectionProcessCorruptPacket);
REBUILD_FUNCTION(UdpConnection_ProcessRawPacket, 0x1403491e0, ConnectionProcessRawPacket);

}  // namespace rebuild::udp
