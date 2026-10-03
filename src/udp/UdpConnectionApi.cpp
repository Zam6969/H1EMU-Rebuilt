// UdpConnection's public API: Send over the 8 channel types, flushing,
// statistics, and the descriptive strings.
#include <cstring>

#include "core/crt.h"
#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"
#include "udp/UdpConnection.h"
#include "udp/UdpReliableChannel.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

// UdpChannel
enum UdpChannel {
  kChannelUnreliable = 0,
  kChannelUnreliableUnbuffered = 1,
  kChannelOrdered = 2,
  kChannelOrderedUnbuffered = 3,
  kChannelReliable1 = 4,  // 4-7: reliable channels 0-3
  kChannelReliable4 = 7,
};

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }

// Not rebuilt in this file.
uint8_t* BufferedSend(UdpConnection* c, const uint8_t* data, int length, const uint8_t* data2, int length2,
                      bool writable) {
  using Fn = uint8_t* (*)(UdpConnection*, const uint8_t*, int, const uint8_t*, int, bool);
  return game::Call<Fn>(0x140345930)(c, data, length, data2, length2, writable);
}
void* NewReliableChannel(void* memory, int channel, UdpConnection* c, void* config) {
  return game::Call<void* (*)(void*, int, UdpConnection*, void*)>(0x14034c360)(memory, channel, c, config);
}
void ReliableSend(void* channel, const uint8_t* data, int length, const uint8_t* data2, int length2) {
  game::Call<void (*)(void*, const uint8_t*, int, const uint8_t*, int)>(0x14034df80)(channel, data, length, data2,
                                                                                    length2);
}
void ReliableGetChannelStatus(void* channel, void* status) {
  game::Call<void (*)(void*, void*)>(0x14034cd40)(channel, status);
}
void InternalGiveTime(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140347360)(c); }
// The helper object's outgoing queue (0x14165xxxx, not rebuilt).
SharedByteArray* HelperPop(void* helper) { return game::Call<SharedByteArray* (*)(void*)>(0x14165ae90)(helper); }
void StringAppend(void* string, const char* text) {
  game::Call<void (*)(void*, const char*)>(0x1402bd730)(string, text);
}
const char* IpToString(const UdpIpAddress* ip, char* buffer, int size) {
  return game::Call<const char* (*)(const UdpIpAddress*, char*, int)>(0x14034a5c0)(ip, buffer, size);
}

void ReleaseByteArray(SharedByteArray* array) {
  int* control = array->control;
  int strong = InterlockedDecrement(reinterpret_cast<volatile LONG*>(&control[0]));
  int weakBefore = InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(&control[1]), -1);
  if (weakBefore == 1 && control) soeutil::Free(control, 0x10);
  if (strong == 0) reinterpret_cast<void (*)(void*)>(array->refVtable[1])(&array->refVtable);
}

}  // namespace

// 0x140346020. Name of a DisconnectReason, from a function-local static
// table of 17 SoeUtil strings (0x48 bytes each) in the game's .data.
const char* DisconnectReasonText(int reason) {
  auto* table = reinterpret_cast<uint8_t*>(0x142b06e40);
  crt::ThreadSafeStatic(reinterpret_cast<int*>(0x142b07308), [table] {
    for (int i = 0; i < 17; ++i) game::Call<void* (*)(void*)>(0x140345070)(table + i * 0x48);
    crt::Atexit(0x1420160e0);
  });
  auto& filled = *reinterpret_cast<uint8_t*>(0x142b06e38);
  if (!filled) {
    filled = 1;
    static const char* const kNames[17] = {
        "DisconnectReasonNone", "DisconnectReasonIcmpError", "DisconnectReasonTimeout",
        "DisconnectReasonOtherSideTerminated", "DisconnectReasonManagerDeleted", "DisconnectReasonConnectFail",
        "DisconnectReasonApplication", "DisconnectReasonUnreachableConnection",
        "DisconnectReasonUnacknowledgedTimeout", "DisconnectReasonNewConnectionAttempt",
        "DisconnectReasonConnectionRefused", "DisconnectReasonConnectError", "DisconnectReasonConnectingToSelf",
        "DisconnectReasonReliableOverflow", "DisconnectReasonApplicationReleased", "DisconnectReasonCorruptPacket",
        "DisconnectReasonProtocolMismatch"};
    for (int i = 0; i < 17; ++i) soeutil::StringAssign(table + i * 0x48, kNames[i]);
  }
  return *reinterpret_cast<const char**>(table + 8 + static_cast<int64_t>(reason) * 0x48);
}

