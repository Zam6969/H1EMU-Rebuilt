// Crypto::CipherCCM (cipher type 2): AES-CCM authenticated encryption with a
// 13-byte nonce (PRNG part + send counter) and a 16-byte tag appended to each
// message; receive side rejects replays by nonce counter. Encode / Decode are
// not rebuilt yet; the CCM primitives are a bundled crypto library.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Memory.h"

namespace rebuild::crypto {
namespace {

constexpr size_t kCcmContextSize = 0x160;

struct CipherCCM {
  void** vtable;
  uint8_t* context;        // +0x08 CCM context
  int keyBytes;            // +0x10
  int padding;
  uint64_t sendCounter;    // +0x18 nonce counter for outgoing messages
  uint64_t lastReceived;   // +0x20 highest accepted incoming counter
  uint64_t blocks;         // +0x28
  uint64_t blockWraps;     // +0x30
  uint8_t prng[0xB8];      // +0x38 Crypto::Prng
  bool ready;              // +0xF0
};
static_assert(offsetof(CipherCCM, keyBytes) == 0x10);
static_assert(offsetof(CipherCCM, sendCounter) == 0x18);
static_assert(offsetof(CipherCCM, blockWraps) == 0x30);
static_assert(offsetof(CipherCCM, prng) == 0x38);
static_assert(offsetof(CipherCCM, ready) == 0xF0);
static_assert(sizeof(CipherCCM) == 0xF8);

struct ByteArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};

constexpr uintptr_t kVtCipherCCM = 0x1424b3ad0;
constexpr uintptr_t kVtCipher = 0x1424b39c0;
constexpr int kCipherTypeCCM = 2;
constexpr int kOverhead = 0x1D;  // 16-byte tag + 13-byte nonce

uint64_t InitialCounter() { return *reinterpret_cast<uint64_t*>(0x143c77cb0); }

}  // namespace

// 0x1415fcaf0: CipherCCM()
CipherCCM* CipherCCMConstruct(CipherCCM* cipher) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherCCM);
  cipher->context = nullptr;
  cipher->keyBytes = 0;
  cipher->sendCounter = InitialCounter();
  cipher->lastReceived = InitialCounter();
  cipher->blocks = 0;
  cipher->blockWraps = 0;
  game::Call<void (*)(uint8_t*)>(0x141603120)(cipher->prng);  // Prng()
  cipher->ready = false;
  cipher->context = static_cast<uint8_t*>(soeutil::Allocate(kCcmContextSize));
  std::memset(cipher->context, 0, kCcmContextSize);
  return cipher;
}

// 0x1415fd130 (slot 0): scalar deleting destructor.
CipherCCM* CipherCCMDestroy(CipherCCM* cipher, unsigned flags) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherCCM);
  game::Call<void (*)(uint8_t*)>(0x1416044f0)(cipher->context);  // ccm_free
  soeutil::Free(cipher->context, kCcmContextSize);
  game::Call<void (*)(uint8_t*)>(0x141603220)(cipher->prng);  // ~Prng
  cipher->vtable = reinterpret_cast<void**>(kVtCipher);
  if (flags & 1) soeutil::Free(cipher, sizeof(CipherCCM));
  return cipher;
}

// 0x1415fd910 (slot 1): Init(key) - seeds the nonce PRNG, then sets the CCM key.
bool CipherCCMInit(CipherCCM* cipher, const ByteArray* key) {
  if (game::Call<bool (*)(const ByteArray*)>(0x1415f9ea0)(key) &&   // ArraySecure::HasKey
      game::Call<bool (*)(CipherCCM*)>(0x1415fdaf0)(cipher)) {      // seed PRNG
    cipher->keyBytes = key->size;
    const uint8_t* bytes = key->size ? key->data : nullptr;
    int result = game::Call<int (*)(const uint8_t*, int, uint8_t*)>(0x141604510)(bytes, key->size, cipher->context);
    cipher->ready = result == 0;  // ccm_setkey
  }
  return cipher->ready;
}

// 0x1415fd8b0 (slot 2): encoded size = plaintext + tag + nonce.
int CipherCCMEncodedSize(CipherCCM* /*cipher*/, int length) { return length + kOverhead; }
// 0x1415fd8a0 (slot 3)
int CipherCCMDecodedSize(CipherCCM* /*cipher*/, int length) { return length; }
// 0x1415fd980 (slot 6)
bool CipherCCMIsReady(CipherCCM* cipher) { return cipher->ready; }
// 0x1415fd8c0 (slot 7)
int CipherCCMType(CipherCCM* /*cipher*/) { return kCipherTypeCCM; }
// 0x1415fd850 (slot 8)
int CipherCCMKeyBytes(CipherCCM* cipher) { return cipher->keyBytes; }

