// UdpConnection's packet encryption methods. Each has the signature
// int (UdpConnection*, uint8_t* dest, const uint8_t* src, int length) and
// returns the output length; the connection's encrypt/decrypt tables point
// at these (see ConnectionSetupEncryption).
#include <cstring>

#include "core/hook.h"
#include "udp/UdpConnection.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

uint32_t Load32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
void Store32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

uint32_t EncryptCode(UdpConnection* c) { return ConnField<uint32_t>(c, O::kEncryptCode); }
const uint8_t* XorKey(UdpConnection* c) { return ConnField<uint8_t*>(c, O::kXorBuffer); }

// The manager's UdpManagerHandler can supply its own cipher (vtable slots
// 2-5). Without a handler nothing is produced.
int UserCipher(UdpConnection* c, int slot, uint8_t* dest, const uint8_t* src, int length) {
  UdpRefCount* handler = ConnField<UdpManager*>(c, O::kManager)->Handler();
  if (!handler) return 0;
  using Fn = int (*)(UdpRefCount*, UdpConnection*, uint8_t*, const uint8_t*, int);
  return reinterpret_cast<Fn>(handler->vtable[slot])(handler, c, dest, src, length);
}

}  // namespace

// 0x140345d30 / 0x140346220
int EncryptNone(UdpConnection*, uint8_t* dest, const uint8_t* src, int length) {
  std::memcpy(dest, src, length);
  return length;
}
int DecryptNone(UdpConnection*, uint8_t* dest, const uint8_t* src, int length) {
  std::memcpy(dest, src, length);
  return length;
}

// 0x140345da0 / 0x140346290 / 0x140345d60 / 0x140346250
int EncryptUserSupplied(UdpConnection* c, uint8_t* d, const uint8_t* s, int n) { return UserCipher(c, 3, d, s, n); }
int DecryptUserSupplied(UdpConnection* c, uint8_t* d, const uint8_t* s, int n) { return UserCipher(c, 2, d, s, n); }
int EncryptUserSupplied2(UdpConnection* c, uint8_t* d, const uint8_t* s, int n) { return UserCipher(c, 5, d, s, n); }
int DecryptUserSupplied2(UdpConnection* c, uint8_t* d, const uint8_t* s, int n) { return UserCipher(c, 4, d, s, n); }

// 0x140345de0. Word-chained XOR: each 32-bit word is XORed with the previous
// plaintext word (the encrypt code for the first); trailing bytes with the
// low byte of the last plaintext word.
int EncryptXor(UdpConnection* c, uint8_t* dest, const uint8_t* src, int length) {
  const uint8_t* end = src + length;
  uint32_t previous = EncryptCode(c);
  for (; src + 4 <= end; src += 4, dest += 4) {
    uint32_t plain = Load32(src);
    Store32(dest, plain ^ previous);
    previous = plain;
  }
  uint8_t tail = static_cast<uint8_t>(previous);
  for (; src < end; ++src, ++dest) *dest = *src ^ tail;
  return length;
}

// 0x1403462d0
int DecryptXor(UdpConnection* c, uint8_t* dest, const uint8_t* src, int length) {
  const uint8_t* end = src + length;
  uint32_t previous = EncryptCode(c);
  for (; src + 4 <= end; src += 4, dest += 4) {
    previous ^= Load32(src);
    Store32(dest, previous);
  }
  uint8_t tail = static_cast<uint8_t>(previous);
  for (; src < end; ++src, ++dest) *dest = *src ^ tail;
  return length;
}

// 0x140345f00. Chained XOR plus the connection's random key stream;
// trailing bytes are XORed with the key stream only.
int EncryptXorBuffer(UdpConnection* c, uint8_t* dest, const uint8_t* src, int length) {
  const uint8_t* key = XorKey(c);
  const uint8_t* end = src + length;
  uint32_t previous = EncryptCode(c);
  for (; src + 4 <= end; src += 4, dest += 4, key += 4) {
    uint32_t plain = Load32(src);
    Store32(dest, Load32(key) ^ plain ^ previous);
    previous = plain;
  }
  for (; src < end; ++src, ++dest, ++key) *dest = *src ^ *key;
  return length;
}

// 0x1403463f0
int DecryptXorBuffer(UdpConnection* c, uint8_t* dest, const uint8_t* src, int length) {
  const uint8_t* key = XorKey(c);
  const uint8_t* end = src + length;
  uint32_t previous = EncryptCode(c);
  for (; src + 4 <= end; src += 4, dest += 4, key += 4) {
    previous = previous ^ Load32(src) ^ Load32(key);
    Store32(dest, previous);
  }
  for (; src < end; ++src, ++dest, ++key) *dest = *src ^ *key;
  return length;
}

REBUILD_FUNCTION(UdpConnection_EncryptNone, 0x140345d30, EncryptNone);
REBUILD_FUNCTION(UdpConnection_DecryptNone, 0x140346220, DecryptNone);
REBUILD_FUNCTION(UdpConnection_EncryptUserSupplied, 0x140345da0, EncryptUserSupplied);
REBUILD_FUNCTION(UdpConnection_DecryptUserSupplied, 0x140346290, DecryptUserSupplied);
REBUILD_FUNCTION(UdpConnection_EncryptUserSupplied2, 0x140345d60, EncryptUserSupplied2);
REBUILD_FUNCTION(UdpConnection_DecryptUserSupplied2, 0x140346250, DecryptUserSupplied2);
REBUILD_FUNCTION(UdpConnection_EncryptXor, 0x140345de0, EncryptXor);
REBUILD_FUNCTION(UdpConnection_DecryptXor, 0x1403462d0, DecryptXor);
REBUILD_FUNCTION(UdpConnection_EncryptXorBuffer, 0x140345f00, EncryptXorBuffer);
REBUILD_FUNCTION(UdpConnection_DecryptXorBuffer, 0x1403463f0, DecryptXorBuffer);

}  // namespace rebuild::udp