// 0x140347a30. Sends application data on a channel. Unreliable and ordered
// data too large for one datagram is promoted to reliable channel 0.
bool ConnectionSend(UdpConnection* c, unsigned channel, const uint8_t* data, int length, const uint8_t* data2,
                    int length2) {
  UdpManager* manager = ConnField<UdpManager*>(c, O::kManager);
  manager->StatsGuard().Enter();
  game::Field<int64_t>(manager, 0x3B8) += 1;
  manager->StatsGuard().Leave();
  ConnField<int64_t>(c, 0x148) += 1;

  int total = length + length2;
  int room = ConnField<int>(c, 0x200) - ConnField<int>(c, O::kEncryptExpansionBytes) - ConnField<int>(c, 0x1F4);
  if (channel <= kChannelUnreliableUnbuffered && total > room) {
    channel = kChannelReliable1;
  } else if (channel - kChannelOrdered <= 1 && total > room - 4) {
    channel = kChannelReliable1;
  } else if (channel > kChannelReliable4) {
    return false;
  }

  uint8_t buffer[0x2000];
  switch (channel) {
    case kChannelUnreliable:
      BufferedSend(c, data, length, data2, length2, false);
      return true;
    case kChannelUnreliableUnbuffered:
      std::memcpy(buffer, data, length);
      if (data2) std::memcpy(buffer + length, data2, length2);
      PhysicalSend(c, buffer, total, true);
      return true;
    case kChannelOrdered:
    case kChannelOrderedUnbuffered: {
      bool buffered = channel == kChannelOrdered;
      int& sequence = ConnField<int>(c, buffered ? 0x268 : 0x26C);
      ++sequence;
      buffer[0] = 0x00;
      buffer[1] = buffered ? 0x1A : 0x1B;
      buffer[2] = static_cast<uint8_t>(static_cast<uint16_t>(sequence) >> 8);
      buffer[3] = static_cast<uint8_t>(sequence);
      std::memcpy(buffer + 4, data, length);
      if (data2) std::memcpy(buffer + 4 + length, data2, length2);
      if (buffered)
        BufferedSend(c, buffer, total + 4, nullptr, 0, true);
      else
        PhysicalSend(c, buffer, total + 4, true);
      return true;
    }
    default: {
      int index = static_cast<int>(channel) - kChannelReliable1;
      void*& reliable = ConnField<void*>(c, 0x1D0 + index * 8);
      if (!reliable) {
        void* memory = soeutil::Allocate(0x158);
        reliable = memory ? NewReliableChannel(memory, index, c,
                                               reinterpret_cast<uint8_t*>(ConnField<UdpManager*>(c, O::kManager)) +
                                                   0xAC + index * 0x34)
                          : nullptr;
      }
      ReliableSend(reliable, data, length, data2, length2);
      return true;
    }
  }
}

// 0x140346900. Sends everything queued in the helper object on reliable
// channel 0, escaping payloads that start with 0x00.
void ConnectionDrainSendQueue(UdpConnection* c) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  if (ConnField<int>(c, UdpConnectionInternals::kStatus) == kStatusConnected) {
    for (SharedByteArray* item = HelperPop(ConnField<void*>(c, 0x2F0)); item;
         item = HelperPop(ConnField<void*>(c, 0x2F0))) {
      const uint8_t* bytes = item->length != 0 ? item->data : nullptr;
      if (bytes[0] == 0) {
        uint8_t escape = 0;
        ConnectionSend(c, kChannelReliable1, &escape, 1, item->length != 0 ? item->data : nullptr, item->length);
      } else {
        ConnectionSend(c, kChannelReliable1, bytes, item->length, nullptr, 0);
      }
      ReleaseByteArray(item);
    }
  }
  guard.Leave();
}

// 0x140346760. Give the connection a tick right now, then push out
// anything buffered.
void ConnectionFlushNow(UdpConnection* c) {
  Ref(c)->VirtualAddRef();
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  guard.Enter();
  if (auto* manager = ConnField<UdpManager*>(c, O::kManager)) {
    manager->VirtualAddRef();
    ConnField<uint8_t>(c, O::kInGiveTime) = 1;
    InternalGiveTime(c);
    ConnField<uint8_t>(c, O::kInGiveTime) = 0;
    manager->VirtualRelease();
  }
  guard.Leave();
  FlushChannels(c);
  guard.Leave();
  Ref(c)->VirtualRelease();
}

// 0x140346a60: status of reliable channel (channel 4-7); zeroed otherwise.
void ConnectionGetChannelStatus(UdpConnection* c, int channel, void* status) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  std::memset(status, 0, 0x80);
  unsigned index = static_cast<unsigned>(channel - 4);
  if (index < 4) {
    if (void* reliable = ConnField<void*>(c, 0x1D0 + index * 8)) ReliableGetChannelStatus(reliable, status);
  }
  guard.Leave();
}

