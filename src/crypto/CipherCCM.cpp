// Crypto::CipherCCM (cipher type 2): AES-CCM authenticated encryption with a
// 13-byte nonce (PRNG part + send counter) and a 16-byte tag appended to each
// message; receive side rejects replays by nonce counter. The CCM primitives
// themselves (0x141604450 encrypt+tag, 0x141604330 decrypt+verify) are a
// bundled crypto library and are still called by address.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Memory.h"
#include "soeutil/Mutex.h"

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

// SoeUtil::Array<unsigned char,16,1>: the CCM tag with 16 inline bytes.
struct TagArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
  uint8_t inlineBytes[16];
};
static_assert(offsetof(TagArray, inlineBytes) == 0x18);

constexpr uintptr_t kVtTagArray = 0x1424b3b30;
constexpr int kNonceBytes = 13;
constexpr int kTagBytes = 16;

void WriteBytes(void* array, int position, const void* source, int count) {
  game::Call<void (*)(void*, int, const void*, int)>(0x14030d520)(array, position, source, count);
}

// Wipes the nonce's 13 bytes (not null-checked, as in the original).
void WipeNonce(NonceArray* nonce) {
  uint8_t* bytes = nonce->size != 0 ? nonce->data : nullptr;
  *reinterpret_cast<uint64_t*>(bytes) = 0;
  *reinterpret_cast<uint32_t*>(bytes + 8) = 0;
  bytes[12] = 0;
}

// 0x1415fd520 (slot 4): Encode(plain, length, out) -> out = ciphertext | tag |
// nonce. Returns 1, or -4 if the CCM primitive fails.
int CipherCCMEncode(CipherCCM* cipher, const uint8_t* plain, int length, ByteArray* out) {
  int size = reinterpret_cast<int (*)(CipherCCM*, int)>(cipher->vtable[2])(cipher, length);
  if (size > out->size)
    game::Call<void (*)(ByteArray*, int)>(0x140339a70)(out, size);  // Array::Resize
  else
    out->size = size;
  uint64_t randomSlot;
  using RandomFn = uint64_t* (*)(uint8_t*, uint64_t*, int);
  uint64_t random = *reinterpret_cast<RandomFn>((*reinterpret_cast<void***>(cipher->prng))[2])(cipher->prng, &randomSlot, 8);
  uint64_t counter = ++cipher->sendCounter;
  uint32_t random32 = static_cast<uint32_t>(random);
  NonceArray nonce;
  BuildNonce(&nonce, &counter, &random32, static_cast<uint8_t>(random >> 32));
  uint8_t* outBytes = out->size != 0 ? out->data : nullptr;
  const uint8_t* nonceBytes = nonce.size != 0 ? nonce.data : nullptr;
  using EncryptFn = int (*)(const uint8_t*, int, const uint8_t*, int, const uint8_t*, int, uint8_t*, uint8_t*, int, uint8_t*);
  int result;
  if (game::Call<EncryptFn>(0x141604450)(nonceBytes, kNonceBytes, nullptr, 0, plain, length, outBytes,
                                         outBytes + length, kTagBytes, cipher->context) != 0) {
    result = -4;
  } else {
    WriteBytes(out, length + kTagBytes, nonce.size != 0 ? nonce.data : nullptr, kNonceBytes);
    CipherCCMCountBlocks(cipher, length);
    result = 1;
  }
  WipeNonce(&nonce);
  DestroyNonce(&nonce);
  return result;
}

bool MatchesStatic(const uint8_t* bytes, uintptr_t reference, int count) {
  return std::memcmp(bytes, reinterpret_cast<const void*>(reference), count) == 0;
}

// 0x1415fd200 (slot 5): Decode(data, length, &plainLength) in place. Returns
// 1; -2 bad/zero nonce, -1 bad/zero tag, -3 replayed counter, -4 auth failure.
int CipherCCMDecode(CipherCCM* cipher, uint8_t* data, int length, int* plainLength) {
  int plain = length - (kTagBytes + kNonceBytes);
  const uint8_t* trailer = data + plain;
  TagArray tag{reinterpret_cast<void**>(kVtTagArray), nullptr, 0, 0, {}};
  WriteBytes(&tag, 0, trailer, kTagBytes);
  NonceArray nonce;
  nonce.vtable = reinterpret_cast<void**>(kVtNonceArray);
  nonce.data = nullptr;
  nonce.size = 0;
  nonce.capacity = 0;
  WriteBytes(&nonce, 0, trailer + kTagBytes, kNonceBytes);
  int result;
  // Compare against the all-zero nonce / tag constants (8+4+1 and 8+8 bytes).
  if (nonce.size != kNonceBytes || (MatchesStatic(nonce.data, 0x143c77c80, 8) && MatchesStatic(nonce.data + 8, 0x143c77c88, 4) &&
                                    nonce.data[12] == *reinterpret_cast<const uint8_t*>(0x143c77c8c))) {
    result = -2;
  } else if (tag.size != kTagBytes ||
             (MatchesStatic(tag.data, 0x143c77c70, 8) && MatchesStatic(tag.data + 8, 0x143c77c78, 8))) {
    result = -1;
  } else {
    uint64_t counter = *reinterpret_cast<uint64_t*>(nonce.data);
    if (counter <= cipher->lastReceived) {
      result = -3;
    } else {
      using DecryptFn = int (*)(const uint8_t*, int, const uint8_t*, int, const uint8_t*, int, uint8_t*, const uint8_t*, int, uint8_t*);
      if (game::Call<DecryptFn>(0x141604330)(nonce.data, kNonceBytes, nullptr, 0, data, plain, data, tag.data, kTagBytes,
                                             cipher->context) != 0) {
        result = -4;
      } else {
        const uint8_t* bytes = nonce.size != 0 ? nonce.data : nullptr;
        cipher->lastReceived = bytes + 8 <= bytes + nonce.size ? *reinterpret_cast<const uint64_t*>(bytes) : 0;
        *plainLength = plain;
        CipherCCMCountBlocks(cipher, plain);
        result = 1;
      }
    }
  }
  WipeNonce(&nonce);
  DestroyNonce(&nonce);
  uint8_t* tagBytes = tag.size != 0 ? tag.data : nullptr;
  *reinterpret_cast<uint64_t*>(tagBytes) = 0;
  *reinterpret_cast<uint64_t*>(tagBytes + 8) = 0;
  game::Call<void (*)(TagArray*)>(0x1415fcdd0)(&tag);
  return result;
}


