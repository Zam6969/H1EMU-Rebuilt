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
#include "soeutil/Mutex.h"
#include "soeutil/String.h"

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

REBUILD_FUNCTION(ClientServerCore_RpcRoutePacket, 0x1415f6db0, RpcRoutePacket);
REBUILD_FUNCTION(SoeUtil_HexDump, 0x14165b970, HexDump);
REBUILD_FUNCTION(BaseApi_OnRoutePacket, 0x1415f4b90, BaseApiOnRoutePacket);
REBUILD_FUNCTION(UdpCompressionHandler_Encrypt, 0x1415f3060, CompressionEncrypt);
REBUILD_FUNCTION(UdpCompressionHandler_Decrypt, 0x1415f2fa0, CompressionDecrypt);

}  // namespace rebuild::csc