// 0x140346af0
UdpIpAddress* ConnectionGetDestinationIp(UdpConnection* c, UdpIpAddress* out) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  *out = ConnField<UdpIpAddress>(c, O::kIp);
  guard.Leave();
  return out;
}

// 0x140346b50: "a.b.c.d:port" into a SoeUtil string; returns its text.
const char* ConnectionGetDestinationString(UdpConnection* c, void* string) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  UdpIpAddress ip;
  ConnectionGetDestinationIp(c, &ip);
  guard.Enter();
  int port = ConnField<int>(c, O::kPort);
  guard.Leave();
  char text[256];
  soeutil::StringFormat(string, "%s:%d", IpToString(&ip, text, sizeof(text)), port);
  const char* result = *reinterpret_cast<const char**>(static_cast<uint8_t*>(string) + 8);
  guard.Leave();
  return result;
}

// 0x140346c00: the disconnect reason, plus the other side's reason when it
// terminated ("...OtherSideTerminated,<their reason>").
const char* ConnectionGetDisconnectReasonText(UdpConnection* c, void* string) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  int reason = ConnField<int>(c, 0x1C0);
  guard.Leave();
  const char* text = DisconnectReasonText(reason);
  if (reason == kDisconnectReasonOtherSideTerminated) {
    guard.Enter();
    int theirs = ConnField<int>(c, 0x1C4);
    guard.Leave();
    const char* theirText = DisconnectReasonText(theirs);
    soeutil::StringAssign(string, text);
    StringAppend(string, reinterpret_cast<const char*>(0x142052284));  // ","
    StringAppend(string, theirText);
  } else {
    soeutil::StringAssign(string, text);
  }
  return *reinterpret_cast<const char**>(static_cast<uint8_t*>(string) + 8);
}

// 0x140346cf0: fills UdpConnectionStatistics (0xC0 bytes, mostly a copy of
// +0xF0..+0x1AF) and derives the packet-loss ratios.
void ConnectionGetStats(UdpConnection* c, uint8_t* stats) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  if (UdpManager* manager = ConnField<UdpManager*>(c, O::kManager)) {
    std::memcpy(stats, &ConnField<uint8_t>(c, 0xF0), 0xC0);
    int& sinceSync = *reinterpret_cast<int*>(stats + 0x78);
    sinceSync = manager->params.At<int>(0x50) == 0 ? -1 : ConnectionElapsed(c, ConnField<int64_t>(c, 0x2A0));
    float& percentSentSuccess = *reinterpret_cast<float*>(stats + 0xB8);
    float& percentReceivedSuccess = *reinterpret_cast<float*>(stats + 0xBC);
    percentSentSuccess = 1.0f;
    percentReceivedSuccess = 1.0f;
    int64_t syncOurSent = *reinterpret_cast<int64_t*>(stats + 0x98);
    int64_t syncTheirReceived = *reinterpret_cast<int64_t*>(stats + 0xB0);
    int64_t syncTheirSent = *reinterpret_cast<int64_t*>(stats + 0xA8);
    int64_t syncOurReceived = *reinterpret_cast<int64_t*>(stats + 0xA0);
    if (syncOurSent > 0) percentSentSuccess = static_cast<float>(syncTheirReceived) / static_cast<float>(syncOurSent);
    if (syncTheirSent > 0)
      percentReceivedSuccess = static_cast<float>(syncOurReceived) / static_cast<float>(syncTheirSent);
    *reinterpret_cast<int*>(stats + 0x90) = 0;
    if (auto* reliable = ConnField<uint8_t*>(c, 0x1D0))
      *reinterpret_cast<int*>(stats + 0x90) = game::Field<int>(reliable, 0x98);
  }
  guard.Leave();
}

REBUILD_FUNCTION(UdpConnection_DisconnectReasonText, 0x140346020, DisconnectReasonText);
REBUILD_FUNCTION(UdpConnection_Send, 0x140347a30, ConnectionSend);
REBUILD_FUNCTION(UdpConnection_DrainSendQueue, 0x140346900, ConnectionDrainSendQueue);
REBUILD_FUNCTION(UdpConnection_FlushNow, 0x140346760, ConnectionFlushNow);
REBUILD_FUNCTION(UdpConnection_GetChannelStatus, 0x140346a60, ConnectionGetChannelStatus);
REBUILD_FUNCTION(UdpConnection_GetDestinationIp, 0x140346af0, ConnectionGetDestinationIp);
REBUILD_FUNCTION(UdpConnection_GetDestinationString, 0x140346b50, ConnectionGetDestinationString);
REBUILD_FUNCTION(UdpConnection_GetDisconnectReasonText, 0x140346c00, ConnectionGetDisconnectReasonText);
REBUILD_FUNCTION(UdpConnection_GetStats, 0x140346cf0, ConnectionGetStats);

}  // namespace rebuild::udp
