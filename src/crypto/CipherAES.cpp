// Crypto::CipherAES (cipher type 1): AES-ECB over 16-byte blocks with a
// padding scheme that always adds 1..16 bytes and stores the pad length in the
// last byte. The AES block / key-schedule routines are a bundled crypto
// library and are still called by address.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"

namespace rebuild::crypto {
namespace {

constexpr size_t kAesContextSize = 0xF4;

struct CipherAES {
  void** vtable;
  uint8_t* encodeContext;  // +0x08
  uint8_t* decodeContext;  // +0x10
  int keyBytes;            // +0x18
  int padding;
  uint64_t blocks;         // +0x20 blocks processed
  uint64_t blockWraps;     // +0x28 times the counter wrapped
  bool ready;              // +0x30
};
static_assert(offsetof(CipherAES, keyBytes) == 0x18);
static_assert(offsetof(CipherAES, blocks) == 0x20);
static_assert(offsetof(CipherAES, ready) == 0x30);
static_assert(sizeof(CipherAES) == 0x38);

struct ByteArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};

constexpr uintptr_t kVtCipherAES = 0x1424b3a20;
constexpr uintptr_t kVtCipher = 0x1424b39c0;
constexpr uintptr_t kVtByteArray = 0x14204adc8;
constexpr int kCipherTypeAES = 1;
constexpr int kErrorFailed = -4;

using BlockFn = int (*)(const uint8_t*, uint8_t*, uint8_t*);

void CountBlocks(CipherAES* cipher, int length) {
  int blocks = length / 16 + (length % 16 > 0 ? 1 : 0);
  uint64_t before = cipher->blocks;
  cipher->blocks += static_cast<int64_t>(blocks);
  if (cipher->blocks <= before) ++cipher->blockWraps;
}

uint8_t* NewContext() {
  void* memory = soeutil::Allocate(kAesContextSize);
  if (memory) game::Call<int (*)(void*)>(0x1415fe1b0)(memory);  // context init (no-op)
  return static_cast<uint8_t*>(memory);
}

}  // namespace

// 0x1415fc490: CipherAES()
CipherAES* CipherAESConstruct(CipherAES* cipher) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherAES);
  cipher->keyBytes = 0;
  cipher->blocks = 0;
  cipher->blockWraps = 0;
  cipher->ready = false;
  cipher->encodeContext = NewContext();
  cipher->decodeContext = NewContext();
  return cipher;
}

// 0x1415fc5e0 (slot 0): scalar deleting destructor; key material is wiped.
CipherAES* CipherAESDestroy(CipherAES* cipher, unsigned flags) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherAES);
  std::memset(cipher->encodeContext, 0, kAesContextSize);
  std::memset(cipher->decodeContext, 0, kAesContextSize);
  soeutil::Free(cipher->encodeContext, kAesContextSize);
  soeutil::Free(cipher->decodeContext, kAesContextSize);
  cipher->vtable = reinterpret_cast<void**>(kVtCipher);
  if (flags & 1) soeutil::Free(cipher, sizeof(CipherAES));
  return cipher;
}

// 0x1415fc9d0 (slot 1): Init(key) - expand encrypt and decrypt key schedules.
bool CipherAESInit(CipherAES* cipher, const ByteArray* key) {
  std::memset(cipher->encodeContext, 0, kAesContextSize);
  std::memset(cipher->decodeContext, 0, kAesContextSize);
  if (!game::Call<bool (*)(const ByteArray*)>(0x1415f9ea0)(key)) {  // ArraySecure::HasKey
    cipher->ready = false;
    return cipher->ready;
  }
  cipher->keyBytes = key->size;
  const uint8_t* bytes = key->size ? key->data : nullptr;
  using KeyFn = int (*)(const uint8_t*, int, uint8_t*);
  if (game::Call<KeyFn>(0x1416002b0)(bytes, key->size, cipher->encodeContext) == 0) {  // set encrypt key
    bytes = key->size ? key->data : nullptr;
    cipher->ready = game::Call<KeyFn>(0x1415fe1c0)(bytes, cipher->keyBytes, cipher->decodeContext) == 0;  // set decrypt key
  }
  return cipher->ready;
}

// 0x1415fc960 (slot 2): encoded size - always pads up to the next 16 bytes.
int CipherAESEncodedSize(CipherAES* /*cipher*/, int length) { return length - length % 16 + 16; }

// 0x1415fc950 (slot 3): decoded buffer size - same as slot 2.
int CipherAESDecodedSize(CipherAES* cipher, int length) {
  return reinterpret_cast<int (*)(CipherAES*, int)>(cipher->vtable[2])(cipher, length);
}

