// UdpConnection::ProcessCookedPacket - the UdpLibrary protocol opcode
// dispatcher, run on every packet after CRC check and decryption.
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

// Protocol opcodes (byte after a leading 0x00).
enum Opcode : uint8_t {
  kEscaped = 0x00,
  kConnect = 0x01,
  kConfirm = 0x02,
  kMulti = 0x03,
  kTerminate = 0x05,
  kClockSync = 0x07,
  kClockReflect = 0x08,
  kReliableFirst = 0x09,    // 09-0C reliable data, 0D-10 reliable fragments (channel = (op - 9) % 4)
  kReliableLast = 0x10,
  kAckFirst = 0x11,         // 11-14
  kAckAllFirst = 0x15,      // 15-18
  kGroup = 0x19,
  kOrdered = 0x1A,
  kOrdered2 = 0x1B,
  kUnreachableConnection = 0x1D,
  kRequestRemap = 0x1E,
};

// Disconnect reasons used here.
constexpr int kReasonOtherSideTerminated = 3;
constexpr int kReasonUnreachableConnection = 7;
constexpr int kReasonConnectCodeMismatch = 9;
constexpr int kReasonProtocolMismatch = 16;
// Terminate reasons sent while still negotiating.
constexpr uint16_t kTerminateNewConnectionAttempt = 11;
constexpr uint16_t kTerminateConnectionRefused = 12;
constexpr int kCorruptMultiPacket = 1;
constexpr int kCorruptGroupPacket = 7;

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
         static_cast<uint32_t>(p[2]) << 8 | p[3];
}
uint64_t Be64(const uint8_t* p) { return static_cast<uint64_t>(Be32(p)) << 32 | Be32(p + 4); }
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

// Not rebuilt yet.
void ProcessApplicationPacket(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140345c30)(c, data, length);
}
void SetOtherSideTerminated(UdpConnection* c, bool value) {
  game::Call<void (*)(UdpConnection*, bool)>(0x140349a60)(c, value);
}
int ManagerLocalSyncStampLong(UdpManager* m) { return game::Call<int (*)(UdpManager*)>(0x140347d00)(m); }
uint16_t ManagerLocalSyncStampShort(UdpManager* m) {
  return game::Call<uint16_t (*)(UdpManager*)>(0x140347d70)(m);
}
int SyncStampShortDelta(uint16_t start, uint16_t stop) {
  return game::Call<int (*)(uint16_t, uint16_t)>(0x14034bbc0)(start, stop);
}
void ManagerCountOrderedStale(UdpManager* m) { game::Call<void (*)(UdpManager*)>(0x140346f80)(m); }
void* NewReliableChannel(void* memory, int channel, UdpConnection* c, void* config) {
  return game::Call<void* (*)(void*, int, UdpConnection*, void*)>(0x14034c360)(memory, channel, c, config);
}
void ReliableChannelReliablePacket(void* channel, const uint8_t* data, int length) {
  game::Call<void (*)(void*, const uint8_t*, int)>(0x14034dab0)(channel, data, length);
}
void ReliableChannelAckPacket(void* channel, const uint8_t* data) {
  game::Call<void (*)(void*, const uint8_t*)>(0x1403457e0)(channel, data);
}
void ReliableChannelAckAllPacket(void* channel, const uint8_t* data) {
  game::Call<void (*)(void*, const uint8_t*)>(0x14034cb30)(channel, data);
}
void ConnectionProcessCorruptPacket(UdpConnection* c, const uint8_t* data, int length, int reason) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int, int)>(0x140345b80)(c, data, length, reason);
}
void CallbackConnectComplete(UdpManager* m, UdpConnection* c) {
  game::Call<void (*)(UdpManager*, UdpConnection*)>(0x14033e980)(m, c);
}

UdpManager* Manager(UdpConnection* c) { return ConnField<UdpManager*>(c, O::kManager); }
void*& ReliableChannel(UdpConnection* c, int channel) { return ConnField<void*>(c, 0x1D0 + channel * 8); }

// Copies a NUL-terminated name of at most 31 characters.
void CopyName(char* dest, const char* dest31End, const uint8_t* src) {
  char* out = dest;
  for (char ch = static_cast<char>(*src); ch != 0 && out < dest31End; ch = static_cast<char>(*++src)) *out++ = ch;
  *out = 0;
}

