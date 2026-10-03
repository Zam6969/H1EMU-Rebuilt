// Crypto::CipherRC4 (cipher type 3): one RC4 state per direction, both keyed
// from the same key bytes.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"

namespace rebuild::crypto {
namespace {

struct Rc4State {
  uint8_t s[256];
  uint8_t i;
  uint8_t j;
};
static_assert(sizeof(Rc4State) == 0x102);

struct CipherRC4 {
  void** vtable;
  Rc4State* encode;  // +0x08
  Rc4State* decode;  // +0x10
  int unknown18;     // +0x18 (returned by slot 8)
  bool ready;        // +0x1C
};
static_assert(offsetof(CipherRC4, decode) == 0x10);
static_assert(offsetof(CipherRC4, unknown18) == 0x18);
static_assert(offsetof(CipherRC4, ready) == 0x1C);
static_assert(sizeof(CipherRC4) == 0x20);

// Crypto::ArraySecure / SoeUtil byte array: {vtable, data, size, capacity}.
struct ByteArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};

constexpr uintptr_t kVtCipherRC4 = 0x1424b3bd0;
constexpr uintptr_t kVtCipher = 0x1424b39c0;
constexpr int kCipherTypeRC4 = 3;

}  // namespace

// 0x1415fde10: RC4 key schedule.
Rc4State* Rc4Init(Rc4State* state, const uint8_t* key, int keyLength) {
  state->i = 0;
  state->j = 0;
  for (unsigned n = 0; n < 0x100; ++n) state->s[n] = static_cast<uint8_t>(n);
  uint8_t j = 0;
  for (int n = 0; n < 0x100; ++n) {
    uint8_t value = state->s[n];
    j = static_cast<uint8_t>(j + key[n % keyLength] + value);
    state->s[n] = state->s[j];
    state->s[j] = value;
  }
  return state;
}

// 0x1415fdf50: RC4 keystream XOR (in may equal out).
void Rc4Crypt(Rc4State* state, const uint8_t* in, uint8_t* out, int length) {
  for (int n = 0; n < length; ++n) {
    state->i = static_cast<uint8_t>(state->i + 1);
    state->j = static_cast<uint8_t>(state->j + state->s[state->i]);
    uint8_t value = state->s[state->i];
    state->s[state->i] = state->s[state->j];
    state->s[state->j] = value;
    out[n] = state->s[static_cast<uint8_t>(state->s[state->i] + state->s[state->j])] ^ in[n];
  }
}

namespace {
Rc4State* NewState(const ByteArray* key) {
  void* memory = soeutil::Allocate(sizeof(Rc4State));
  if (!memory) return nullptr;
  const uint8_t* bytes = key->size ? key->data : nullptr;
  return Rc4Init(static_cast<Rc4State*>(memory), bytes, key->size);
}
}  // namespace

// 0x1415fddf0: CipherRC4()
CipherRC4* CipherRC4Construct(CipherRC4* cipher) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherRC4);
  cipher->encode = nullptr;
  cipher->decode = nullptr;
  cipher->unknown18 = 0;
  cipher->ready = false;
  return cipher;
}

// 0x1415fdef0 (slot 0): scalar deleting destructor.
CipherRC4* CipherRC4Destroy(CipherRC4* cipher, unsigned flags) {
  cipher->vtable = reinterpret_cast<void**>(kVtCipherRC4);
  soeutil::Free(cipher->encode, sizeof(Rc4State));
  soeutil::Free(cipher->decode, sizeof(Rc4State));
  cipher->vtable = reinterpret_cast<void**>(kVtCipher);
  if (flags & 1) soeutil::Free(cipher, sizeof(CipherRC4));
  return cipher;
}

// 0x1415fe0c0 (slot 1): Init(key, bits) - both directions keyed identically.
bool CipherRC4Init(CipherRC4* cipher, const ByteArray* key, int /*bits*/) {
  soeutil::Free(cipher->encode, sizeof(Rc4State));
  cipher->encode = NewState(key);
  soeutil::Free(cipher->decode, sizeof(Rc4State));
  cipher->decode = NewState(key);
  cipher->ready = true;
  return true;
}

