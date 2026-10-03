#pragma once

#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "udp/UdpManager.h"

// UdpLibrary::UdpReliableChannel (0x158 bytes): one of a connection's four
// reliable streams (opcodes 09-18). Sequenced delivery with a resend ring,
// an out-of-order receive window, TCP-style congestion control and a
// smoothed round-trip estimate. Addressed by named offsets for now.
namespace rebuild::udp {

struct UdpReliableChannel;

// One slot of the outgoing ring (0x30 bytes, params "max instandingPackets" of them).
struct ReliableOutgoingEntry {
  int64_t firstSendTime;  // +0x00
  int64_t lastSendTime;   // +0x08 equal to firstSendTime until a resend
  int64_t unknown10;      // +0x10
  UdpRefCount* parent;    // +0x18 the LogicalPacket this piece came from
  uint8_t* data;          // +0x20 null = slot free / acked
  int length;             // +0x28
};
static_assert(sizeof(ReliableOutgoingEntry) == 0x30);

// One slot of the incoming out-of-order window (0x10 bytes).
struct ReliableIncomingEntry {
  UdpRefCount* packet;  // +0x00 LogicalPacket holding the payload
  int mode;             // +0x08 0 reliable, 1 fragment, 2 already processed
};
static_assert(sizeof(ReliableIncomingEntry) == 0x10);

struct ReliableOffsets {
  // +0x00..+0x33: copy of the manager's UdpReliableConfig for this channel
  static constexpr size_t kMaxOutstandingPackets = 0x04;  // outgoing ring size (<= 30000)
  static constexpr size_t kWindowSize = 0x08;             // incoming window size
  static constexpr size_t kResendDelayCap = 0x20;
  static constexpr size_t kProcessOnArrival = 0x30;       // bool: handle in-order-less packets immediately
  static constexpr size_t kAckDeduping = 0x32;            // bool
  static constexpr size_t kConnection = 0x38;
  static constexpr size_t kLastAckedSendTime = 0x40;
  static constexpr size_t kResendTimer = 0x50;
  static constexpr size_t kChannelNumber = 0x68;
  static constexpr size_t kNextOutgoingId = 0x70;
  static constexpr size_t kOldestUnackedId = 0x78;
  static constexpr size_t kBytesInFlight = 0x80;
  static constexpr size_t kFragmentAssembly = 0x88;  // non-null while a fragmented packet is being rebuilt
  static constexpr size_t kAveragePing = 0x98;
  static constexpr size_t kMaxDataBytes = 0x9C;
  static constexpr size_t kOutgoingRing = 0xA8;
  static constexpr size_t kCongestionWindow = 0xD4;
  static constexpr size_t kSlowStartThreshold = 0xD8;
  static constexpr size_t kCongestionControl = 0xE0;  // bool
  static constexpr size_t kNextIncomingId = 0xE8;
  static constexpr size_t kIncomingWindow = 0xF0;
  static constexpr size_t kPendingAck = 0x118;  // points into the connection's multi-buffer
  static constexpr size_t kStatOutOfOrder = 0x120;
  static constexpr size_t kStatDuplicate = 0x124;
  static constexpr size_t kStatOverflow = 0x128;
  static constexpr size_t kQueuedIncoming = 0x134;
  static constexpr size_t kMaxQueuedIncoming = 0x138;
};

template <class T>
T& RelField(UdpReliableChannel* channel, size_t offset) {
  return game::Field<T>(channel, offset);
}

void ReliableAckInternal(UdpReliableChannel* self, int64_t id);                         // 0x14034c9e0
void ReliableAckPacket(UdpReliableChannel* self, const uint8_t* data, int length);       // 0x1403457e0
void ReliableAckAllPacket(UdpReliableChannel* self, const uint8_t* data, int length);    // 0x14034cb30
void ReliableReliablePacket(UdpReliableChannel* self, const uint8_t* data, int length);  // 0x14034dab0

}  // namespace rebuild::udp