void HandleConnect(UdpConnection* c, const uint8_t* p, int length) {
  int protocolVersion = static_cast<int>(Be32(p + 2));
  uint32_t connectCode = Be32(p + 6);
  int maxRawPacketSize = static_cast<int>(Be32(p + 10));
  char name[32];
  name[0] = 0;
  if (protocolVersion > 2 && length > 14) CopyName(name, name + 31, p + 14);

  if (ConnField<int>(c, UdpConnectionInternals::kStatus) == kStatusNegotiating) {
    SendTerminatePacket(c, connectCode,
                        connectCode == ConnField<uint32_t>(c, O::kConnectCode) ? kTerminateConnectionRefused
                                                                               : kTerminateNewConnectionAttempt);
    return;
  }
  if (connectCode != ConnField<uint32_t>(c, O::kConnectCode)) {
    ConnectionDisconnect(c, 0, kReasonConnectCodeMismatch);
    return;
  }

  ConnField<int>(c, 0x204) = protocolVersion;
  char* storedName = &ConnField<char>(c, 0x208);
  CopyName(storedName, storedName + 0x1F, reinterpret_cast<const uint8_t*>(name));
  if (ConnField<int>(c, 0x200) < maxRawPacketSize) maxRawPacketSize = ConnField<int>(c, 0x200);
  ConnField<int>(c, 0x200) = maxRawPacketSize;

  // Confirm: code, encrypt code, CRC bytes, both encrypt methods, max raw
  // packet size, protocol version 3.
  uint8_t confirm[21];
  confirm[0] = 0x00;
  confirm[1] = kConfirm;
  PutBe32(confirm + 2, ConnField<uint32_t>(c, O::kConnectCode));
  PutBe32(confirm + 6, ConnField<uint32_t>(c, O::kEncryptCode));
  confirm[10] = static_cast<uint8_t>(ConnField<int>(c, 0x1F4));
  confirm[11] = static_cast<uint8_t>(ConnField<int>(c, O::kEncryptMethods));
  confirm[12] = static_cast<uint8_t>(ConnField<int>(c, O::kEncryptMethods + 4));
  PutBe32(confirm + 13, static_cast<uint32_t>(maxRawPacketSize));
  PutBe32(confirm + 17, 3);
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x1403495a0)(c, confirm, sizeof(confirm));

  // A manager configured with a protocol name only talks to the same one.
  const char* expected = reinterpret_cast<const char*>(&Manager(c)->params.At<uint8_t>(0x181));
  if (*expected != 0 && std::strcmp(expected, storedName) != 0)
    ConnectionDisconnect(c, 0, kReasonProtocolMismatch);
}

void HandleConfirm(UdpConnection* c, const uint8_t* p, int length) {
  uint32_t encryptCode = Be32(p + 6);
  int crcBytes = p[10];
  int method1 = p[11];
  int method2 = p[12];
  int maxRawPacketSize = static_cast<int>(Be32(p + 13));
  int protocolVersion = length > 0x11 ? static_cast<int>(Be32(p + 17)) : 0;
  if (ConnField<int>(c, UdpConnectionInternals::kStatus) != kStatusNegotiating ||
      ConnField<uint32_t>(c, O::kConnectCode) != Be32(p + 2))
    return;
  ConnField<uint32_t>(c, O::kEncryptCode) = encryptCode;
  ConnField<int>(c, 0x1F4) = crcBytes;
  ConnField<int>(c, O::kEncryptMethods) = method1;
  ConnField<int>(c, O::kEncryptMethods + 4) = method2;
  ConnField<int>(c, 0x200) = maxRawPacketSize;
  ConnField<int>(c, 0x204) = protocolVersion;
  ConnectionSetupEncryption(c);
  ConnField<int>(c, UdpConnectionInternals::kStatus) = kStatusConnected;
  CallbackConnectComplete(Manager(c), c);
}