// 0x1415fe090 / 0x1415fe0a0 (slots 3 / 2): encoded / decoded size = input size.
int CipherRC4SameSize(CipherRC4* /*cipher*/, int length) { return length; }

// 0x1415fe010 (slot 4): Encode(in, length, out array)
bool CipherRC4Encode(CipherRC4* cipher, const uint8_t* in, int length, ByteArray* out) {
  if (out->size < length) {
    game::Call<void (*)(ByteArray*, int)>(0x140339a70)(out, length);  // Array::Resize
  } else {
    out->size = length;
  }
  Rc4Crypt(cipher->encode, in, out->size ? out->data : nullptr, length);
  return true;
}

// 0x1415fdfd0 (slot 5): Decode in place.
bool CipherRC4Decode(CipherRC4* cipher, uint8_t* data, int length, int* outLength) {
  Rc4Crypt(cipher->decode, data, data, length);
  *outLength = length;
  return true;
}

// 0x1415fe180 (slot 6)
bool CipherRC4IsReady(CipherRC4* cipher) { return cipher->ready; }
// 0x1415fe0b0 (slot 7)
int CipherRC4Type(CipherRC4* /*cipher*/) { return kCipherTypeRC4; }
// 0x1415fe080 (slot 8)
int CipherRC4Value18(CipherRC4* cipher) { return cipher->unknown18; }
// 0x1415fe1a0 (slot 9)
bool CipherRC4False(CipherRC4* /*cipher*/) { return false; }

// 0x1415fe190 (slot 10): Rekey(key) = Init(key, 0) through the vtable.
bool CipherRC4Rekey(CipherRC4* cipher, const ByteArray* key) {
  using InitFn = bool (*)(CipherRC4*, const ByteArray*, int);
  return reinterpret_cast<InitFn>(cipher->vtable[1])(cipher, key, 0);
}

// 0x1415fc5b0: Crypto::Cipher base scalar deleting destructor.
void* CipherDestroy(void** cipher, unsigned flags) {
  *cipher = reinterpret_cast<void*>(kVtCipher);
  if (flags & 1) soeutil::Free(cipher, 8);
  return cipher;
}

REBUILD_FUNCTION(Crypto_Rc4Init, 0x1415fde10, Rc4Init);
REBUILD_FUNCTION(Crypto_Rc4Crypt, 0x1415fdf50, Rc4Crypt);
REBUILD_FUNCTION(Crypto_CipherRC4_Construct, 0x1415fddf0, CipherRC4Construct);
REBUILD_FUNCTION(Crypto_CipherRC4_Destroy, 0x1415fdef0, CipherRC4Destroy);
REBUILD_FUNCTION(Crypto_CipherRC4_Init, 0x1415fe0c0, CipherRC4Init);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherRC4_EncodedSize, 0x1415fe090, CipherRC4SameSize);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherRC4_DecodedSize, 0x1415fe0a0, CipherRC4SameSize);
REBUILD_FUNCTION(Crypto_CipherRC4_Encode, 0x1415fe010, CipherRC4Encode);
REBUILD_FUNCTION(Crypto_CipherRC4_Decode, 0x1415fdfd0, CipherRC4Decode);
REBUILD_FUNCTION(Crypto_CipherRC4_IsReady, 0x1415fe180, CipherRC4IsReady);
REBUILD_FUNCTION(Crypto_CipherRC4_Type, 0x1415fe0b0, CipherRC4Type);
REBUILD_FUNCTION(Crypto_CipherRC4_Value18, 0x1415fe080, CipherRC4Value18);
REBUILD_FUNCTION_TOO_SMALL(Crypto_CipherRC4_False, 0x1415fe1a0, CipherRC4False);
REBUILD_FUNCTION(Crypto_CipherRC4_Rekey, 0x1415fe190, CipherRC4Rekey);
REBUILD_FUNCTION(Crypto_Cipher_Destroy, 0x1415fc5b0, CipherDestroy);

}  // namespace rebuild::crypto
