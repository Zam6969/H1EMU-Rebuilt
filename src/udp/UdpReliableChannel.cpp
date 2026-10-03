#include "udp/UdpReliableChannel.h"

#include <cstring>

#include "core/crt.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpConnection.h"
#include "udp/UdpPlatformDriver.h"

namespace rebuild::udp {
namespace {

using R = ReliableOffsets;
using O = ConnectionOffsets;

constexpr int kCorruptAckTooShort = 11;
constexpr int kCorruptReliableTooShort = 2;
constexpr int kModeProcessed = 2;

UdpConnection* Connection(UdpReliableChannel* c) { return RelField<UdpConnection*>(c, R::kConnection); }
UdpManager* Manager(UdpReliableChannel* c) { return ConnField<UdpManager*>(Connection(c), O::kManager); }
ReliableOutgoingEntry& Outgoing(UdpReliableChannel* c, int64_t id) {
  return RelField<ReliableOutgoingEntry*>(c, R::kOutgoingRing)[static_cast<int>(id % RelField<int>(c, R::kMaxOutstandingPackets))];
}
ReliableIncomingEntry& Incoming(UdpReliableChannel* c, int64_t id) {
  return RelField<ReliableIncomingEntry*>(c, R::kIncomingWindow)[static_cast<int>(id % RelField<int>(c, R::kWindowSize))];
}

// Widens a 16-bit wire sequence to the 64-bit id nearest `reference`
// (never above it for acks).
int64_t ExpandAckId(uint16_t wire, int64_t reference) {
  int64_t id = static_cast<int64_t>(wire) | (reference & ~0xFFFFll);
  return id <= reference ? id : id - 0x10000;
}

int PacketLength(UdpRefCount* p) { return reinterpret_cast<int (*)(UdpRefCount*)>(p->vtable[7])(p); }
const uint8_t* PacketData(UdpRefCount* p) {
  return reinterpret_cast<const uint8_t* (*)(UdpRefCount*)>(p->vtable[6])(p);
}

// Not rebuilt yet.
void ProcessCorruptPacket(UdpConnection* c, const uint8_t* data, int length, int reason) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int, int)>(0x140345b80)(c, data, length, reason);
}
void ProcessCookedPacket(UdpConnection* c, const uint8_t* data, int length) {
  game::Call<void (*)(UdpConnection*, const uint8_t*, int)>(0x140348390)(c, data, length);
}
LogicalPacket* CreatePacket(UdpManager* m, const uint8_t* data, int length) {
  using Fn = LogicalPacket* (*)(UdpManager*, const uint8_t*, int, const uint8_t*, int);
  return game::Call<Fn>(0x14033f340)(m, data, length, nullptr, 0);
}

void CountManagerStat(UdpManager* m, int64_t& stat) {
  m->StatsGuard().Enter();
  stat += 1;
  m->StatsGuard().Leave();
}

}  // namespace

// 0x14034cbf0: UdpMisc::Clock(), read through a function-local static
// UdpPlatformDriver that lives in the game's .data.
int64_t MiscClock() {
  auto* driver = reinterpret_cast<UdpPlatformDriver*>(0x142b13510);
  crt::ThreadSafeStatic(reinterpret_cast<int*>(0x142b17570), [driver] {
    game::Call<UdpPlatformDriver* (*)(UdpPlatformDriver*)>(0x140349f40)(driver);  // constructor
    crt::Atexit(0x142016150);
  });
  return static_cast<int64_t>(game::Call<uint64_t (*)(UdpPlatformDriver*)>(0x14034a410)(driver));
}

