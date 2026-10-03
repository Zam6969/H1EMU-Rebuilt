// ClientServerCrypto::CryptoBaseApi: BaseApi plus two cipher objects - the
// session cipher (+0xD28) and the "heavy encryption" cipher used by
// SendPacketSecure (+0xD30). Login and gateway APIs derive from it.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"

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
// SoeUtil::Array<unsigned char,0,1> as passed to WrapPacket.
struct PacketBytes {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};

constexpr uint64_t kWrapMagic0 = 0x62D0E3EF56D9433Aull;
constexpr uint64_t kWrapMagic1 = 0xBC20ED3ABCB2CC08ull;

void PacketBytesWrite(PacketBytes* array, int position, const void* source, int count) {
  game::Call<void (*)(PacketBytes*, int, const void*, int)>(0x14030d520)(array, position, source, count);
}
void PacketBytesAppend(PacketBytes* array, const void* source, int count) {
  game::Call<void (*)(PacketBytes*, const void*, int)>(0x140313f70)(array, source, count);
}

// The 17-byte wrap footer {u8 cipher type, 16-byte magic} appended to out.
void AppendWrapFooter(PacketBytes* out, uint8_t cipherType) {
  soeutil::ByteStream stream;
  stream.inlineArray.data = nullptr;
  stream.inlineArray.size = 0;
  stream.inlineArray.unknown14 = 0;
  stream.inlineArray.vtable = reinterpret_cast<void**>(soeutil::kVtByteArray8k);
  stream.unknown202C = 0;
  stream.maxSize = soeutil::kByteStreamMaxSize;
  stream.writePos = 0;
  stream.array = &stream.inlineArray;
  soeutil::ByteArrayWrite(&stream.inlineArray, 0, &cipherType, 1);  // unclamped
  stream.writePos += 1;
  uint64_t magic = kWrapMagic0;
  soeutil::StreamPut(&stream, &magic, 8);
  magic = kWrapMagic1;
  soeutil::StreamPut(&stream, &magic, 8);
  int size = stream.array->size;
  PacketBytesAppend(out, size != 0 ? stream.array->data : nullptr, size);
  soeutil::ByteArrayDestroy(&stream.inlineArray);
}

// 0x1415f96b0: PacketUtils::WrapPacket(data, length, out, cipherType, cipher).
// Type 0 copies a packet that already ends in the magic and appends the
// footer (returns 2 when it does not); other types encrypt with `cipher`
// (slot 6 ready, slot 4 Encode) then append the footer. Returns 1 on success.
int WrapPacket(const uint8_t* data, int length, PacketBytes* out, int cipherType, uint8_t* cipher) {
  if (cipherType == 0) {
    if (length < 0x11 || *reinterpret_cast<const uint64_t*>(data + (length - 0x10)) != kWrapMagic0 ||
        *reinterpret_cast<const uint64_t*>(data + (length - 8)) != kWrapMagic1)
      return 2;
    // Reserve room for data + footer, copy, then append the footer.
    game::Call<void (*)(PacketBytes*, int, bool)>(0x14030fea0)(out, 0x11 + length, true);
    PacketBytesWrite(out, 0, data, length);
    AppendWrapFooter(out, 0);
    return 1;
  }
  if (!cipher) return 0;
  void** vtable = *reinterpret_cast<void***>(cipher);
  if (!reinterpret_cast<bool (*)(uint8_t*)>(vtable[0x30 / 8])(cipher)) return 0;
  PacketBytesWrite(out, 0, data, length);
  int result = reinterpret_cast<int (*)(uint8_t*, const uint8_t*, int, PacketBytes*)>(vtable[0x20 / 8])(cipher, data, length, out);
  if (result != 1) {
    out->size = 0;
    game::Call<void (*)(const char*, const char*, int)>(0x1402bab70)(
        nullptr, reinterpret_cast<const char*>(0x1424b31c8), result);  // "PacketUtils::WrapPacket - Failed to encrypt data (%d)."
    return 0;
  }
  AppendWrapFooter(out, static_cast<uint8_t>(cipherType));
  return 1;
}

bool EndsWithWrapMagic(const uint8_t* data, int length) {
  return length >= 0x11 && *reinterpret_cast<const uint64_t*>(data + (length - 0x10)) == kWrapMagic0 &&
         *reinterpret_cast<const uint64_t*>(data + (length - 8)) == kWrapMagic1;
}

// 0x1415f95d0: PacketUtils::UnwrapPacket(data, length, &outLength, cipher).
// Unwrapped packets pass through; footer type 0 strips the footer, type 1
// decrypts in place with `cipher` (slot 6 ready, slot 5 Decode).
bool UnwrapPacket(uint8_t* data, int length, int* outLength, uint8_t* cipher) {
  if (!EndsWithWrapMagic(data, length)) {
    *outLength = length;
    return true;
  }
  int payload = length - 0x11;
  uint8_t type = data[payload];
  if (type == 0) {
    *outLength = payload;
    return true;
  }
  if (type == 1 && cipher) {
    void** vtable = *reinterpret_cast<void***>(cipher);
    if (reinterpret_cast<bool (*)(uint8_t*)>(vtable[0x30 / 8])(cipher)) {
      int decoded = 0;
      if (reinterpret_cast<int (*)(uint8_t*, uint8_t*, int, int*)>(vtable[0x28 / 8])(cipher, data, payload, &decoded) == 1) {
        *outLength = decoded;
        return true;
      }
    }
  }
  return false;
}