// Peer asks for our clock: record its stats and reflect them back with ours.
void HandleClockSync(UdpConnection* c, const uint8_t* p) {
  ConnField<int>(c, 0x170) = static_cast<int>(Be32(p + 8));   // peer average ping
  ConnField<int>(c, 0x178) = static_cast<int>(Be32(p + 16));  // high
  ConnField<int>(c, 0x174) = static_cast<int>(Be32(p + 12));  // low
  ConnField<int>(c, 0x17C) = static_cast<int>(Be32(p + 20));  // last
  ConnField<int>(c, 0x16C) = static_cast<int>(Be32(p + 4));   // master ping
  ConnField<int64_t>(c, 0x190) = ConnField<int64_t>(c, 0x108);
  ConnField<int64_t>(c, 0x188) = ConnField<int64_t>(c, 0x100);
  ConnField<uint64_t>(c, 0x1A0) = Be64(p + 32);  // peer packets received
  ConnField<uint64_t>(c, 0x198) = Be64(p + 24);  // peer packets sent
  if (Manager(c)->lastPollDelta >= 1001) return;  // our timing is too stale to be useful

  uint8_t reflect[40];
  reflect[0] = 0x00;
  reflect[1] = kClockReflect;
  reflect[2] = p[2];
  reflect[3] = p[3];
  PutBe32(reflect + 4, static_cast<uint32_t>(ManagerLocalSyncStampLong(Manager(c))));
  std::memcpy(reflect + 8, p + 24, 16);
  PutBe64(reflect + 24, ConnField<uint64_t>(c, 0x100));
  PutBe64(reflect + 32, ConnField<uint64_t>(c, 0x108));
  PhysicalSend(c, reflect, sizeof(reflect), true);
}

// Our clock sync came back: update ping statistics and, when the sample is
// trustworthy, the estimated offset to the peer's clock.
void HandleClockReflect(UdpConnection* c, const uint8_t* p) {
  uint16_t sentStamp = Be16(p + 2);
  int serverStamp = static_cast<int>(Be32(p + 4));
  ConnField<uint64_t>(c, 0x190) = Be64(p + 16);
  ConnField<uint64_t>(c, 0x188) = Be64(p + 8);
  ConnField<uint64_t>(c, 0x1A0) = Be64(p + 32);
  ConnField<uint64_t>(c, 0x198) = Be64(p + 24);
  UdpManager* manager = Manager(c);
  if (manager->lastPollDelta >= 1001) return;

  int ping = SyncStampShortDelta(sentStamp, ManagerLocalSyncStampShort(manager));
  int& samples = ConnField<int>(c, 0x28C);
  int& total = ConnField<int>(c, 0x288);
  int& low = ConnField<int>(c, 0x290);
  int& high = ConnField<int>(c, 0x294);
  int& masterPing = ConnField<int>(c, 0x29C);
  samples += 1;
  total += ping;
  if (low == 0 || ping < low) low = ping;
  if (high < ping) high = ping;
  ConnField<int>(c, 0x298) = ping;
  int sinceMaster = ConnectionElapsed(c, ConnField<int64_t>(c, 0x2A0));
  if ((ping <= masterPing + 20 || sinceMaster > 120000) && (ping < masterPing * 2 || sinceMaster > 240000)) {
    ConnField<int>(c, 0x284) = (ping / 2 + serverStamp) - ManagerLocalSyncStampLong(Manager(c));
    ConnField<int64_t>(c, 0x2A0) = ConnectionClock(c);
    masterPing = ping;
  }
  ConnField<int>(c, 0x170) = samples < 1 ? 0 : total / samples;
  ConnField<int>(c, 0x178) = high;
  ConnField<int>(c, 0x174) = low;
  ConnField<int>(c, 0x17C) = ping;
  ConnField<int>(c, 0x16C) = masterPing;
}

// 1A / 1B: drop anything not newer than the last sequence (with wraparound;
// a jump of 30000+ counts as stale).
bool AcceptOrdered(UdpConnection* c, const uint8_t* p, size_t lastOffset) {
  uint16_t sequence = Be16(p + 2);
  int diff = static_cast<int>(sequence) - static_cast<int>(ConnField<uint16_t>(c, lastOffset));
  int distance = diff > 0 ? diff : diff + 0x10000;
  if (distance >= 30000) {
    ConnField<int64_t>(c, 0x118) += 1;
    ManagerCountOrderedStale(Manager(c));
    return false;
  }
  ConnField<uint16_t>(c, lastOffset) = sequence;
  return true;
}

}  // namespace