// 0x14034d650. Hands a reliable payload up: a whole packet goes straight to
// the dispatcher; fragments (mode 1) are reassembled first. The first
// fragment starts with the total size (BE32).
void ProcessDeliveredPacket(UdpReliableChannel* c, int mode, const uint8_t* data, int length) {
  if (mode == 0) {
    if (RelField<uint8_t*>(c, R::kFragmentAssembly))
      ProcessCorruptPacket(Connection(c), data, length, 10);  // whole packet inside a fragment run
    else
      ProcessCookedPacket(Connection(c), data, length);
    return;
  }
  if (mode != 1) return;

  uint8_t*& assembly = RelField<uint8_t*>(c, R::kFragmentAssembly);
  int& received = RelField<int>(c, 0x90);
  int& total = RelField<int>(c, 0x94);
  if (!assembly) {
    if (length < 4) {
      ProcessCorruptPacket(Connection(c), data, length, 9);
      return;
    }
    total = static_cast<int>(static_cast<uint32_t>(data[0]) << 24 | data[1] << 16 | data[2] << 8 | data[3]);
    if (total < 1) {
      ProcessCorruptPacket(Connection(c), data, length, 9);
      return;
    }
    if (total > Manager(c)->params.At<int>(0x17C)) {  // larger than the allowed big-packet size
      ProcessCorruptPacket(Connection(c), data, length, 8);
      return;
    }
    assembly = static_cast<uint8_t*>(soeutil::AllocateArray(static_cast<size_t>(total)));
    data += 4;
    length -= 4;
    received = 0;
    if (RelField<int>(c, 0x13C) < total) RelField<int>(c, 0x13C) = total;  // largest big packet
    RelField<int64_t>(c, 0x60) = MiscClock();                                // assembly started
  }
  int take = total - received;
  if (length < take) take = length;
  std::memcpy(assembly + received, data, take);
  received += take;
  if (total == received) {
    int64_t took = MiscClock() - RelField<int64_t>(c, 0x60);
    int elapsed = took > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(took);
    if (RelField<int>(c, 0x140) < elapsed) RelField<int>(c, 0x140) = elapsed;
    RelField<int64_t>(c, 0x150) += 1;
    RelField<int64_t>(c, 0x148) += elapsed;
    ProcessCookedPacket(Connection(c), assembly, received);
    soeutil::FreeArray(assembly);
    RelField<int64_t>(c, 0x90) = 0;  // clears received and total together
    assembly = nullptr;
  }
}

// 0x140345930. Queues a small packet into the connection's 00 03
// multi-packet buffer (1-byte lengths), sending directly when it cannot be
// combined. Returns where the packet's bytes landed in the buffer, or null.
uint8_t* BufferedSend(UdpConnection* c, const uint8_t* data, int length, const uint8_t* data2, int length2,
                      bool writable) {
  UdpManager* manager = ConnField<UdpManager*>(c, O::kManager);
  if (!manager) return nullptr;
  uint8_t*& cursor = ConnField<uint8_t*>(c, 0x258);
  uint8_t* start = ConnField<uint8_t*>(c, 0x250);
  int pending = static_cast<int>(cursor - start);
  int maxRaw = ConnField<int>(c, 0x200);
  int limit = manager->params.At<int>(0x40) <= maxRaw ? manager->params.At<int>(0x40) : maxRaw;
  int total = length + length2;

  if (total > 0xFF || limit < total + 3) {
    if (pending > 2) FlushChannels(c);
    if (data2) {
      std::memcpy(start, data, length);
      std::memcpy(start + length, data2, length2);
      data = start;
      writable = true;
      length = total;
    }
    PhysicalSend(c, data, length, writable);
    return nullptr;
  }

  bool startNew = true;
  if (maxRaw - ConnField<int>(c, O::kEncryptExpansionBytes) - ConnField<int>(c, 0x1F4) < total + 1 + pending)
    FlushChannels(c);
  else if (pending != 0)
    startNew = false;
  if (startNew) {
    *cursor++ = 0x00;
    *cursor++ = 0x03;
    ConnField<int64_t>(c, 0x230) = ConnectionClock(c);  // buffer opened
    if (!ConnField<uint8_t>(c, O::kInGiveTime) && ConnField<UdpManager*>(c, O::kManager))
      ManagerScheduleConnection(ConnField<UdpManager*>(c, O::kManager), c, 0);
  }
  *cursor++ = static_cast<uint8_t>(total);
  uint8_t* placed = cursor;
  std::memcpy(cursor, data, length);
  cursor += length;
  if (data2) {
    std::memcpy(cursor, data2, length2);
    cursor += length2;
  }
  if (limit <= cursor - start) {
    FlushChannels(c);
    placed = nullptr;
  }
  return placed;
}

// 0x14034d5c0 / 0x14034d570
void ManagerCountReliableOutOfOrder(UdpManager* m) { CountManagerStat(m, game::Field<int64_t>(m, 0x380)); }
void ManagerCountReliableDuplicate(UdpManager* m) { CountManagerStat(m, game::Field<int64_t>(m, 0x388)); }