// 0x1415fc790 (slot 4): Encode(in, length, out array)
int CipherAESEncode(CipherAES* cipher, const uint8_t* in, int length, ByteArray* out) {
  int padded = reinterpret_cast<int (*)(CipherAES*, int)>(cipher->vtable[2])(cipher, length);
  ByteArray plain{reinterpret_cast<void**>(kVtByteArray), nullptr, 0, 0};
  if (padded < 1) {
    plain.size = padded < 0 ? padded : 0;
  } else {
    game::Call<void (*)(ByteArray*, int)>(0x140655d70)(&plain, padded);  // zero-filled resize
  }
  plain.data[padded - 1] = static_cast<uint8_t>(padded - length);  // pad length
  game::Call<void (*)(ByteArray*, int, const void*, int)>(0x14030d520)(&plain, 0, in, length);
  if (out->size < padded) {
    game::Call<void (*)(ByteArray*, int)>(0x140339a70)(out, padded);
  } else {
    out->size = padded;
  }
  int result = kErrorFailed;
  if (length >= 1) {
    uint8_t* dst = out->size ? out->data : nullptr;
    const uint8_t* src = plain.size ? plain.data : nullptr;
    result = 1;
    for (int remaining = padded; remaining > 0;) {
      int chunk = remaining > 16 ? 16 : remaining;
      if (game::Call<BlockFn>(0x141602160)(src, dst, cipher->encodeContext) != 0) {  // AES encrypt block
        result = kErrorFailed;
        break;
      }
      dst += chunk;
      src += chunk;
      remaining -= chunk;
    }
    if (result == 1) CountBlocks(cipher, padded);
  }
  plain.vtable = reinterpret_cast<void**>(kVtByteArray);
  plain.size = 0;
  if (soeutil::ThreadAllocatorCount() == 0) {
    soeutil::FreeArray(plain.data);
  } else {
    soeutil::MemoryFree(plain.data, 1);
  }
  return result;
}

// 0x1415fc6a0 (slot 5): Decode in place; *outLength drops the padding.
int CipherAESDecode(CipherAES* cipher, uint8_t* data, int length, int* outLength) {
  if (length < 1) return kErrorFailed;
  uint8_t* block = data;
  for (int remaining = length; remaining > 0;) {
    int chunk = remaining > 16 ? 16 : remaining;
    if (game::Call<BlockFn>(0x141601170)(block, block, cipher->decodeContext) != 0) return kErrorFailed;  // AES decrypt
    remaining -= chunk;
    block += chunk;
  }
  CountBlocks(cipher, length);
  if (outLength) *outLength = length - data[length - 1];
  return 1;
}

// 0x1415fca80 (slot 6)
bool CipherAESIsReady(CipherAES* cipher) { return cipher->ready; }
// 0x1415fc980 (slot 7)
int CipherAESType(CipherAES* /*cipher*/) { return kCipherTypeAES; }
// 0x1415fc940 (slot 8)
int CipherAESKeyBytes(CipherAES* cipher) { return cipher->keyBytes; }

// 0x1415fcaa0 (slot 9): NeedsRekey - block-count limits per key size.
bool CipherAESNeedsRekey(CipherAES* cipher) {
  switch (cipher->keyBytes) {
    case 16:
      if (cipher->blocks != ~0ull && cipher->blockWraps == 0) return false;
      break;
    case 24:
      return cipher->blockWraps > 0x3E;
    case 32:
      return cipher->blockWraps > 0x7E;
  }
  return true;
}

// 0x1415fca90 (slot 10)
bool CipherAESFalse(CipherAES* /*cipher*/) { return false; }

REBUILD_FUNCTION(Crypto_CipherAES_Construct, 0x1415fc490, CipherAESConstruct);
REBUILD_FUNCTION(Crypto_CipherAES_Destroy, 0x1415fc5e0, CipherAESDestroy);
REBUILD_FUNCTION(Crypto_CipherAES_Init, 0x1415fc9d0, CipherAESInit);
REBUILD_FUNCTION(Crypto_CipherAES_EncodedSize, 0x1415fc960, CipherAESEncodedSize);
REBUILD_FUNCTION(Crypto_CipherAES_DecodedSize, 0x1415fc950, CipherAESDecodedSize);
REBUILD_FUNCTION(Crypto_CipherAES_Encode, 0x1415fc790, CipherAESEncode);
REBUILD_FUNCTION(Crypto_CipherAES_Decode, 0x1415fc6a0, CipherAESDecode);
REBUILD_FUNCTION(Crypto_CipherAES_IsReady, 0x1415fca80, CipherAESIsReady);
REBUILD_FUNCTION(Crypto_CipherAES_Type, 0x1415fc980, CipherAESType);
REBUILD_FUNCTION(Crypto_CipherAES_KeyBytes, 0x1415fc940, CipherAESKeyBytes);
REBUILD_FUNCTION(Crypto_CipherAES_NeedsRekey, 0x1415fcaa0, CipherAESNeedsRekey);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherAES_False, 0x1415fca90, CipherAESFalse);

}  // namespace rebuild::crypto
