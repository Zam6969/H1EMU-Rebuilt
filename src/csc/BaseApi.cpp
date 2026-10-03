// ClientServerCore: the game's networking layer on top of UdpLibrary.
//
// ClientServerCore::UdpCompressionHandler plugs into UdpLibrary's
// "user supplied" encryption slots to compress every packet: payloads get
// a flag byte, 01 = deflated (zlib level 6), 00 = stored.
// ClientServerCore::BaseApi is both the UdpManagerHandler and (through its
// subobject at +0x80) the UdpConnectionHandler; incoming game packets enter
// at BaseApi::OnRoutePacket.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Mutex.h"
#include "soeutil/String.h"
#include "udp/UdpConnection.h"

namespace rebuild::soeutil {
int64_t* TimeNow(int64_t* out);  // Time.cpp
}

namespace rebuild::csc {
namespace {

// UdpCompressionHandler statistics (under the SoeUtil mutex at +0x08).
struct CompressionStats {
  static constexpr size_t kMutex = 0x08;
  static constexpr size_t kSentOut = 0x50;        // bytes produced by compression (incl. flag)
  static constexpr size_t kSentIn = 0x58;         // bytes offered for sending
  static constexpr size_t kSentStored = 0x60;     // bytes sent uncompressed
  static constexpr size_t kReceivedIn = 0x68;     // bytes received (incl. flag)
  static constexpr size_t kReceivedOut = 0x70;    // bytes after decompression
  static constexpr size_t kReceivedStored = 0x78; // bytes received uncompressed
};

constexpr int kMinCompressSize = 0x18;
constexpr int kMaxDecompressedSize = 0x1FFE;

int Deflate(const uint8_t* src, int length, uint8_t* dest, int capacity, int level, int flags) {
  return game::Call<int (*)(const uint8_t*, int, uint8_t*, int, int, int)>(0x14167ee90)(src, length, dest,
                                                                                       capacity, level, flags);
}
int Inflate(const uint8_t* src, int length, uint8_t* dest, int capacity) {
  return game::Call<int (*)(const uint8_t*, int, uint8_t*, int)>(0x14167efa0)(src, length, dest, capacity);
}

struct StatsLock {
  explicit StatsLock(void* handler)
      : mutex(reinterpret_cast<CRITICAL_SECTION*>(static_cast<uint8_t*>(handler) + CompressionStats::kMutex)) {
    soeutil::MutexLock(mutex);
  }
  ~StatsLock() { soeutil::MutexUnlock(mutex); }
  CRITICAL_SECTION* mutex;
};

int64_t& Stat(void* handler, size_t offset) { return game::Field<int64_t>(handler, offset); }

}  // namespace

// 0x1415f3060 (UdpManagerHandler slot 2): compress an outgoing packet.
int CompressionEncrypt(void* handler, void* /*connection*/, uint8_t* dest, const uint8_t* src, int length) {
  if (length >= kMinCompressSize) {
    int packed = Deflate(src, length, dest + 1, length - 1, 6, 0);
    if (packed > 0 && packed < length) {
      dest[0] = 1;
      StatsLock lock(handler);
      Stat(handler, CompressionStats::kSentIn) += length;
      Stat(handler, CompressionStats::kSentOut) += packed + 1;
      return packed + 1;
    }
  }
  dest[0] = 0;
  std::memcpy(dest + 1, src, length);
  StatsLock lock(handler);
  Stat(handler, CompressionStats::kSentStored) += length;
  Stat(handler, CompressionStats::kSentIn) += length;
  Stat(handler, CompressionStats::kSentOut) += length + 1;
  return length + 1;
}

// 0x1415f2fa0 (slot 3): decompress an incoming packet. -1 = corrupt.
int CompressionDecrypt(void* handler, void* /*connection*/, uint8_t* dest, const uint8_t* src, int length) {
  if (src[0] == 1) {
    int unpacked = Inflate(src + 1, length - 1, dest, kMaxDecompressedSize);
    if (unpacked < 0) return -1;
    StatsLock lock(handler);
    Stat(handler, CompressionStats::kReceivedIn) += length;
    Stat(handler, CompressionStats::kReceivedOut) += unpacked;
    return unpacked;
  }
  int stored = length - 1;
  std::memcpy(dest, src + 1, stored);
  StatsLock lock(handler);
  Stat(handler, CompressionStats::kReceivedIn) += length;
  Stat(handler, CompressionStats::kReceivedOut) += stored;
  Stat(handler, CompressionStats::kReceivedStored) += stored;
  return stored;
}

namespace {

// Offsets inside BaseApi's UdpConnectionHandler subobject (BaseApi + 0x80).
constexpr size_t kRpcHandler = 0x10;            // routes RPC packets first
constexpr size_t kReceiveStats = 0x2E8;         // stats object, slot 1 = AddBytes(length)
constexpr size_t kSlowPacketMs = 0xB18;         // log packets slower than this (0 = off)
constexpr size_t kSlowPacketLogBytes = 0xB1C;   // max bytes dumped for a slow packet
constexpr size_t kLogger = 0xB28;
constexpr size_t kSubobjectToBaseApi = 0x80;
constexpr size_t kBaseApiHandlePacketSlot = 0x90 / 8;  // slot 18: game packet dispatch

constexpr uintptr_t kVtStringFixed512 = 0x14204aea0;
constexpr uintptr_t kVtIStringFixed512 = 0x14204ae80;

int ElapsedMsSince(int64_t start) {
  int64_t now;
  int64_t elapsed = *soeutil::TimeNow(&now) - start;
  return elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
}

}  // namespace

namespace {

// BaseApi fields.
constexpr size_t kApiConnection = 0x2C0;     // UdpConnection*
constexpr size_t kApiLogContext = 0x2D0;     // {.. +0x209 name, +0x250 log, +0x288 verbose}
constexpr size_t kApiConnecting = 0x2E0;     // bool
constexpr size_t kApiConnected = 0x2E1;      // bool
constexpr size_t kApiTimeoutMs = 0x2F0;
constexpr size_t kApiAutoReconnect = 0x2F8;  // bool
constexpr size_t kApiSendStats = 0x780;      // stats object, slot 1 = AddBytes(length)
constexpr size_t kApiAddressText = 0xC10;    // char*
constexpr size_t kApiOnConnectSlot = 0x80 / 8;
constexpr size_t kApiManagerOwned = 0x2D8;   // bool: this api drives the shared manager
constexpr size_t kApiAttempting = 0x2D9;     // bool: connect in progress
constexpr size_t kApiReconnectStart = 0x2E8; // ms timestamp of the last connect attempt
constexpr size_t kApiAddressCount = 0xC18;
constexpr size_t kApiDisconnectSlot = 0x40 / 8;
constexpr size_t kApiGiveTimeSlot = 0x48 / 8;
constexpr size_t kApiIsConnectedSlot = 0x50 / 8;
constexpr size_t kApiIsConnectingSlot = 0x58 / 8;
constexpr int kReasonApplication = 6;
constexpr int kChannelUnreliable = 0;
constexpr int kChannelReliable1 = 4;

template <typename... Args>
void ApiLog(uint8_t* logContext, const char* format, Args... args) {
  if (!logContext || !game::Field<bool>(logContext, 0x288)) return;
  game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(game::Field<void*>(logContext, 0x250), format,
                                                             logContext + 0x209, args...);
}

template <typename Fn>
Fn ApiVirtual(uint8_t* api, size_t slot) {
  return reinterpret_cast<Fn>((*reinterpret_cast<void***>(api))[slot]);
}

bool ApiIsConnected(uint8_t* api) { return ApiVirtual<bool (*)(uint8_t*)>(api, kApiIsConnectedSlot)(api); }
bool ApiIsConnecting(uint8_t* api) { return ApiVirtual<bool (*)(uint8_t*)>(api, kApiIsConnectingSlot)(api); }

void ManagerGiveTime(uint8_t* api, int maxPollingMs) {
  game::Call<void (*)(void*, int)>(0x1415f4470)(game::Field<void*>(api, kApiLogContext), maxPollingMs);
}

// One pump iteration of the Wait* loops.
void PumpOnce(uint8_t* api, int maxPollingMs) {
  ApiVirtual<void (*)(uint8_t*, int)>(api, kApiGiveTimeSlot)(api, maxPollingMs);
  if (!game::Field<bool>(api, kApiManagerOwned)) ManagerGiveTime(api, maxPollingMs);
  game::Call<void (*)(unsigned)>(0x14032ec60)(5);  // Sleep(5)
}

bool TimedOut(int64_t start, int timeoutMs) {
  return timeoutMs != -1 && ElapsedMsSince(start) >= timeoutMs;
}

void AddStatBytes(uint8_t* api, int length) {
  void* stats = api + kApiSendStats;
  reinterpret_cast<void (*)(void*, int)>((*static_cast<void***>(stats))[1])(stats, length);
}

}  // namespace

void ConnectionDisconnectGuarded(udp::UdpConnection* connection, int flushTimeout);  // 0x1415f41c0
void BaseUdpManagerServiceStart(uint8_t* manager);  // BaseUdpManager.cpp
void BaseUdpManagerServiceStop(uint8_t* manager);

// Server address list: singly linked {.., +0x08 text, .., +0x18 next}.
constexpr size_t kApiAddressHead = 0xBE8;
constexpr size_t kApiAddressCurrent = 0xC00;
constexpr size_t kApiAddressString = 0xC08;  // IString (data = kApiAddressText)
constexpr size_t kApiPort = 0x2DC;
constexpr size_t kApiLastDisconnectReason = 0x300;  // IString
constexpr size_t kApiOnFailedSlot = 0x78 / 8;

// 0x1415f4660: Connect. Rotates to the next server address, starts the
// shared UdpManager service and begins establishing the connection.
void BaseApiConnect(uint8_t* api) {
  auto*& current = game::Field<uint8_t*>(api, kApiAddressCurrent);
  if (!current || !(current = *reinterpret_cast<uint8_t**>(current + 0x18))) {
    current = game::Field<uint8_t*>(api, kApiAddressHead);
  }
  const char* address = current ? *reinterpret_cast<const char**>(current + 8) : nullptr;
  soeutil::StringAssign(reinterpret_cast<soeutil::IString*>(api + kApiAddressString), address);
  game::Field<uint64_t>(api, 0x360) = 0;
  auto* reason = reinterpret_cast<soeutil::IString*>(api + kApiLastDisconnectReason);
  soeutil::StringRelease(reason);
  reason->data = soeutil::EmptyStringData();
  reason->length = 0;
  reason->capacity = 0;
  int64_t now;
  game::Field<int64_t>(api, kApiReconnectStart) = *soeutil::TimeNow(&now);
  BaseUdpManagerServiceStart(game::Field<uint8_t*>(api, kApiLogContext));
  game::Field<bool>(api, kApiAttempting) = true;
  uint8_t* manager = game::Field<uint8_t*>(api, kApiLogContext);
  using EstablishFn = udp::UdpConnection* (*)(void*, const char*, int, int);
  udp::UdpConnection* connection = game::Call<EstablishFn>(0x14033fda0)(
      game::Field<void*>(manager, 0x240), game::Field<const char*>(api, kApiAddressText),
      game::Field<int>(api, kApiPort), game::Field<int>(api, kApiTimeoutMs));
  game::Field<udp::UdpConnection*>(api, kApiConnection) = connection;
  if (!connection) {
    manager = game::Field<uint8_t*>(api, kApiLogContext);
    game::Call<void (*)(void*, const char*, ...)>(0x1402baba0)(
        game::Field<void*>(manager, 0x250), reinterpret_cast<const char*>(0x1424b05f0),  // "BaseApi EstablishConnection failed ..."
        manager + 0x209, game::Field<const char*>(api, kApiAddressText));
    ApiVirtual<void (*)(uint8_t*)>(api, kApiOnFailedSlot)(api);
    game::Field<bool>(api, kApiAttempting) = false;
    BaseUdpManagerServiceStop(game::Field<uint8_t*>(api, kApiLogContext));
    return;
  }
  auto* guard = reinterpret_cast<udp::UdpPlatformGuardObject*>(reinterpret_cast<uint8_t*>(connection) + 0x2E8);
  guard->Enter();
  game::Field<uint8_t*>(connection, 0x2B0) = api + kSubobjectToBaseApi;  // connection handler
  guard->Leave();
  game::Field<bool>(api, kApiConnecting) = true;
}

// 0x1415f4d20: UdpConnectionHandler::OnTerminated (on BaseApi + 0x80).
// Records the disconnect reason, logs the connection's final statistics,
// raises OnDisconnect / OnFailed and releases the connection.
void BaseApiOnTerminated(uint8_t* handler, uint8_t* connection) {
  uint8_t* api = handler - kSubobjectToBaseApi;
  auto* reason = reinterpret_cast<soeutil::IString*>(api + kApiLastDisconnectReason);
  if (auto* current = game::Field<uint8_t*>(api, kApiConnection)) {
    game::Call<void (*)(void*, soeutil::IString*)>(0x140346c00)(current, reason);  // GetDisconnectReasonText
  } else {
    soeutil::StringAssignString(reason, reason);
  }
  auto* guard = reinterpret_cast<udp::UdpPlatformGuardObject*>(connection + 0x2E0);
  guard->Enter();
  int value = game::Field<int>(connection, 0x1C0);
  guard->Leave();
  game::Field<int>(api, 0x360) = value;
  guard->Enter();
  value = game::Field<int>(connection, 0x1C4);
  guard->Leave();
  game::Field<int>(api, 0x364) = value;

  uint8_t* logContext = game::Field<uint8_t*>(api, kApiLogContext);
  if (logContext && game::Field<bool>(logContext, 0x288)) {
    alignas(8) uint8_t stats[0xD0];
    game::Call<void (*)(void*, uint8_t*)>(0x140341130)(game::Field<void*>(logContext, 0x240), stats);  // GetStats
    alignas(8) uint8_t channel[0x40];
    game::Call<void (*)(uint8_t*, int, uint8_t*)>(0x140346a60)(connection, kChannelReliable1, channel);  // GetChannelStatus
    logContext = game::Field<uint8_t*>(api, kApiLogContext);
    uint8_t* udpManager = game::Field<uint8_t*>(logContext, 0x240);
    const char* reasonText = reason->data;
    const char* address = game::Field<const char*>(api, kApiAddressText);
    void* log = game::Field<void*>(logContext, 0x250);
    int outgoing = game::Call<int (*)(uint8_t*)>(0x1415f4fe0)(connection);
    int incoming = game::Call<int (*)(uint8_t*)>(0x1415f4610)(connection);
    int lastEventAge = game::Call<int (*)(uint8_t*)>(0x1415f4a10)(udpManager);
    int lastSend = game::Call<int (*)(uint8_t*)>(0x1415f4ad0)(connection);
    int lastReceive = game::Call<int (*)(uint8_t*)>(0x1415f4a70)(connection);
    auto i32 = [](const uint8_t* p, size_t o) { return *reinterpret_cast<const int*>(p + o); };
    auto i64 = [](const uint8_t* p, size_t o) { return *reinterpret_cast<const int64_t*>(p + o); };
    game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(
        log, reinterpret_cast<const char*>(0x1424b0c80),  // "BaseApi connection terminated (%s) address=%s reason=%s ..."
        logContext + 0x209, address, reasonText, lastReceive, lastSend, lastEventAge, incoming, outgoing,
        i32(channel, 0x00) /*TotalPendingBytes*/, i32(channel, 0x34) /*AckPing*/, i32(channel, 0x30) /*WindowSize*/,
        i32(channel, 0x08) /*QueuedBytes*/, i32(channel, 0x14) /*oldest age*/, i32(channel, 0x38) /*oldest last send*/,
        i32(channel, 0x3C) /*attempt*/, i64(stats, 0x98) /*MaxPollingTimeExceeded*/,
        i64(stats, 0xA0) /*MaxDeliveryTimeExceeded*/, i64(stats, 0x90) /*SocketOverflowErrors*/);
  }
  if (game::Field<bool>(api, kApiConnected)) {
    game::Field<bool>(api, kApiConnected) = false;
    ApiVirtual<void (*)(uint8_t*)>(api, 0x88 / 8)(api);  // OnDisconnect
  } else if (game::Field<bool>(api, kApiConnecting)) {
    game::Field<bool>(api, kApiConnecting) = false;
    ApiVirtual<void (*)(uint8_t*)>(api, kApiOnFailedSlot)(api);
  }
  auto* current = game::Field<uint8_t*>(api, kApiConnection);
  auto* connectionGuard = reinterpret_cast<udp::UdpPlatformGuardObject*>(current + 0x2E8);
  connectionGuard->Enter();
  game::Field<void*>(current, 0x2B0) = nullptr;  // detach handler
  connectionGuard->Leave();
  current = game::Field<uint8_t*>(api, kApiConnection);
  reinterpret_cast<void (*)(void*)>((*reinterpret_cast<void***>(current))[1])(current);  // Release
  game::Field<void*>(api, kApiConnection) = nullptr;
  game::Field<bool>(api, kApiAttempting) = false;
  BaseUdpManagerServiceStop(game::Field<uint8_t*>(api, kApiLogContext));
}

// 0x1415f49f0 (slot 11)
bool BaseApiIsConnecting(uint8_t* api) { return game::Field<bool>(api, kApiConnecting); }

// 0x1415f4a00 (slot 10)
bool BaseApiIsConnected(uint8_t* api) { return game::Field<bool>(api, kApiConnected); }

// 0x1415f4920 (slot 20): Send(data, length, reliable)
bool BaseApiSend(uint8_t* api, const uint8_t* data, int length, bool reliable) {
  void* connection = game::Field<void*>(api, kApiConnection);
  if (!connection) return false;
  int channel = reliable ? kChannelReliable1 : kChannelUnreliable;
  if (!game::Call<bool (*)(void*, int, const uint8_t*, int)>(0x1403498a0)(connection, channel, data, length)) {
    return false;
  }
  AddStatBytes(api, length);
  return true;
}

// 0x1415f4980 (slot 19): Send(LogicalPacket*, reliable)
bool BaseApiSendLogical(uint8_t* api, void* packet, bool reliable) {
  void* connection = game::Field<void*>(api, kApiConnection);
  if (!connection) return false;
  int channel = reliable ? kChannelReliable1 : kChannelUnreliable;
  if (!game::Call<bool (*)(void*, int, void*)>(0x1403497c0)(connection, channel, packet)) return false;
  int length = reinterpret_cast<int (*)(void*)>((*static_cast<void***>(packet))[7])(packet);  // GetDataLen
  AddStatBytes(api, length);
  return true;
}

// 0x1415f4b30: UdpConnectionHandler::OnConnectComplete (on BaseApi + 0x80).
void BaseApiOnConnectComplete(uint8_t* handler) {
  uint8_t* api = handler - kSubobjectToBaseApi;
  ApiLog(game::Field<uint8_t*>(api, kApiLogContext), reinterpret_cast<const char*>(0x1424b0dd0),
         game::Field<const char*>(api, kApiAddressText));  // "BaseApi connection completed (%s) address=%s"
  game::Field<uint16_t>(api, kApiConnecting) = 0x100;  // connecting = false, connected = true
  reinterpret_cast<void (*)(uint8_t*)>((*reinterpret_cast<void***>(api))[kApiOnConnectSlot])(api);
}

// 0x1415f4160 (slot 8): Disconnect
void BaseApiDisconnect(uint8_t* api) {
  ApiLog(game::Field<uint8_t*>(api, kApiLogContext), reinterpret_cast<const char*>(0x1424b0650));  // "BaseApi disconnect request (%s)"
  game::Field<bool>(api, kApiAutoReconnect) = false;
  if (void* connection = game::Field<void*>(api, kApiConnection)) {
    ConnectionDisconnectGuarded(static_cast<udp::UdpConnection*>(connection), 0);
  }
}

// 0x1415f5350 (slot 7): Reconnect(autoReconnect)
void BaseApiReconnect(uint8_t* api, bool autoReconnect) {
  game::Field<bool>(api, kApiAutoReconnect) = autoReconnect;
  BaseApiConnect(api);
  ApiLog(game::Field<uint8_t*>(api, kApiLogContext), reinterpret_cast<const char*>(0x1424b0520),
         game::Field<const char*>(api, kApiAddressText), game::Field<int>(api, kApiTimeoutMs),
         static_cast<int>(game::Field<bool>(api, kApiAutoReconnect)));
}

// 0x1415f43a0 (slot 9): GiveTime. Drives auto-reconnect, or the shared
// manager while a connect is in progress.
void BaseApiGiveTime(uint8_t* api, int maxPollingMs) {
  if (!game::Field<bool>(api, kApiAttempting)) {
    if (!game::Field<bool>(api, kApiAutoReconnect) || game::Field<int>(api, kApiAddressCount) == 0) return;
    if (ElapsedMsSince(game::Field<int64_t>(api, kApiReconnectStart)) < game::Field<int>(api, kApiTimeoutMs)) return;
    ApiLog(game::Field<uint8_t*>(api, kApiLogContext), reinterpret_cast<const char*>(0x1424b06f0),
           game::Field<const char*>(api, kApiAddressText));  // "BaseApi auto-reconnect initiated (%s) address=%s"
    BaseApiConnect(api);
    return;
  }
  if (game::Field<bool>(api, kApiManagerOwned)) ManagerGiveTime(api, maxPollingMs);
}

// 0x1415f5de0 (slot 12): WaitForDisconnect(timeoutMs, forceDisconnect)
void BaseApiWaitForDisconnect(uint8_t* api, int timeoutMs, bool forceDisconnect) {
  int64_t start;
  soeutil::TimeNow(&start);
  while (ApiIsConnected(api) && !TimedOut(start, timeoutMs)) PumpOnce(api, 1000);
  if (forceDisconnect && (ApiIsConnected(api) || ApiIsConnecting(api))) {
    ApiVirtual<void (*)(uint8_t*)>(api, kApiDisconnectSlot)(api);
  }
}

// 0x1415f5ca0 (slot 13): WaitForConnect(timeoutMs, noPollingWait)
bool BaseApiWaitForConnect(uint8_t* api, int timeoutMs, bool noPollingWait) {
  uint8_t* logContext = game::Field<uint8_t*>(api, kApiLogContext);
  ApiLog(logContext, reinterpret_cast<const char*>(0x1424b0670), timeoutMs);  // "BaseApi waiting for connect (%s) timeout=%d"
  int pollingMs = noPollingWait ? 0 : 1000;
  int64_t start;
  soeutil::TimeNow(&start);
  while (ApiIsConnecting(api) && !TimedOut(start, timeoutMs)) PumpOnce(api, pollingMs);
  logContext = game::Field<uint8_t*>(api, kApiLogContext);
  if (logContext && game::Field<bool>(logContext, 0x288)) {
    bool attempting = ApiIsConnecting(api);
    bool connected = ApiIsConnected(api);
    ApiLog(logContext, reinterpret_cast<const char*>(0x1424b06a0), static_cast<int>(connected),
           static_cast<int>(attempting));  // "BaseApi finished waiting for connect (%s) IsConnected=%d IsAttempting=%d"
  }
  return ApiIsConnected(api);
}

// 0x1415f5ed0 (slot 14): WaitForFlush(timeoutMs) - until nothing is pending.
void BaseApiWaitForFlush(uint8_t* api, int timeoutMs) {
  int64_t start;
  soeutil::TimeNow(&start);
  while (!TimedOut(start, timeoutMs) && ApiIsConnected(api)) {
    auto* connection = game::Field<udp::UdpConnection*>(api, kApiConnection);
    if (game::Call<int (*)(udp::UdpConnection*)>(0x140349d80)(connection) == 0) break;  // TotalPendingBytes
    PumpOnce(api, 1000);
  }
}

// 0x1415f41c0: UdpConnection::Disconnect under the connection's guard, holding a reference.
void ConnectionDisconnectGuarded(udp::UdpConnection* connection, int flushTimeout) {
  auto** vtable = *reinterpret_cast<void***>(connection);
  reinterpret_cast<void (*)(udp::UdpConnection*)>(vtable[0])(connection);  // AddRef
  auto* guard = reinterpret_cast<udp::UdpPlatformGuardObject*>(reinterpret_cast<uint8_t*>(connection) + 0x2E0);
  guard->Enter();
  udp::ConnectionDisconnect(connection, flushTimeout, kReasonApplication);
  guard->Leave();
  reinterpret_cast<void (*)(udp::UdpConnection*)>((*reinterpret_cast<void***>(connection))[1])(connection);  // Release
}

// ClientServerCore RPC router (at handler + 0x10): packets whose 16-bit id
// matches a registered RPC handler are consumed before game dispatch.
struct RpcHandlerNode {
  void** vtable;  // slot 1: Handle(payload, payloadLength, packet)
  RpcHandlerNode* next;
  unsigned id;
};
static_assert(offsetof(RpcHandlerNode, id) == 0x10);

struct RpcRouter {
  int idEncoding;  // 0: big-endian u16 id, 1/2: little-endian s16 id
  int unknown4[9];
  int handlerCount;  // +0x28
  int padding;
  RpcHandlerNode* buckets[64];  // +0x30, by id & 63
};
static_assert(offsetof(RpcRouter, handlerCount) == 0x28);
static_assert(offsetof(RpcRouter, buckets) == 0x30);

// 0x1415f6db0: returns true if an RPC handler took the packet. The handler
// receives the payload after the 2-byte id and 2 more header bytes.
bool RpcRoutePacket(RpcRouter* router, const uint8_t* data, int length) {
  if (router->handlerCount == 0) return false;
  int encoding = router->idEncoding;
  if (encoding != 0 && encoding != 1 && encoding != 2) return false;
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 2;
  unsigned id;
  if (end < cursor) {
    id = 0;
    cursor = end;
  } else if (encoding == 0) {
    id = static_cast<unsigned>(static_cast<int>(static_cast<int16_t>(data[0] << 8)) | data[1]);
  } else {
    id = static_cast<unsigned>(static_cast<int>(*reinterpret_cast<const int16_t*>(data)));
  }
  for (RpcHandlerNode* node = router->buckets[id & 0x3F]; node; node = node->next) {
    if (node->id != id) continue;
    const uint8_t* payload = cursor + 2;
    if (end < payload) payload = end;
    int consumed = static_cast<int>(reinterpret_cast<uintptr_t>(payload)) - static_cast<int>(reinterpret_cast<uintptr_t>(data));
    using HandleFn = void (*)(RpcHandlerNode*, const uint8_t*, int, const uint8_t*);
    reinterpret_cast<HandleFn>(node->vtable[1])(node, data + consumed, length - consumed, data);
    return true;
  }
  return false;
}

// 0x14165b970: append a hex dump ("xx xx ...") of `length` bytes to `out`.
const char* HexDump(const uint8_t* data, int length, soeutil::IString* out) {
  const char* digits = reinterpret_cast<const char*>(0x142236b78);  // "0123456789abcdef"
  using AppendFn = void (*)(soeutil::IString*, const char*, ...);
  auto append = game::Call<AppendFn>(0x1402ed6c0);  // IString::AppendFormat
  int whole = length - length % 8;
  for (int offset = 0; offset < whole; offset += 8) {
    const uint8_t* b = data + offset;
    const char* separator = reinterpret_cast<const char*>(offset > 0 ? 0x14204c8cc : 0x142046fcb);  // " " / ""
    append(out, reinterpret_cast<const char*>(0x1424c22c0), separator,  // "%s%c%c %c%c ..."
           digits[b[0] >> 4], digits[b[0] & 0xF], digits[b[1] >> 4], digits[b[1] & 0xF], digits[b[2] >> 4],
           digits[b[2] & 0xF], digits[b[3] >> 4], digits[b[3] & 0xF], digits[b[4] >> 4], digits[b[4] & 0xF],
           digits[b[5] >> 4], digits[b[5] & 0xF], digits[b[6] >> 4], digits[b[6] & 0xF], digits[b[7] >> 4],
           digits[b[7] & 0xF]);
  }
  for (int i = whole; i < length; ++i) {
    append(out, reinterpret_cast<const char*>(0x1424c22ec), digits[data[i] >> 4], digits[data[i] & 0xF]);  // " %c%c"
  }
  return out->data;
}

// 0x1415f4b90: UdpConnectionHandler::OnRoutePacket (on BaseApi + 0x80).
// RPC packets are consumed by the RPC handler; everything else goes to the
// game's BaseApi::HandlePacket override (login / gateway dispatch).
void BaseApiOnRoutePacket(uint8_t* handler, void* /*connection*/, const uint8_t* data, int length) {
  void* stats = handler + kReceiveStats;
  reinterpret_cast<void (*)(void*, int)>((*static_cast<void***>(stats))[1])(stats, length);
  int64_t start;
  soeutil::TimeNow(&start);
  bool handled = RpcRoutePacket(reinterpret_cast<RpcRouter*>(handler + kRpcHandler), data, length);
  if (!handled) {
    uint8_t* api = handler - kSubobjectToBaseApi;
    using HandlePacketFn = void (*)(uint8_t*, const uint8_t*, int);
    reinterpret_cast<HandlePacketFn>((*reinterpret_cast<void***>(api))[kBaseApiHandlePacketSlot])(api, data, length);
  }
  int thresholdMs = game::Field<int>(handler, kSlowPacketMs);
  if (thresholdMs <= 0 || ElapsedMsSince(start) <= thresholdMs) return;
  int elapsedMs = ElapsedMsSince(start);
  int logBytes = game::Field<int>(handler, kSlowPacketLogBytes);
  if (length < logBytes) logBytes = length;
  soeutil::StringFixed<512> hex;
  hex.data = soeutil::EmptyStringData();
  hex.length = 0;
  hex.capacity = 0;
  hex.vtable = reinterpret_cast<void**>(kVtStringFixed512);
  HexDump(data, logBytes, &hex);
  using LogFn = void (*)(void*, const char*, ...);
  game::Call<LogFn>(0x1402ef740)(game::Field<void*>(handler, kLogger),
                                 reinterpret_cast<const char*>(0x1424b0c20),  // "BaseApi::OnRoutePacket processed a slow packet, ..."
                                 logBytes, length, elapsedMs, hex.data);
  hex.vtable = reinterpret_cast<void**>(kVtIStringFixed512);
  soeutil::StringRelease(&hex);
}

REBUILD_FUNCTION(BaseApi_IsConnecting, 0x1415f49f0, BaseApiIsConnecting);
REBUILD_FUNCTION(BaseApi_IsConnected, 0x1415f4a00, BaseApiIsConnected);
REBUILD_FUNCTION(BaseApi_Send, 0x1415f4920, BaseApiSend);
REBUILD_FUNCTION(BaseApi_SendLogical, 0x1415f4980, BaseApiSendLogical);
REBUILD_FUNCTION(BaseApi_OnConnectComplete, 0x1415f4b30, BaseApiOnConnectComplete);
REBUILD_FUNCTION(BaseApi_Disconnect, 0x1415f4160, BaseApiDisconnect);
REBUILD_FUNCTION(BaseApi_Reconnect, 0x1415f5350, BaseApiReconnect);
REBUILD_FUNCTION(BaseApi_Connect, 0x1415f4660, BaseApiConnect);
REBUILD_FUNCTION(BaseApi_OnTerminated, 0x1415f4d20, BaseApiOnTerminated);
REBUILD_FUNCTION(BaseApi_GiveTime, 0x1415f43a0, BaseApiGiveTime);
REBUILD_FUNCTION(BaseApi_WaitForDisconnect, 0x1415f5de0, BaseApiWaitForDisconnect);
REBUILD_FUNCTION(BaseApi_WaitForConnect, 0x1415f5ca0, BaseApiWaitForConnect);
REBUILD_FUNCTION(BaseApi_WaitForFlush, 0x1415f5ed0, BaseApiWaitForFlush);
REBUILD_FUNCTION(UdpConnection_DisconnectGuarded, 0x1415f41c0, ConnectionDisconnectGuarded);
// 0x1415f6810: send a header-only packet {u16 opcode, u16 0} through the
// endpoint's sender (+0x08, slot 1). Byte order at +0x00: 0 big-endian
// opcode, 1/2 native; anything else sends nothing and returns false.
bool RpcSendHeaderOnly(uint8_t* endpoint, uint16_t opcode) {
  int byteOrder = game::Field<int>(endpoint, 0);
  if (byteOrder < 0 || byteOrder > 2) return false;
  soeutil::ByteStream stream;
  stream.inlineArray.data = nullptr;
  stream.inlineArray.size = 0;
  stream.inlineArray.unknown14 = 0;
  stream.inlineArray.vtable = reinterpret_cast<void**>(soeutil::kVtByteArray8k);
  stream.unknown202C = 0;
  stream.maxSize = soeutil::kByteStreamMaxSize;
  stream.writePos = 0;
  stream.array = &stream.inlineArray;
  uint16_t word = byteOrder == 0 ? static_cast<uint16_t>((opcode << 8) | (opcode >> 8)) : opcode;
  soeutil::ByteArrayWrite(&stream.inlineArray, 0, &word, 2);  // unclamped
  stream.writePos += 2;
  uint16_t zero = 0;
  soeutil::StreamPut(&stream, &zero, 2);
  int size = stream.array->size;
  const uint8_t* bytes = size != 0 ? stream.array->data : nullptr;
  uint8_t* sender = game::Field<uint8_t*>(endpoint, 8);
  using SendFn = bool (*)(uint8_t*, uint8_t*, const uint8_t*, int);
  bool sent = reinterpret_cast<SendFn>((*reinterpret_cast<void***>(sender))[1])(sender, endpoint, bytes, size);
  soeutil::ByteArrayDestroy(&stream.inlineArray);
  return sent;
}

REBUILD_FUNCTION(ClientServerCore_RpcRoutePacket, 0x1415f6db0, RpcRoutePacket);
REBUILD_FUNCTION(ClientServerCore_RpcSendHeaderOnly, 0x1415f6810, RpcSendHeaderOnly);
REBUILD_FUNCTION(SoeUtil_HexDump, 0x14165b970, HexDump);
REBUILD_FUNCTION(BaseApi_OnRoutePacket, 0x1415f4b90, BaseApiOnRoutePacket);
REBUILD_FUNCTION(UdpCompressionHandler_Encrypt, 0x1415f3060, CompressionEncrypt);
REBUILD_FUNCTION(UdpCompressionHandler_Decrypt, 0x1415f2fa0, CompressionDecrypt);

}  // namespace rebuild::csc