// 0x14034c9e0. The peer acknowledged reliable packet `id`: update the
// congestion window and ping estimate, free the slot, and slide the window.
void ReliableAckInternal(UdpReliableChannel* c, int64_t id) {
  if (id < RelField<int64_t>(c, R::kOldestUnackedId) || id >= RelField<int64_t>(c, R::kNextOutgoingId)) return;
  ReliableOutgoingEntry& entry = Outgoing(c, id);
  if (!entry.data) return;

  RelField<int64_t>(c, R::kResendTimer) = 0;
  if (RelField<uint8_t>(c, R::kWindowFullLastPass)) {
    int window = RelField<int>(c, R::kCongestionWindow);
    int maxData = RelField<int>(c, R::kMaxDataBytes);
    if (window < RelField<int>(c, R::kSlowStartThreshold)) {
      RelField<int>(c, R::kCongestionWindow) = maxData + window;  // slow start
    } else {
      int growth = (maxData * maxData) / window;  // congestion avoidance
      RelField<int>(c, R::kCongestionWindow) = window + (growth > 4 ? growth : 4);
    }
  }
  // Only packets sent exactly once give an unambiguous round-trip sample.
  if (entry.lastSendTime == entry.firstSendTime) {
    int sample = ConnectionElapsed(Connection(c), entry.firstSendTime) + RelField<int>(c, R::kAveragePing) * 3;
    RelField<int>(c, R::kAveragePing) = sample / 4;
  }
  RelField<int64_t>(c, R::kLastAckedSendTime) = entry.firstSendTime;
  RelField<int>(c, R::kBytesInFlight) -= entry.length;
  entry.length = 0;
  entry.data = nullptr;
  entry.parent->VirtualRelease();
  entry.parent = nullptr;

  int64_t& oldest = RelField<int64_t>(c, R::kOldestUnackedId);
  while (oldest < RelField<int64_t>(c, R::kNextOutgoingId) && !Outgoing(c, oldest).data) ++oldest;
}

// 0x1403457e0: 00 11+ch <id16>
void ReliableAckPacket(UdpReliableChannel* c, const uint8_t* data, int length) {
  if (length < 4) {
    ProcessCorruptPacket(Connection(c), data, length, kCorruptAckTooShort);
    return;
  }
  ReliableAckInternal(c, ExpandAckId(static_cast<uint16_t>(data[2] << 8 | data[3]),
                                     RelField<int64_t>(c, R::kNextOutgoingId)));
}

// 0x14034cb30: 00 15+ch <id16> acknowledges everything up to and including id.
// An "ack all" for something already acked means a resend was needless,
// so the resend delay backs off by 400 ms (capped).
void ReliableAckAllPacket(UdpReliableChannel* c, const uint8_t* data, int length) {
  if (length < 4) {
    ProcessCorruptPacket(Connection(c), data, length, kCorruptAckTooShort);
    return;
  }
  int64_t oldest = RelField<int64_t>(c, R::kOldestUnackedId);
  int64_t through = ExpandAckId(static_cast<uint16_t>(data[2] << 8 | data[3]),
                                RelField<int64_t>(c, R::kNextOutgoingId));
  if (through < oldest) {
    int& delay = RelField<int>(c, R::kAveragePing);
    delay += 400;
    if (RelField<int>(c, R::kResendDelayCap) <= delay) delay = RelField<int>(c, R::kResendDelayCap);
    return;
  }
  for (int64_t id = oldest; id <= through; ++id) ReliableAckInternal(c, id);
}