// Crypto::ArraySecure<unsigned char,32,1> as built on the stack by the seeder.
struct SeedArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
  uint8_t inlineBytes[32];
};
static_assert(offsetof(SeedArray, inlineBytes) == 0x18 && sizeof(SeedArray) == 0x38);

void FreeSeedStorage(void* memory) {
  if (*reinterpret_cast<uint64_t*>(0x143e09638) == 0)
    soeutil::FreeArray(memory);
  else
    soeutil::MemoryFree(memory, 1);
}

// 0x1415fdaf0: seed the nonce PRNG (slot 1 of the Prng at +0x38) with 8 words
// from SoeUtil's shared LCG (0x142b06c38, guarded by 0x142b06bf0), each XORed
// with 0x14032e6c0().
bool CipherCCMSeedPrng(CipherCCM* cipher) {
  uint32_t salt = static_cast<uint32_t>(game::Call<uint64_t (*)()>(0x14032e6c0)());
  SeedArray seed{reinterpret_cast<void**>(0x1424b3ba8), nullptr, 0, 0, {}};
  auto* mutex = reinterpret_cast<CRITICAL_SECTION*>(0x142b06bf0);
  auto& state = *reinterpret_cast<uint32_t*>(0x142b06c38);
  for (int i = 0; i < 8; ++i) {
    soeutil::MutexLock(mutex);
    uint32_t word = state * 0x7FF8A3ED + 0x2AA01D31;
    state = word;
    soeutil::MutexUnlock(mutex);
    word ^= salt;
    if (seed.size + 4 > seed.capacity) {
      int capacity;
      using AllocFn = uint8_t* (*)(SeedArray*, int, int*, bool);
      uint8_t* grown = reinterpret_cast<AllocFn>(seed.vtable[1])(&seed, seed.size + 4, &capacity, false);
      if (grown != seed.data) {
        if (seed.data) {
          game::Call<void (*)(void*, const void*, size_t)>(0x140d11e20)(grown, seed.data, seed.size);  // memcpy
          reinterpret_cast<void (*)(SeedArray*, uint8_t*, int)>(seed.vtable[2])(&seed, seed.data, seed.capacity);
        }
        seed.data = grown;
        seed.capacity = capacity;
      }
    }
    *reinterpret_cast<uint32_t*>(seed.data + seed.size) = word;
    seed.size += 4;
  }
  using SeedFn = bool (*)(uint8_t*, SeedArray*);
  bool ok = reinterpret_cast<SeedFn>((*reinterpret_cast<void***>(cipher->prng))[1])(cipher->prng, &seed);
  // Inlined ~ArraySecure<uchar,32,1> / ~Array<uchar,32,1> / ~Array<uchar,0,1>.
  seed.vtable = reinterpret_cast<void**>(0x1424b3ba8);
  seed.size = 0;
  if (seed.data != seed.inlineBytes) FreeSeedStorage(seed.data);
  seed.data = nullptr;
  seed.vtable = reinterpret_cast<void**>(0x1424b3b80);
  seed.size = 0;
  FreeSeedStorage(nullptr);
  seed.data = nullptr;
  seed.vtable = reinterpret_cast<void**>(0x14204adc8);
  seed.size = 0;
  FreeSeedStorage(nullptr);
  return ok;
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
REBUILD_FUNCTION(Crypto_CipherCCM_Encode, 0x1415fd520, CipherCCMEncode);
REBUILD_FUNCTION(Crypto_CipherCCM_Decode, 0x1415fd200, CipherCCMDecode);
REBUILD_FUNCTION(Crypto_CipherCCM_SeedPrng, 0x1415fdaf0, CipherCCMSeedPrng);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherCCM_False, 0x1415fdae0, CipherCCMFalse);

}  // namespace rebuild::crypto
