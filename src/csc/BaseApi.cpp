// ClientServerCore: the game's networking layer on top of UdpLibrary.
//
// ClientServerCore::UdpCompressionHandler plugs into UdpLibrary's
// "user supplied" encryption slots to compress every packet: payloads get
// a flag byte, 01 = deflated (zlib level 6), 00 = stored.
// ClientServerCore::BaseApi is both the UdpManagerHandler and (through its
// subobject at +0x80) the UdpConnectionHandler; incoming game packets enter
// at BaseApi::OnRoutePacket.
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Mutex.h"

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

// BaseApi::OnRoutePacket (0x1415f4b90) is next: it needs SoeUtil::StringFixed<512>
// for its slow-packet log line, which is not rebuilt yet.

REBUILD_FUNCTION(UdpCompressionHandler_Encrypt, 0x1415f3060, CompressionEncrypt);
REBUILD_FUNCTION(UdpCompressionHandler_Decrypt, 0x1415f2fa0, CompressionDecrypt);

}  // namespace rebuild::csc