// 0x1415f8ec0 (CryptoBaseApi slot 20): Send(data, length, reliable). With the
// session cipher active (+0xD38) it defers to slot 21; otherwise packets that
// already carry the wrap magic are re-wrapped with the second cipher (+0xD30).
bool CryptoBaseApiSend(uint8_t* api, const uint8_t* data, int length, bool reliable) {
  if (game::Field<bool>(api, 0xD38)) {
    using SendFn = bool (*)(uint8_t*, const uint8_t*, int, bool);
    return reinterpret_cast<SendFn>((*reinterpret_cast<void***>(api))[0xA8 / 8])(api, data, length, reliable);
  }
  if (!game::Field<void*>(api, 0x2C0)) return false;  // no UdpConnection
  soeutil::ByteArray8k wrapped{reinterpret_cast<void**>(soeutil::kVtByteArray8k), nullptr, 0, 0, {}};
  uint8_t* cipher = game::Field<uint8_t*>(api, 0xD30);
  if (cipher && EndsWithWrapMagic(data, length)) {
    WrapPacket(data, length, reinterpret_cast<PacketBytes*>(&wrapped), 0, cipher);
    length = wrapped.size;
    data = length != 0 ? wrapped.data : nullptr;
  }
  bool sent = game::Call<bool (*)(uint8_t*, const uint8_t*, int, bool)>(0x1415f4920)(api, data, length, reliable);
  soeutil::ByteArrayDestroy(&wrapped);
  return sent;
}

// 0x1415f9330 (CryptoBaseApi, UdpConnectionHandler sub-object at +0x80,
// slot 1): decrypt / unwrap an incoming packet, then BaseApi::OnRoutePacket.
// A packet that fails to decode is hex-dumped (first 128 bytes) to the log.
void CryptoBaseApiOnRoutePacket(uint8_t* handler, void* connection, uint8_t* data, int length) {
  int routed = length;
  bool ok = true;
  uint8_t* secondCipher = game::Field<uint8_t*>(handler, 0xCB0);
  if (game::Field<bool>(handler, 0xCB8)) {
    uint8_t* session = game::Field<uint8_t*>(handler, 0xCA8);
    using DecodeFn = int (*)(uint8_t*, uint8_t*, int, int*);
    if (reinterpret_cast<DecodeFn>((*reinterpret_cast<void***>(session))[0x28 / 8])(session, data, length, &routed) != 1)
      ok = false;
    else if (secondCipher && EndsWithWrapMagic(data, routed))
      ok = game::Call<bool (*)(uint8_t*, int, int*, uint8_t*)>(0x1415f95d0)(data, routed, &routed, secondCipher);
  } else if (EndsWithWrapMagic(data, length)) {
    ok = game::Call<bool (*)(uint8_t*, int, int*, uint8_t*)>(0x1415f95d0)(data, length, &routed, secondCipher);
  }
  if (!ok) {
    soeutil::StringFixed<4096> text;
    text.vtable = reinterpret_cast<void**>(0x14204b038);
    text.data = soeutil::EmptyStringData();
    text.length = 0;
    text.capacity = 0;
    soeutil::StringFormat(&text, reinterpret_cast<const char*>(0x1424b30a0), length);  // "...Failed to decode a packet (%d bytes):"
    game::Call<void (*)(const uint8_t*, int, soeutil::IString*)>(0x14165b970)(data, length < 0x80 ? length : 0x80, &text);
    const char* channel = game::Field<const char*>(game::Field<uint8_t*>(handler, 0x250), 0x250);
    game::Call<void (*)(const char*, const char*, const char*)>(0x1402baba0)(channel, reinterpret_cast<const char*>(0x142046fb8),
                                                                           text.data);  // "%s"
    text.vtable = reinterpret_cast<void**>(0x14204b018);  // IStringFixed<char,4096>
    soeutil::StringRelease(&text);
    return;
  }
  game::Call<void (*)(uint8_t*, void*, uint8_t*, int)>(0x1415f4b90)(handler, connection, data, routed);
}

REBUILD_FUNCTION(CryptoBaseApi_SetSessionKey, 0x1415f8d00, CryptoBaseApiSetSessionKey);
REBUILD_FUNCTION(PacketUtils_WrapPacket, 0x1415f96b0, WrapPacket);
REBUILD_FUNCTION(PacketUtils_UnwrapPacket, 0x1415f95d0, UnwrapPacket);
REBUILD_FUNCTION(CryptoBaseApi_Send, 0x1415f8ec0, CryptoBaseApiSend);
REBUILD_FUNCTION(CryptoBaseApi_OnRoutePacket, 0x1415f9330, CryptoBaseApiOnRoutePacket);
REBUILD_FUNCTION(CryptoBaseApi_EncodeSecure, 0x1415f8e70, CryptoBaseApiEncodeSecure);

}  // namespace rebuild::csc