// 0x14034dab0: 00 09+ch <id16> <payload> (or 0D+ch for a fragment).
void ReliableReliablePacket(UdpReliableChannel* c, const uint8_t* data, int length) {
  if (length <= 4) {
    ProcessCorruptPacket(Connection(c), data, length, kCorruptReliableTooShort);
    return;
  }
  int64_t expected = RelField<int64_t>(c, R::kNextIncomingId);
  int64_t id = static_cast<int64_t>(data[2] << 8 | data[3]) | (expected & ~0xFFFFll);
  if (id < expected - 30000) id += 0x10000;
  if (id > expected + 30000) id -= 0x10000;

  // Beyond the receive window: drop without acking so the peer resends.
  if (id >= expected + RelField<int>(c, R::kWindowSize)) {
    RelField<int>(c, R::kStatOverflow) += 1;
    ConnField<int64_t>(Connection(c), 0x130) += 1;
    CountManagerStat(Manager(c), game::Field<int64_t>(Manager(c), 0x390));
    return;
  }

  bool duplicate = true;
  if (id >= expected) {
    int mode = (data[1] - 9) / 4;  // 0 reliable, 1 fragment
    if (id == expected) {
      // In order: deliver it, then everything queued behind it.
      ProcessDeliveredPacket(c, mode, data + 4, length - 4);
      int64_t& next = RelField<int64_t>(c, R::kNextIncomingId);
      ++next;
      while (Incoming(c, next).packet) {
        ReliableIncomingEntry& queued = Incoming(c, next);
        if (queued.mode != kModeProcessed) {
          int queuedLength = PacketLength(queued.packet);
          ProcessDeliveredPacket(c, queued.mode, PacketData(queued.packet), queuedLength);
        }
        queued.packet->VirtualRelease();
        queued.packet = nullptr;
        ++next;
        RelField<int>(c, R::kQueuedIncoming) -= 1;
      }
      duplicate = false;
    } else {
      // Early: park it in the window.
      RelField<int>(c, R::kStatOutOfOrder) += 1;
      ConnField<int64_t>(Connection(c), 0x120) += 1;
      ManagerCountReliableOutOfOrder(Manager(c));
      ReliableIncomingEntry& slot = Incoming(c, id);
      if (!slot.packet) {
        slot.mode = mode;
        slot.packet = CreatePacket(Manager(c), data + 4, length - 4);
        int& queued = RelField<int>(c, R::kQueuedIncoming);
        queued += 1;
        if (RelField<int>(c, R::kMaxQueuedIncoming) < queued) RelField<int>(c, R::kMaxQueuedIncoming) = queued;
        if (mode == 0 && RelField<uint8_t>(c, R::kProcessOnArrival)) {
          int parkedLength = PacketLength(slot.packet);
          const uint8_t* parked = PacketData(slot.packet);
          if (RelField<void*>(c, R::kFragmentAssembly))
            ProcessCorruptPacket(Connection(c), parked, parkedLength, mode + 10);
          else
            ProcessCookedPacket(Connection(c), parked, parkedLength);
          slot.mode = kModeProcessed;
        }
        duplicate = false;
      }
    }
  }
  if (duplicate) {
    RelField<int>(c, R::kStatDuplicate) += 1;
    ConnField<int64_t>(Connection(c), 0x128) += 1;
    ManagerCountReliableDuplicate(Manager(c));
  }

  // Ack: "ack all up to next-1" once we are past it, otherwise ack this id.
  uint8_t ack[4];
  bool ackAll = RelField<int64_t>(c, R::kNextIncomingId) > id;
  uint16_t ackId;
  ack[0] = 0;
  if (ackAll) {
    ack[1] = static_cast<uint8_t>(RelField<uint8_t>(c, R::kChannelNumber) + 0x15);
    ackId = static_cast<uint16_t>(RelField<uint16_t>(c, R::kNextIncomingId) - 1);
  } else {
    ack[1] = static_cast<uint8_t>(RelField<uint8_t>(c, R::kChannelNumber) + 0x11);
    ackId = static_cast<uint16_t>(id);
  }
  ack[2] = static_cast<uint8_t>(ackId >> 8);
  ack[3] = static_cast<uint8_t>(ackId);
  uint8_t*& pending = RelField<uint8_t*>(c, R::kPendingAck);
  if (pending && RelField<uint8_t>(c, R::kAckDeduping) && ackAll) {
    // An ack-all already waiting in the outgoing buffer is updated in place.
    std::memcpy(pending, ack, 4);
    return;
  }
  uint8_t* sent = BufferedSend(Connection(c), ack, 4, nullptr, 0, true);
  if (!pending) pending = sent;
}

REBUILD_FUNCTION(UdpManager_CountReliableOutOfOrder, 0x14034d5c0, ManagerCountReliableOutOfOrder);
REBUILD_FUNCTION(UdpManager_CountReliableDuplicate, 0x14034d570, ManagerCountReliableDuplicate);
REBUILD_FUNCTION(UdpReliableChannel_AckInternal, 0x14034c9e0, ReliableAckInternal);
REBUILD_FUNCTION(UdpReliableChannel_AckPacket, 0x1403457e0, ReliableAckPacket);
REBUILD_FUNCTION(UdpReliableChannel_AckAllPacket, 0x14034cb30, ReliableAckAllPacket);
REBUILD_FUNCTION(UdpReliableChannel_ReliablePacket, 0x14034dab0, ReliableReliablePacket);
REBUILD_FUNCTION(UdpReliableChannel_ProcessDeliveredPacket, 0x14034d650, ProcessDeliveredPacket);
REBUILD_FUNCTION(UdpConnection_BufferedSend, 0x140345930, BufferedSend);
REBUILD_FUNCTION(UdpMisc_Clock, 0x14034cbf0, MiscClock);

}  // namespace rebuild::udp