// 0x1415fdcc0 (slot 9): NeedsRekey - same limits as CipherAES.
bool CipherCCMNeedsRekey(CipherCCM* cipher) {
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

// 0x1415fdae0 (slot 10)
bool CipherCCMFalse(CipherCCM* /*cipher*/) { return false; }

// SoeUtil::Array<unsigned char,13,1>: CCM nonce with 13 inline bytes at +0x18.
struct NonceArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
  uint8_t inlineBytes[13];
};
static_assert(offsetof(NonceArray, inlineBytes) == 0x18);

constexpr uintptr_t kVtNonceArray = 0x1424b3b58;
constexpr uintptr_t kVtByteArray0 = 0x14204adc8;

// 0x1415fcbb0: build the 13-byte nonce {u64 counter, u32 random, u8 extra}.
NonceArray* BuildNonce(NonceArray* nonce, const uint64_t* counter, const uint32_t* random, uint8_t extra) {
  soeutil::ByteStream stream;
  stream.maxSize = soeutil::kByteStreamMaxSize;
  nonce->data = nullptr;
  nonce->vtable = reinterpret_cast<void**>(kVtNonceArray);
  *reinterpret_cast<uint64_t*>(&nonce->size) = 0;
  stream.inlineArray.vtable = reinterpret_cast<void**>(soeutil::kVtByteArray8k);
  stream.inlineArray.data = nullptr;
  stream.array = &stream.inlineArray;
  *reinterpret_cast<uint64_t*>(&stream.inlineArray.size) = 0;
  stream.unknown202C = 0;
  stream.writePos = 0;
  uint64_t first = *counter;
  soeutil::ByteArrayWrite(stream.array, 0, &first, 8);  // unclamped
  stream.writePos += 8;
  uint32_t second = *random;
  soeutil::StreamPut(&stream, &second, 4);
  soeutil::StreamPut(&stream, &extra, 1);
  const uint8_t* bytes = stream.array->size ? stream.array->data : nullptr;
  game::Call<void (*)(NonceArray*, int, const void*, int)>(0x14030d520)(nonce, 0, bytes, stream.writePos);
  soeutil::ByteArrayDestroy(&stream.inlineArray);
  return nonce;
}

// 0x1415fce70: ~Array<unsigned char,13,1>
void DestroyNonce(NonceArray* nonce) {
  nonce->vtable = reinterpret_cast<void**>(kVtNonceArray);
  nonce->size = 0;
  auto free = [](void* memory) {
    if (soeutil::ThreadAllocatorCount() == 0) {
      soeutil::FreeArray(memory);
    } else {
      soeutil::MemoryFree(memory, 1);
    }
  };
  if (nonce->data != nonce->inlineBytes) free(nonce->data);
  nonce->data = nullptr;
  nonce->vtable = reinterpret_cast<void**>(kVtByteArray0);  // base Array<uchar,0,1> dtor
  nonce->size = 0;
  free(nullptr);
  nonce->data = nullptr;
}

// 0x1415fd8d0: add ceil(length / 16) to the block counter.
void CipherCCMCountBlocks(CipherCCM* cipher, int length) {
  int blocks = length / 16 + (length % 16 > 0 ? 1 : 0);
  uint64_t before = cipher->blocks;
  cipher->blocks += static_cast<int64_t>(blocks);
  if (cipher->blocks <= before) ++cipher->blockWraps;
}

REBUILD_FUNCTION(Crypto_CipherCCM_Construct, 0x1415fcaf0, CipherCCMConstruct);
REBUILD_FUNCTION(Crypto_CipherCCM_Destroy, 0x1415fd130, CipherCCMDestroy);
REBUILD_FUNCTION(Crypto_CipherCCM_Init, 0x1415fd910, CipherCCMInit);
REBUILD_FUNCTION(Crypto_CipherCCM_EncodedSize, 0x1415fd8b0, CipherCCMEncodedSize);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherCCM_DecodedSize, 0x1415fd8a0, CipherCCMDecodedSize);
REBUILD_FUNCTION(Crypto_CipherCCM_IsReady, 0x1415fd980, CipherCCMIsReady);
REBUILD_FUNCTION(Crypto_CipherCCM_Type, 0x1415fd8c0, CipherCCMType);
REBUILD_FUNCTION(Crypto_CipherCCM_KeyBytes, 0x1415fd850, CipherCCMKeyBytes);
REBUILD_FUNCTION(Crypto_CipherCCM_NeedsRekey, 0x1415fdcc0, CipherCCMNeedsRekey);
REBUILD_FUNCTION(Crypto_CipherCCM_BuildNonce, 0x1415fcbb0, BuildNonce);
REBUILD_FUNCTION(Crypto_NonceArray_Destroy, 0x1415fce70, DestroyNonce);
REBUILD_FUNCTION(Crypto_CipherCCM_CountBlocks, 0x1415fd8d0, CipherCCMCountBlocks);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherCCM_False, 0x1415fdae0, CipherCCMFalse);

}  // namespace rebuild::crypto
