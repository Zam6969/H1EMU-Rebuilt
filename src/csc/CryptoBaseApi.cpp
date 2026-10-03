// ClientServerCrypto::CryptoBaseApi: BaseApi plus two cipher objects - the
// session cipher (+0xD28) and the "heavy encryption" cipher used by
// SendPacketSecure (+0xD30). Login and gateway APIs derive from it.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"

namespace rebuild::csc {
namespace {

constexpr size_t kSessionCipher = 0xD28;
constexpr size_t kSecureCipher = 0xD30;

// ClientServerCrypto key description: {.., +0x60 cipher type, +0x64 key bits}.
constexpr size_t kKeyCipherType = 0x60;
constexpr size_t kKeyBits = 0x64;

void DeleteObject(void* object) {
  if (object) reinterpret_cast<void (*)(void*, int)>((*static_cast<void***>(object))[0])(object, 1);
}

}  // namespace

// Crypto::ArraySecure<unsigned char,64,1>: key byte storage {vtable, data, size, capacity}.
struct SecureByteArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};
static_assert(sizeof(SecureByteArray) == 0x18);

// 0x1415f9ac0: ArraySecure()
SecureByteArray* SecureByteArrayConstruct(SecureByteArray* array) {
  array->data = nullptr;
  *reinterpret_cast<uint64_t*>(&array->size) = 0;
  array->vtable = reinterpret_cast<void**>(0x1424b3258);
  return array;
}

// 0x1415f9e40: assign key bytes (copy from `source`).
void SecureByteArrayAssign(SecureByteArray* array, const void* source) {
  game::Call<void (*)(const void*, SecureByteArray*)>(0x14166ad30)(source, array);
}

// 0x1415f9ea0: true if any key byte is non-zero.
bool SecureByteArrayHasKey(const SecureByteArray* array) {
  int size = array->size;
  if (size <= 0) return false;
  int zeros = 0;
  for (int i = 0; i < size && array->data[i] == 0; ++i) ++zeros;
  return zeros != size;
}

// 0x1415fa3d0: cipher factory by type (1, 2, 3); null for unknown types.
void* CreateCipher(int type) {
  struct Kind {
    size_t size;
    uintptr_t constructor;
  };
  Kind kind;
  if (type == 1) kind = {0x38, 0x1415fc490};
  else if (type == 2) kind = {0xF8, 0x1415fcaf0};
  else if (type == 3) kind = {0x20, 0x1415fddf0};
  else return nullptr;
  void* memory = soeutil::Allocate(kind.size);
  return memory ? game::Call<void* (*)(void*)>(kind.constructor)(memory) : nullptr;
}

// 0x1415f8c20: ~CryptoBaseApi
void CryptoBaseApiDestroy(uint8_t* api) {
  game::Field<uintptr_t>(api, 0x00) = 0x1424b2f88;  // CryptoBaseApi vtables
  game::Field<uintptr_t>(api, 0x80) = 0x1424b3048;
  game::Field<uintptr_t>(api, 0x88) = 0x1424b3080;
  DeleteObject(game::Field<void*>(api, kSessionCipher));
  game::Field<void*>(api, kSessionCipher) = nullptr;
  DeleteObject(game::Field<void*>(api, kSecureCipher));
  game::Field<void*>(api, kSecureCipher) = nullptr;
  game::Call<void (*)(uint8_t*)>(0x1415f3a40)(api);  // ~BaseApi
}

// 0x1415f88b0: CryptoKey(keyData, cipherType, keyBits)
uint8_t* CryptoKeyConstruct(uint8_t* key, const void* keyData, int cipherType, int keyBits) {
  game::Call<void (*)(uint8_t*)>(0x1415f9ac0)(key);  // base key storage
  game::Field<int>(key, kKeyCipherType) = cipherType;
  game::Field<int>(key, kKeyBits) = keyBits;
  game::Call<void (*)(uint8_t*, const void*)>(0x1415f9e40)(key, keyData);  // set key bytes
  return key;
}

// 0x1415f8a30: CryptoKey::IsValid - key bytes present and a cipher type set.
bool CryptoKeyIsValid(uint8_t* key) {
  if (!game::Call<bool (*)(uint8_t*)>(0x1415f9ea0)(key)) return false;
  return game::Field<int>(key, kKeyCipherType) != 0;
}

// 0x1415f8d00: SetSessionKey(key) - replaces the session cipher; false if the
// key is invalid or the cipher rejects it.
bool CryptoBaseApiSetSessionKey(uint8_t* api, uint8_t* key) {
  if (void* old = game::Field<void*>(api, kSessionCipher)) {
    DeleteObject(old);
    game::Field<void*>(api, kSessionCipher) = nullptr;
  }
  if (CryptoKeyIsValid(key)) {
    void* cipher = game::Call<void* (*)(int)>(0x1415fa3d0)(game::Field<int>(key, kKeyCipherType));  // factory
    game::Field<void*>(api, kSessionCipher) = cipher;
    using InitFn = bool (*)(void*, uint8_t*, int);
    if (reinterpret_cast<InitFn>((*static_cast<void***>(cipher))[1])(cipher, key, game::Field<int>(key, kKeyBits))) {
      return true;
    }
  }
  DeleteObject(game::Field<void*>(api, kSessionCipher));
  game::Field<void*>(api, kSessionCipher) = nullptr;
  return false;
}

// 0x1415f8e70: encode with the secure cipher into `out` (an Array<uchar,8192>).
bool CryptoBaseApiEncodeSecure(uint8_t* api, const uint8_t* data, int length, void* out) {
  void* cipher = game::Field<void*>(api, kSecureCipher);
  if (!cipher) return false;
  using EncodeFn = int (*)(const uint8_t*, int, void*, int, void*);
  return game::Call<EncodeFn>(0x1415f96b0)(data, length, out, 1, cipher) == 1;
}

REBUILD_FUNCTION(Crypto_ArraySecure_Construct, 0x1415f9ac0, SecureByteArrayConstruct);
REBUILD_FUNCTION(Crypto_ArraySecure_Assign, 0x1415f9e40, SecureByteArrayAssign);
REBUILD_FUNCTION(Crypto_ArraySecure_HasKey, 0x1415f9ea0, SecureByteArrayHasKey);
REBUILD_FUNCTION(Crypto_CreateCipher, 0x1415fa3d0, CreateCipher);
REBUILD_FUNCTION(CryptoBaseApi_Destroy, 0x1415f8c20, CryptoBaseApiDestroy);
REBUILD_FUNCTION(CryptoKey_Construct, 0x1415f88b0, CryptoKeyConstruct);
REBUILD_FUNCTION(CryptoKey_IsValid, 0x1415f8a30, CryptoKeyIsValid);
REBUILD_FUNCTION(CryptoBaseApi_SetSessionKey, 0x1415f8d00, CryptoBaseApiSetSessionKey);
REBUILD_FUNCTION(CryptoBaseApi_EncodeSecure, 0x1415f8e70, CryptoBaseApiEncodeSecure);

}  // namespace rebuild::csc