// 0x140348390
void ConnectionProcessCookedPacket(UdpConnection* c, const uint8_t* data, int length) {
  UdpManager* manager = Manager(c);
  if (!manager) return;
  if (data[0] != 0 || length < 2) {
    ProcessApplicationPacket(c, data, length);
    return;
  }

  uint8_t opcode = data[1];
  switch (opcode) {
    case kEscaped:
      ProcessApplicationPacket(c, data + 1, length - 1);
      break;
    case kConnect:
      HandleConnect(c, data, length);
      break;
    case kConfirm:
      HandleConfirm(c, data, length);
      break;
    case kMulti: {
      const uint8_t* end = data + length;
      for (const uint8_t* p = data + 2; p < end;) {
        uint8_t subLength = *p;
        const uint8_t* sub = p + 1;
        p = sub + subLength;
        if (p > end) {
          ConnectionProcessCorruptPacket(c, data, length, kCorruptMultiPacket);
          if (!Manager(c)) return;
        } else {
          ConnectionProcessCookedPacket(c, sub, subLength);
        }
      }
      break;
    }
    case kTerminate: {
      uint32_t code = Be32(data + 2);
      if (length > 7) ConnField<int>(c, 0x1C4) = Be16(data + 6);  // the peer's reason
      if (ConnField<uint32_t>(c, O::kConnectCode) == code) {
        SetOtherSideTerminated(c, true);
        ConnectionDisconnect(c, 0, kReasonOtherSideTerminated);
      }
      break;
    }
    case kClockSync:
      HandleClockSync(c, data);
      break;
    case kClockReflect:
      HandleClockReflect(c, data);
      break;
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: {
      int channel = (opcode - kReliableFirst) % 4;
      void*& reliable = ReliableChannel(c, channel);
      if (!reliable) {
        void* memory = soeutil::Allocate(0x158);
        reliable = memory ? NewReliableChannel(memory, channel, c,
                                               reinterpret_cast<uint8_t*>(manager) + 0xAC + channel * 0x34)
                          : nullptr;
      }
      ReliableChannelReliablePacket(reliable, data, length);
      break;
    }
    case 0x11: case 0x12: case 0x13: case 0x14:
      if (void* reliable = ReliableChannel(c, opcode - kAckFirst)) ReliableChannelAckPacket(reliable, data);
      break;
    case 0x15: case 0x16: case 0x17: case 0x18:
      if (void* reliable = ReliableChannel(c, opcode - kAckAllFirst)) ReliableChannelAckAllPacket(reliable, data);
      break;
    case kGroup: {
      const uint8_t* end = data + length;
      for (const uint8_t* p = data + 2; p < end;) {
        uint32_t subLength;
        p += GetVariableValue(p, &subLength);
        if (p > end || static_cast<uint32_t>(end - p) < subLength) {
          ConnectionProcessCorruptPacket(c, data, length, kCorruptGroupPacket);
          return;
        }
        ConnectionProcessCookedPacket(c, p, static_cast<int>(subLength));
        p += subLength;
      }
      break;
    }
    case kOrdered:
      if (AcceptOrdered(c, data, 0x270)) ProcessApplicationPacket(c, data + 4, length - 4);
      break;
    case kOrdered2:
      if (AcceptOrdered(c, data, 0x272)) ProcessApplicationPacket(c, data + 4, length - 4);
      break;
    case kUnreachableConnection:
      // The peer lost us (e.g. our NAT mapping changed): ask it to remap
      // for up to 5 seconds before giving up.
      if (manager->AllowAddressRemapping()) {
        int64_t& firstUnreachable = ConnField<int64_t>(c, 0x2C8);
        if (firstUnreachable == 0) firstUnreachable = ConnectionClock(c);
        if (ConnectionElapsed(c, firstUnreachable) < 5000) {
          uint8_t remap[10] = {0x00, kRequestRemap};
          PutBe32(remap + 2, ConnField<uint32_t>(c, O::kConnectCode));
          PutBe32(remap + 6, ConnField<uint32_t>(c, O::kEncryptCode));
          game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x1403495a0)(c, remap, sizeof(remap));
          return;
        }
      }
      ConnectionDisconnect(c, 0, kReasonUnreachableConnection);
      break;
    default:
      break;
  }
}

REBUILD_FUNCTION(UdpConnection_ProcessCookedPacket, 0x140348390, ConnectionProcessCookedPacket);

}  // namespace rebuild::udp
