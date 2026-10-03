// UdpReliableChannel: construction, the send queue (coalescing and
// fragmentation) and GiveTime, the resend / congestion-control engine.
#include <climits>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"
#include "udp/UdpConnection.h"
#include "udp/UdpReliableChannel.h"

namespace rebuild::udp {
namespace {

using R = ReliableOffsets;
using O = ConnectionOffsets;

// Config (UdpReliableConfig copy at +0x00) fields used here.
constexpr size_t kCfgMaxInstandingBytes = 0x00;
constexpr size_t kCfgMaxDataBytes = 0x0C;
constexpr size_t kCfgFragmentSize = 0x10;
constexpr size_t kCfgTrickleRate = 0x14;      // minimum ms between sends (0 = off)
constexpr size_t kCfgResendDelayBase = 0x18;
constexpr size_t kCfgResendDelayPercent = 0x1C;
constexpr size_t kCfgCongestionMinimum = 0x24;
constexpr size_t kCfgInitialWindowMinimum = 0x28;
constexpr size_t kCfgToleranceLossCount = 0x2C;
constexpr size_t kCfgCoalesce = 0x31;

constexpr size_t kLastSendTime = 0x48;
constexpr size_t kNextNeededTime = 0x50;
constexpr size_t kLastCongestionChange = 0x58;
constexpr size_t kQueuedBytes = 0x84;
constexpr size_t kFragmentOffset = 0xA0;
constexpr size_t kInitialWindow = 0xD0;
constexpr size_t kMinimumWindow = 0xDC;
constexpr size_t kCoalescePacket = 0xF8;
constexpr size_t kCoalesceStart = 0x100;
constexpr size_t kCoalesceCursor = 0x108;
constexpr size_t kCoalesceCount = 0x110;
constexpr size_t kCoalesceLimit = 0x114;
constexpr size_t kStatResentLost = 0x12C;
constexpr size_t kStatResentTimeout = 0x130;

constexpr int kDisconnectReliableTooOld = 8;

UdpConnection* Connection(UdpReliableChannel* c) { return RelField<UdpConnection*>(c, R::kConnection); }
UdpManager* Manager(UdpReliableChannel* c) { return ConnField<UdpManager*>(Connection(c), O::kManager); }
int Cfg(UdpReliableChannel* c, size_t offset) { return RelField<int>(c, offset); }

UdpLinkedList<LogicalPacket>& Queue(UdpReliableChannel* c) {
  return RelField<UdpLinkedList<LogicalPacket>>(c, 0xB0);
}
ReliableOutgoingEntry* Ring(UdpReliableChannel* c) { return RelField<ReliableOutgoingEntry*>(c, R::kOutgoingRing); }
ReliableOutgoingEntry& Outgoing(UdpReliableChannel* c, int64_t id) {
  return Ring(c)[static_cast<int>(id % Cfg(c, R::kMaxOutstandingPackets))];
}

int Clamp(int64_t value) { return value > INT_MAX ? INT_MAX : static_cast<int>(value); }

int PacketLength(UdpRefCount* p) { return reinterpret_cast<int (*)(UdpRefCount*)>(p->vtable[7])(p); }
uint8_t* PacketData(UdpRefCount* p) { return reinterpret_cast<uint8_t* (*)(UdpRefCount*)>(p->vtable[5])(p); }
uint8_t* PacketDataConst(UdpRefCount* p) { return reinterpret_cast<uint8_t* (*)(UdpRefCount*)>(p->vtable[6])(p); }
void PacketSetLength(UdpRefCount* p, int n) { reinterpret_cast<void (*)(UdpRefCount*, int)>(p->vtable[8])(p, n); }

LogicalPacket* CreatePacket(UdpManager* m, const uint8_t* data, int length, const uint8_t* data2, int length2) {
  using Fn = LogicalPacket* (*)(UdpManager*, const uint8_t*, int, const uint8_t*, int);
  return game::Call<Fn>(0x14033f340)(m, data, length, data2, length2);
}
uint8_t* BufferedSend(UdpConnection* c, const uint8_t* data, int length, const uint8_t* data2, int length2,
                      bool writable) {
  using Fn = uint8_t* (*)(UdpConnection*, const uint8_t*, int, const uint8_t*, int, bool);
  return game::Call<Fn>(0x140345930)(c, data, length, data2, length2, writable);
}

void CountManager(UdpManager* m, size_t offset) {
  m->StatsGuard().Enter();
  game::Field<int64_t>(m, offset) += 1;
  m->StatsGuard().Leave();
}

void ScheduleOwner(UdpConnection* conn) {
  if (!ConnField<uint8_t>(conn, O::kInGiveTime) && ConnField<UdpManager*>(conn, O::kManager))
    ManagerScheduleConnection(ConnField<UdpManager*>(conn, O::kManager), conn, 0);
}

}  // namespace

// 0x14034c340 / 0x14034c350: ring element constructors.
ReliableIncomingEntry* IncomingEntryConstruct(ReliableIncomingEntry* e) {
  e->packet = nullptr;
  e->mode = 0;
  return e;
}
ReliableOutgoingEntry* OutgoingEntryConstruct(ReliableOutgoingEntry* e) {
  e->parent = nullptr;
  *reinterpret_cast<int*>(&e->unknown10) = 0;
  return e;
}

// 0x14034c6c0 / 0x14034c6f0: ring element destructors.
void IncomingEntryDestruct(ReliableIncomingEntry* e) {
  if (e->packet) e->packet->VirtualRelease();
}
void OutgoingEntryDestruct(ReliableOutgoingEntry* e) {
  if (e->parent) e->parent->VirtualRelease();
}

namespace {

template <class T, void (*Destruct)(T*)>
void DeleteRing(T* ring) {
  if (!ring) return;
  auto* header = reinterpret_cast<uint64_t*>(ring) - 1;
  for (uint64_t i = *header; i-- > 0;) Destruct(&ring[i]);  // eh vector destructor runs back to front
  soeutil::FreeArraySized(header, *header * sizeof(T) + 8);
}

template <class T, T* (*Construct)(T*)>
T* NewRing(int count) {
  uint64_t bytes = static_cast<uint64_t>(static_cast<int64_t>(count)) * sizeof(T);
  if (static_cast<int64_t>(count) < 0 || bytes / sizeof(T) != static_cast<uint64_t>(static_cast<int64_t>(count)))
    bytes = ~0ull;
  uint64_t total = bytes > ~0ull - 8 ? ~0ull : bytes + 8;
  auto* header = static_cast<uint64_t*>(soeutil::AllocateArray(total));
  if (!header) return nullptr;
  *header = static_cast<uint64_t>(static_cast<int64_t>(count));
  T* ring = reinterpret_cast<T*>(header + 1);
  for (int i = 0; i < count; ++i) Construct(&ring[i]);
  return ring;
}

}  // namespace

// 0x14034c870 / 0x14034c910: the entries' vector deleting destructors.
ReliableIncomingEntry* IncomingEntryVectorDelete(ReliableIncomingEntry* e, unsigned flags) {
  if (!(flags & 2)) {
    IncomingEntryDestruct(e);
    if (flags & 1) soeutil::Free(e, sizeof(*e));
    return e;
  }
  auto* header = reinterpret_cast<uint64_t*>(e) - 1;
  for (uint64_t i = *header; i-- > 0;) IncomingEntryDestruct(&e[i]);
  if (flags & 1) soeutil::FreeArraySized(header, *header * sizeof(*e) + 8);
  return reinterpret_cast<ReliableIncomingEntry*>(header);
}
ReliableOutgoingEntry* OutgoingEntryVectorDelete(ReliableOutgoingEntry* e, unsigned flags) {
  if (!(flags & 2)) {
    OutgoingEntryDestruct(e);
    if (flags & 1) soeutil::Free(e, sizeof(*e));
    return e;
  }
  auto* header = reinterpret_cast<uint64_t*>(e) - 1;
  for (uint64_t i = *header; i-- > 0;) OutgoingEntryDestruct(&e[i]);
  if (flags & 1) soeutil::FreeArraySized(header, *header * sizeof(*e) + 8);
  return reinterpret_cast<ReliableOutgoingEntry*>(header);
}

// 0x14034c360
UdpReliableChannel* ReliableConstruct(UdpReliableChannel* c, int channel, UdpConnection* connection,
                                      const UdpReliableConfig* config) {
  auto& queue = Queue(c);
  queue.vtable = reinterpret_cast<void**>(0x142052bb8);  // UdpLinkedList<LogicalPacket>
  queue.first = nullptr;
  queue.last = nullptr;
  queue.linkOffset = 0x10;
  queue.count = 0;
  RelField<UdpConnection*>(c, R::kConnection) = connection;
  RelField<int>(c, R::kChannelNumber) = channel;
  std::memcpy(c, config, sizeof(UdpReliableConfig));
  if (Cfg(c, R::kMaxOutstandingPackets) > 30000) RelField<int>(c, R::kMaxOutstandingPackets) = 30000;
  RelField<int>(c, R::kAveragePing) = 800;
  RelField<int64_t>(c, kLastSendTime) = 0;

  int maxData = Cfg(c, kCfgMaxDataBytes);
  int connectionMax = ConnField<int>(connection, 0x200);
  if (maxData == 0 || connectionMax < maxData) maxData = connectionMax;
  maxData = maxData - ConnField<int>(connection, O::kEncryptExpansionBytes) - ConnField<int>(connection, 0x1F4) - 4;
  RelField<int>(c, R::kMaxDataBytes) = maxData;
  if (Cfg(c, kCfgFragmentSize) != 0 && Cfg(c, kCfgFragmentSize) < maxData)
    RelField<int>(c, R::kMaxDataBytes) = Cfg(c, kCfgFragmentSize);
  RelField<int>(c, kCoalesceLimit) = -1;
  if (RelField<uint8_t>(c, kCfgCoalesce)) RelField<int>(c, kCoalesceLimit) = RelField<int>(c, R::kMaxDataBytes) - 5;

  static constexpr size_t kZeroed[] = {0xE8, 0x70, 0x78, 0x80, 0xF8, 0x100, 0x108, 0x118,
                                       0x120, 0x128, 0x130, 0x138, 0x148, 0x150, 0x58};
  for (size_t offset : kZeroed) RelField<int64_t>(c, offset) = 0;
  RelField<int>(c, kCoalesceCount) = 0;
  RelField<int>(c, 0x140) = 0;

  int mss = RelField<int>(c, R::kMaxDataBytes);
  int minimum = Cfg(c, kCfgCongestionMinimum);
  RelField<int>(c, kMinimumWindow) = minimum > mss ? minimum : mss;
  // RFC 3390 initial window: min(4*MSS, max(2*MSS, 4380)), then raised to
  // the configured and the minimum window.
  int initial = 2 * mss >= 0x111C ? 2 * mss : 0x111C;
  if (initial >= 4 * mss) initial = 4 * mss;
  if (initial < Cfg(c, kCfgInitialWindowMinimum)) initial = Cfg(c, kCfgInitialWindowMinimum);
  if (initial < RelField<int>(c, kMinimumWindow)) initial = RelField<int>(c, kMinimumWindow);
  RelField<int>(c, kInitialWindow) = initial;
  int ssthresh = Cfg(c, R::kMaxOutstandingPackets) * mss;
  RelField<int>(c, R::kSlowStartThreshold) =
      Cfg(c, kCfgMaxInstandingBytes) < ssthresh ? Cfg(c, kCfgMaxInstandingBytes) : ssthresh;
  RelField<int>(c, R::kCongestionWindow) = initial;

  RelField<int64_t>(c, 0x90) = 0;
  RelField<uint8_t*>(c, R::kFragmentAssembly) = nullptr;
  RelField<int>(c, kFragmentOffset) = 0;
  RelField<int64_t>(c, R::kLastAckedSendTime) = 0;
  RelField<uint8_t>(c, R::kWindowFullLastPass) = 0;
  RelField<int64_t>(c, kNextNeededTime) = 0;
  RelField<ReliableOutgoingEntry*>(c, R::kOutgoingRing) =
      NewRing<ReliableOutgoingEntry, OutgoingEntryConstruct>(Cfg(c, R::kMaxOutstandingPackets));
  RelField<ReliableIncomingEntry*>(c, R::kIncomingWindow) =
      NewRing<ReliableIncomingEntry, IncomingEntryConstruct>(Cfg(c, R::kWindowSize));
  return c;
}

// 0x14034c720
void ReliableDestruct(UdpReliableChannel* c) {
  if (auto*& coalesce = RelField<UdpRefCount*>(c, kCoalescePacket)) {
    coalesce->VirtualRelease();
    coalesce = nullptr;
  }
  while (LogicalPacket* packet = Queue(c).RemoveHead()) packet->VirtualRelease();
  DeleteRing<ReliableOutgoingEntry, OutgoingEntryDestruct>(Ring(c));
  DeleteRing<ReliableIncomingEntry, IncomingEntryDestruct>(RelField<ReliableIncomingEntry*>(c, R::kIncomingWindow));
  soeutil::FreeArray(RelField<uint8_t*>(c, R::kFragmentAssembly));
  Queue(c).vtable = reinterpret_cast<void**>(0x142052bb8);
}

// 0x14034def0: UdpLinkedList<LogicalPacket>::RemoveHead (out of line).
LogicalPacket* LogicalListRemoveHead(UdpLinkedList<LogicalPacket>* list) { return list->RemoveHead(); }

// 0x14034c6b0: UdpLinkedList<LogicalPacket> destructor.
void LogicalListDestruct(UdpLinkedList<LogicalPacket>* list) {
  list->vtable = reinterpret_cast<void**>(0x142052bb8);
}

// 0x14034da30
void ReliableQueueLogicalPacket(UdpReliableChannel* c, LogicalPacket* packet) {
  packet->VirtualAddRef();
  RelField<int>(c, kQueuedBytes) += PacketLength(packet);
  Queue(c).AddTail(packet);
}

// 0x14034cc90. Closes the current coalescing group packet and queues it. A
// group with a single entry is unwrapped back to that entry.
void ReliableFlushCoalesce(UdpReliableChannel* c) {
  auto*& coalesce = RelField<UdpRefCount*>(c, kCoalescePacket);
  if (!coalesce) return;
  uint8_t*& start = RelField<uint8_t*>(c, kCoalesceStart);
  uint8_t*& cursor = RelField<uint8_t*>(c, kCoalesceCursor);
  if (RelField<int>(c, kCoalesceCount) == 1) {
    uint32_t length;
    int sizeBytes = GetVariableValue(start + 2, &length);
    std::memmove(start, start + 2 + sizeBytes, static_cast<int>(length));
    cursor = start + static_cast<int>(length);
  }
  PacketSetLength(coalesce, static_cast<int>(cursor - start));
  ReliableQueueLogicalPacket(c, static_cast<LogicalPacket*>(coalesce));
  coalesce->VirtualRelease();
  coalesce = nullptr;
}

// 0x14034e060. Adds a small packet to the current 00 19 group, starting a
// new group when it would not fit.
void ReliableSendCoalesce(UdpReliableChannel* c, const uint8_t* data, int length, const uint8_t* data2,
                          int length2) {
  int maxData = RelField<int>(c, R::kMaxDataBytes);
  bool fits = false;
  while (RelField<UdpRefCount*>(c, kCoalescePacket)) {
    int used = static_cast<int>(RelField<uint8_t*>(c, kCoalesceCursor) - RelField<uint8_t*>(c, kCoalesceStart));
    if (length + length2 + 3 <= maxData - used) {
      fits = true;
      break;
    }
    ReliableFlushCoalesce(c);
  }
  if (!fits) {
    LogicalPacket* group = CreatePacket(Manager(c), nullptr, maxData, nullptr, 0);
    RelField<UdpRefCount*>(c, kCoalescePacket) = group;
    uint8_t* buffer = PacketDataConst(group);
    RelField<uint8_t*>(c, kCoalesceCursor) = buffer;
    RelField<uint8_t*>(c, kCoalesceStart) = buffer;
    *RelField<uint8_t*>(c, kCoalesceCursor)++ = 0x00;
    *RelField<uint8_t*>(c, kCoalesceCursor)++ = 0x19;
    RelField<int>(c, kCoalesceCount) = 0;
  }
  RelField<int>(c, kCoalesceCount) += 1;
  uint8_t*& cursor = RelField<uint8_t*>(c, kCoalesceCursor);
  cursor += PutVariableValue(cursor, static_cast<uint32_t>(length + length2));
  if (data) std::memcpy(cursor, data, length);
  cursor += length;
  if (data2) std::memcpy(cursor, data2, length2);
  cursor += length2;
}

// 0x14034df80. Entry point for reliable application data on this channel.
void ReliableSend(UdpReliableChannel* c, const uint8_t* data, int length, const uint8_t* data2, int length2) {
  if (Queue(c).count == 0 && !RelField<UdpRefCount*>(c, kCoalescePacket)) {
    RelField<int64_t>(c, kNextNeededTime) = 0;  // idle channel: service it now
    ScheduleOwner(Connection(c));
  }
  if (RelField<int>(c, kCoalesceLimit) < length + length2) {
    ReliableFlushCoalesce(c);
    LogicalPacket* packet = CreatePacket(Manager(c), data, length, data2, length2);
    ReliableQueueLogicalPacket(c, packet);
    packet->VirtualRelease();
  } else {
    ReliableSendCoalesce(c, data, length, data2, length2);
  }
}

// 0x14034d860. Moves queued data into free outgoing slots, cutting large
// packets into maxDataBytes fragments (the first fragment reserves 4 bytes
// for the total length). Returns whether anything was moved.
bool ReliablePullDataFromQueue(UdpReliableChannel* c, int budget) {
  bool moved = false;
  int inFlight = static_cast<int>(RelField<int64_t>(c, R::kNextOutgoingId) - RelField<int64_t>(c, R::kOldestUnackedId));
  if (budget < 1) return false;
  while (inFlight < Cfg(c, R::kMaxOutstandingPackets)) {
    if (Queue(c).count == 0) {
      ReliableFlushCoalesce(c);
      if (Queue(c).count == 0) return moved;
    }
    LogicalPacket* packet = Queue(c).first;
    ReliableOutgoingEntry& entry = Outgoing(c, RelField<int64_t>(c, R::kNextOutgoingId));
    entry.parent = packet;
    packet->VirtualAddRef();
    entry.firstSendTime = 0;
    entry.lastSendTime = 0;
    *reinterpret_cast<int*>(&entry.unknown10) = 0;
    int total = PacketLength(entry.parent);
    uint8_t* bytes = PacketData(entry.parent);
    int& offset = RelField<int>(c, kFragmentOffset);
    int remaining = total - offset;
    int take = remaining < RelField<int>(c, R::kMaxDataBytes) ? remaining : RelField<int>(c, R::kMaxDataBytes);
    entry.data = bytes + offset;
    if (take != total && offset == 0) take -= 4;
    entry.length = take;
    RelField<int>(c, R::kBytesInFlight) += take;
    if (take == remaining) {
      offset = 0;
      LogicalPacket* head = Queue(c).first;
      Queue(c).Remove(head);
      head->VirtualRelease();
    } else {
      offset += take;
    }
    RelField<int>(c, kQueuedBytes) -= take;
    ++inFlight;
    RelField<int64_t>(c, R::kNextOutgoingId) += 1;
    budget -= take;
    moved = true;
    if (budget < 1) return true;
  }
  return moved;
}

// 0x14034df60
void ReliableClearStats(UdpReliableChannel* c) {
  RelField<int64_t>(c, 0x138) = 0;
  RelField<int>(c, 0x140) = 0;
  RelField<int64_t>(c, 0x150) = 0;
  RelField<int64_t>(c, 0x148) = 0;
}

// 0x14034cd40. Fills a UdpReliableChannel::ChannelStatus snapshot.
void ReliableGetChannelStatus(UdpReliableChannel* c, int* status) {
  int coalesced = 0;
  if (RelField<UdpRefCount*>(c, kCoalescePacket))
    coalesced = static_cast<int>(RelField<uint8_t*>(c, kCoalesceCursor) - RelField<uint8_t*>(c, kCoalesceStart));
  status[0] = RelField<int>(c, R::kBytesInFlight) + RelField<int>(c, kQueuedBytes) + coalesced;
  status[1] = Queue(c).count;
  status[2] = RelField<int>(c, kQueuedBytes);
  status[3] = RelField<int>(c, 0x94);
  status[4] = RelField<int>(c, 0x90);
  status[6] = RelField<int>(c, R::kStatOutOfOrder);
  status[7] = RelField<int>(c, R::kStatDuplicate);
  status[8] = RelField<int>(c, R::kStatOverflow);
  status[9] = RelField<int>(c, kStatResentLost);
  status[10] = RelField<int>(c, kStatResentTimeout);
  status[11] = RelField<int>(c, R::kSlowStartThreshold);
  status[12] = RelField<int>(c, R::kCongestionWindow);
  status[13] = RelField<int>(c, R::kAveragePing);
  status[5] = 0;
  status[14] = 0;
  status[15] = 0;
  *reinterpret_cast<int64_t*>(status + 0x14) = RelField<int64_t>(c, R::kNextIncomingId);
  *reinterpret_cast<int64_t*>(status + 0x16) = RelField<int64_t>(c, R::kNextOutgoingId);
  *reinterpret_cast<int64_t*>(status + 0x18) = RelField<int64_t>(c, R::kOldestUnackedId);
  status[0x10] = RelField<int>(c, R::kQueuedIncoming);
  status[0x11] = RelField<int>(c, R::kMaxQueuedIncoming);
  status[0x12] = RelField<int>(c, 0x13C);
  *reinterpret_cast<int64_t*>(status + 0x1A) = RelField<int>(c, 0x140);
  *reinterpret_cast<int64_t*>(status + 0x1C) = RelField<int64_t>(c, 0x148);
  *reinterpret_cast<int64_t*>(status + 0x1E) = RelField<int64_t>(c, 0x150);
  int64_t oldest = RelField<int64_t>(c, R::kOldestUnackedId);
  if (oldest < RelField<int64_t>(c, R::kNextOutgoingId)) {
    ReliableOutgoingEntry& entry = Outgoing(c, oldest);
    if (entry.firstSendTime != 0) {
      status[5] = ConnectionElapsed(Connection(c), entry.firstSendTime);
      status[14] = ConnectionElapsed(Connection(c), entry.lastSendTime);
      status[15] = *reinterpret_cast<int*>(&entry.unknown10);  // times sent
    }
  }
}

// 0x14034ceb0. Sends new and overdue reliable packets within the congestion
// window and returns how many ms until the channel next needs service.
int ReliableGiveTime(UdpReliableChannel* c) {
  UdpConnection* conn = Connection(c);
  int64_t now = ConnectionClock(conn);
  int64_t& nextNeeded = RelField<int64_t>(c, kNextNeededTime);
  if (now < nextNeeded) return Clamp(nextNeeded - now);

  if (Cfg(c, kCfgTrickleRate) > 0) {
    int wait = Cfg(c, kCfgTrickleRate) - Clamp(now - RelField<int64_t>(c, kLastSendTime));
    if (wait > 0) return wait;
  }

  int resendDelay = (Cfg(c, kCfgResendDelayPercent) * RelField<int>(c, R::kAveragePing)) / 100 +
                    Cfg(c, kCfgResendDelayBase);
  if (Cfg(c, R::kResendDelayCap) <= resendDelay) resendDelay = Cfg(c, R::kResendDelayCap);
  RelField<uint8_t>(c, R::kWindowFullLastPass) = 0;
  int nextResend = 600000;

  if (RelField<int64_t>(c, R::kOldestUnackedId) < RelField<int64_t>(c, R::kNextOutgoingId) || Queue(c).count != 0 ||
      RelField<UdpRefCount*>(c, kCoalescePacket)) {
    int64_t resendBefore = now - resendDelay;
    if (resendBefore < RelField<int64_t>(c, R::kLastAckedSendTime)) resendBefore = RelField<int64_t>(c, R::kLastAckedSendTime);

    bool overflowed;
    do {
      overflowed = false;
      auto windowLimit = [c] {
        int window = RelField<int>(c, R::kCongestionWindow);
        return Cfg(c, kCfgMaxInstandingBytes) <= window ? Cfg(c, kCfgMaxInstandingBytes) : window;
      };
      int window = windowLimit();
      int budget = window;
      int inFlight = 0;
      ReliableOutgoingEntry* due[1000];
      int dueCount = 0;

      // Pass 1: walk the in-flight range (pulling new data at its end) and
      // collect what is due for (re)sending.
      int64_t id = RelField<int64_t>(c, R::kOldestUnackedId);
      while (id <= RelField<int64_t>(c, R::kNextOutgoingId)) {
        if (id == RelField<int64_t>(c, R::kNextOutgoingId) && !ReliablePullDataFromQueue(c, budget)) break;
        ReliableOutgoingEntry& entry = Outgoing(c, id);
        if (entry.data) {
          budget -= entry.length;
          if (entry.lastSendTime < resendBefore) {
            if (dueCount < 1000)
              due[dueCount++] = &entry;
            else
              overflowed = true;
          } else {
            inFlight += entry.length;
            int left = resendDelay - Clamp(now - entry.lastSendTime);
            if (left < nextResend) nextResend = left;
          }
          if (entry.firstSendTime == 0 && budget < 1) break;
        }
        ++id;
      }

      // Pass 2: send them, adjusting congestion control on loss.
      int lostThisPass = 0;
      bool mayAdjust = RelField<int>(c, R::kAveragePing) < Clamp(now - RelField<int64_t>(c, kLastCongestionChange));
      int sentThisPass = 0;
      int oldestIndex = static_cast<int>(RelField<int64_t>(c, R::kOldestUnackedId) % Cfg(c, R::kMaxOutstandingPackets));
      for (int i = 0;; ++i) {
        if (i >= dueCount) break;
        if (window <= inFlight) {
          RelField<uint8_t>(c, R::kWindowFullLastPass) = 1;
          goto passDone;
        }
        {
          ReliableOutgoingEntry& entry = *due[i];
          uint8_t* parentData = PacketData(entry.parent);
          bool fragment = entry.data != parentData || entry.length != PacketLength(entry.parent);
          int slot = static_cast<int>(&entry - Ring(c));
          int relative = slot >= oldestIndex ? slot - oldestIndex : slot + Cfg(c, R::kMaxOutstandingPackets) - oldestIndex;
          uint16_t wireId = static_cast<uint16_t>(relative + RelField<int64_t>(c, R::kOldestUnackedId));
          uint8_t header[8] = {0x00,
                               static_cast<uint8_t>(fragment * 4 + 9 + RelField<uint8_t>(c, R::kChannelNumber)),
                               static_cast<uint8_t>(wireId >> 8), static_cast<uint8_t>(wireId)};
          int headerLength = 4;
          if (fragment && entry.data == parentData) {  // first fragment carries the total size
            uint32_t total = static_cast<uint32_t>(PacketLength(entry.parent));
            header[4] = static_cast<uint8_t>(total >> 24);
            header[5] = static_cast<uint8_t>(total >> 16);
            header[6] = static_cast<uint8_t>(total >> 8);
            header[7] = static_cast<uint8_t>(total);
            headerLength = 8;
          }
          BufferedSend(conn, header, headerLength, entry.data, entry.length, false);

          if (entry.firstSendTime == 0) {
            entry.firstSendTime = now;
          } else {
            int maxAge = ConnField<UdpManager*>(conn, O::kManager)->params.At<int>(0x34);
            if (maxAge > 0 && Clamp(now - entry.firstSendTime) > maxAge) {
              ConnectionDisconnect(conn, 0, kDisconnectReliableTooOld);
              return 0;
            }
            if (entry.lastSendTime < RelField<int64_t>(c, R::kLastAckedSendTime)) {
              // Something sent after this was acked first: treat it as lost.
              if (mayAdjust && Cfg(c, kCfgToleranceLossCount) < lostThisPass) {
                mayAdjust = false;
                RelField<int64_t>(c, kLastCongestionChange) = now;
                int reduced = (RelField<int>(c, R::kCongestionWindow) * 9) / 10;
                if (reduced <= RelField<int>(c, kMinimumWindow)) reduced = RelField<int>(c, kMinimumWindow);
                RelField<int>(c, R::kCongestionWindow) = reduced;
                RelField<int>(c, R::kSlowStartThreshold) = reduced;
                window = windowLimit();
              }
              ++lostThisPass;
              RelField<int>(c, kStatResentLost) += 1;
              ConnField<int64_t>(conn, 0x138) += 1;
              CountManager(Manager(c), 0x398);
            } else {
              // Plain timeout: back to the initial window, slower resends.
              if (mayAdjust) {
                mayAdjust = false;
                RelField<int64_t>(c, kLastCongestionChange) = now;
                int half = RelField<int>(c, R::kCongestionWindow) / 2;
                int floor = RelField<int>(c, R::kMaxDataBytes) * 2;
                RelField<int>(c, R::kSlowStartThreshold) = floor < half ? half : floor;
                RelField<int>(c, R::kCongestionWindow) = RelField<int>(c, kInitialWindow);
                window = windowLimit();
                int& delay = RelField<int>(c, R::kAveragePing);
                delay += 100;
                if (Cfg(c, R::kResendDelayCap) <= delay) delay = Cfg(c, R::kResendDelayCap);
              }
              RelField<int>(c, kStatResentTimeout) += 1;
              ConnField<int64_t>(conn, 0x140) += 1;
              CountManager(Manager(c), 0x3A0);
            }
          }
          entry.lastSendTime = now;
          *reinterpret_cast<int*>(&entry.unknown10) += 1;
          if (resendDelay < nextResend) nextResend = resendDelay;
          inFlight += entry.length;
          RelField<int64_t>(c, kLastSendTime) = now;
          sentThisPass += entry.length;
        }
        if (Cfg(c, kCfgFragmentSize) != 0 && sentThisPass >= Cfg(c, kCfgFragmentSize)) break;
      }
      if (window <= inFlight) RelField<uint8_t>(c, R::kWindowFullLastPass) = 1;
    passDone:;
    } while (overflowed && !RelField<uint8_t>(c, R::kWindowFullLastPass));
  } else {
    RelField<int>(c, R::kCongestionWindow) = RelField<int>(c, kInitialWindow);
  }

  int trickle = Cfg(c, kCfgTrickleRate) - Clamp(now - RelField<int64_t>(c, kLastSendTime));
  int wait = trickle >= nextResend ? trickle : nextResend;
  if (wait < 0) wait = 0;
  nextNeeded = now + wait;
  return wait;
}

REBUILD_FUNCTION(UdpReliableChannel_IncomingEntryConstruct, 0x14034c340, IncomingEntryConstruct);
REBUILD_FUNCTION(UdpReliableChannel_OutgoingEntryConstruct, 0x14034c350, OutgoingEntryConstruct);
REBUILD_FUNCTION(UdpReliableChannel_IncomingEntryDestruct, 0x14034c6c0, IncomingEntryDestruct);
REBUILD_FUNCTION(UdpReliableChannel_OutgoingEntryDestruct, 0x14034c6f0, OutgoingEntryDestruct);
REBUILD_FUNCTION(UdpReliableChannel_IncomingEntryVectorDelete, 0x14034c870, IncomingEntryVectorDelete);
REBUILD_FUNCTION(UdpReliableChannel_OutgoingEntryVectorDelete, 0x14034c910, OutgoingEntryVectorDelete);
REBUILD_FUNCTION(UdpReliableChannel_Construct, 0x14034c360, ReliableConstruct);
REBUILD_FUNCTION(UdpReliableChannel_Destruct, 0x14034c720, ReliableDestruct);
REBUILD_FUNCTION(UdpLinkedList_LogicalPacket_RemoveHead, 0x14034def0, LogicalListRemoveHead);
REBUILD_FUNCTION(UdpLinkedList_LogicalPacket_Destruct, 0x14034c6b0, LogicalListDestruct);
REBUILD_FUNCTION(UdpReliableChannel_QueueLogicalPacket, 0x14034da30, ReliableQueueLogicalPacket);
REBUILD_FUNCTION(UdpReliableChannel_FlushCoalesce, 0x14034cc90, ReliableFlushCoalesce);
REBUILD_FUNCTION(UdpReliableChannel_SendCoalesce, 0x14034e060, ReliableSendCoalesce);
REBUILD_FUNCTION(UdpReliableChannel_Send, 0x14034df80, ReliableSend);
REBUILD_FUNCTION(UdpReliableChannel_PullDataFromQueue, 0x14034d860, ReliablePullDataFromQueue);
REBUILD_FUNCTION(UdpReliableChannel_ClearStats, 0x14034df60, ReliableClearStats);
REBUILD_FUNCTION(UdpReliableChannel_GetChannelStatus, 0x14034cd40, ReliableGetChannelStatus);
REBUILD_FUNCTION(UdpReliableChannel_GiveTime, 0x14034ceb0, ReliableGiveTime);

}  // namespace rebuild::udp
