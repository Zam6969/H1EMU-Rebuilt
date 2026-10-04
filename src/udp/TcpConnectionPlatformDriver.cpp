// TcpLibrary::TcpConnectionPlatformDriver: the per-connection Winsock side
// (0x70 bytes, built inline by TcpPlatformDriver Accept / Connect).
#include <winsock2.h>
#include <windows.h>
#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::udp {
namespace {

struct TcpConnectionPlatformDriver {
  void** vtable;         // +0x00 0x1425abcb0
  void* platformDriver;  // +0x08 TcpPlatformDriver*
  void* addressLookup;   // +0x10 AsyncAddressResult* (shared) or null
  int optionB;           // +0x18
  int optionA;           // +0x1C
  SOCKET socket;         // +0x20
  void* owner;           // +0x28 TcpConnection*
  uint8_t pad30[0x28];
  int sslState;          // +0x58 2 = TLS established
  uint8_t pad5C[4];
  uint64_t timeout;      // +0x60
  bool flag68;           // +0x68
};
static_assert(offsetof(TcpConnectionPlatformDriver, socket) == 0x20 && offsetof(TcpConnectionPlatformDriver, sslState) == 0x58 &&
              offsetof(TcpConnectionPlatformDriver, flag68) == 0x68);

struct TcpAddress {
  uint8_t ip[4];
  uint16_t port;
};

}  // namespace

// 0x141ebc4a0 (slot 0): scalar deleting destructor.
void* TcpConnectionPlatformDriverDeletingDestructor(TcpConnectionPlatformDriver* self, unsigned flags) {
  game::Call<void (*)(void*)>(0x141ebc240)(self);
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, 0x70);
  return self;
}

// 0x141ebefd0 (slot 1): SetOwner.
void TcpConnectionPlatformDriverSetOwner(TcpConnectionPlatformDriver* self, void* owner) { self->owner = owner; }

// 0x141ebda30 / 0x141ebd930 (slots 3, 4): local / remote address.
bool TcpConnectionPlatformDriverGetLocalAddress(TcpConnectionPlatformDriver* self, TcpAddress* out) {
  int length = sizeof(sockaddr);
  sockaddr_in address{};
  if (getsockname(self->socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return false;
  std::memcpy(out->ip, &address.sin_addr, 4);
  out->port = ntohs(address.sin_port);
  return true;
}
bool TcpConnectionPlatformDriverGetRemoteAddress(TcpConnectionPlatformDriver* self, TcpAddress* out) {
  int length = sizeof(sockaddr);
  sockaddr_in address{};
  if (getpeername(self->socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return false;
  std::memcpy(out->ip, &address.sin_addr, 4);
  out->port = ntohs(address.sin_port);
  return true;
}

// 0x141ebeaa0 (slot 5): Send - through TLS (0x141ebead0) once established,
// else a plain send; returns bytes sent, 0 on error.
int TcpConnectionPlatformDriverSend(TcpConnectionPlatformDriver* self, const char* data, int length) {
  if (self->sslState == 2) return game::Call<int (*)(TcpConnectionPlatformDriver*, const char*, int)>(0x141ebead0)(self, data, length);
  int sent = send(self->socket, data, length, 0);
  return sent < 0 ? 0 : sent;
}

// 0x141ebe8e0 (slot 9): IsSecure - attached and TLS established.
bool TcpConnectionPlatformDriverIsSecure(TcpConnectionPlatformDriver* self) {
  return self->platformDriver && self->sslState == 2;
}

// 0x141ebdab0 (slot 10): local port (0 on error).
uint16_t TcpConnectionPlatformDriverGetLocalPort(TcpConnectionPlatformDriver* self) {
  int length = sizeof(sockaddr);
  sockaddr_in address{};
  if (getsockname(self->socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return 0;
  return ntohs(address.sin_port);
}

// 0x141ebdf20 (slot 2): GiveTime. Finishes a pending async-address connect,
// waits (zero-timeout select) for a non-blocking connect to complete, then
// drains the socket into the TcpConnection (through TLS when established).
// Failures report a disconnect reason through 0x141ec07a0(owner, 0, reason).
void TcpConnectionPlatformDriverGiveTime(TcpConnectionPlatformDriver* self) {
  using FailFn = void (*)(void*, int, int);
  auto fail = [&](int reason) { game::Call<FailFn>(0x141ec07a0)(self->owner, 0, reason); };
  auto releaseLookup = [&] { game::Call<void (*)(void*)>(0x141ebbf40)(&self->addressLookup); };
  if (!self->owner) return;
  if (auto* lookup = static_cast<uint8_t*>(self->addressLookup)) {
    if (!lookup[0x59]) return;  // address not resolved yet
    auto* address = game::Call<const TcpAddress* (*)(void*)>(0x141ec2cf0)(self->addressLookup);
    if (!address) {
      fail(0x11);
      releaseLookup();
      return;
    }
    if (*reinterpret_cast<int*>(static_cast<uint8_t*>(self->owner) + 0xBC) == 5) {
      void* name = nullptr;
      if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x18)) {
        void* text = (*reinterpret_cast<void* (***)(void*)>(self->addressLookup))[0x30 / 8](self->addressLookup);
        name = game::Call<void* (*)(void*, void*)>(0x1402ef7c0)(memory, text);
      }
      *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + 0x30) = name;
    }
    self->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (self->socket == INVALID_SOCKET) {
      WSAGetLastError();
      fail(0x12);
      releaseLookup();
      return;
    }
    if (!game::Call<bool (*)(SOCKET, int, int)>(0x141ebf630)(self->socket, self->optionA, self->optionB)) {
      closesocket(self->socket);
      fail(0x13);
      releaseLookup();
      return;
    }
    sockaddr_in to;
    to.sin_family = AF_INET;
    std::memcpy(&to.sin_addr, address->ip, 4);
    to.sin_port = htons(address->port);
    if (connect(self->socket, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
      closesocket(self->socket);
      fail(10);
      releaseLookup();
      return;
    }
    releaseLookup();
  }
  if (*reinterpret_cast<int*>(static_cast<uint8_t*>(self->owner) + 0x38) == 0) {  // still connecting
    fd_set writable;
    writable.fd_count = 1;
    writable.fd_array[0] = self->socket;
    fd_set failed;
    failed.fd_count = 1;
    failed.fd_array[0] = self->socket;
    timeval timeout{0, 0};
    int ready = select(0, nullptr, &writable, &failed, &timeout);
    if (ready == 0) return;
    if (ready == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (static_cast<unsigned>(error - WSAEWOULDBLOCK) <= 2 || error == WSAEINVAL) return;
      fail(7);
      return;
    }
    if (!__WSAFDIsSet(self->socket, &writable)) {
      if (__WSAFDIsSet(self->socket, &failed)) fail(0xB);
      return;
    }
    game::Call<void (*)(void*)>(0x141ec0a90)(self->owner);  // connected
  }
  if (self->sslState == 1) return;  // TLS handshake in progress
  char buffer[0x10000];  // 64 KB on the stack, as in the original
  while (self->owner) {
    int received = recv(self->socket, buffer, sizeof(buffer), 0);
    if (received == SOCKET_ERROR) {
      switch (WSAGetLastError()) {
        case 10000:
        case WSAEINTR:
        case WSAEWOULDBLOCK:
        case WSAEINPROGRESS:
        case WSAENOBUFS:
          return;
        case WSAECONNABORTED:
          fail(0x10);
          return;
        case WSAECONNRESET:
          fail(0xF);
          return;
        case WSAECONNREFUSED:
          fail(1);
          return;
        default:
          fail(6);
          return;
      }
    }
    if (received == 0) {
      game::Call<void (*)(void*)>(0x141ec0b00)(self->owner);  // closed by peer
      return;
    }
    if (self->sslState == 2)
      game::Call<void (*)(TcpConnectionPlatformDriver*, char*, int)>(0x141ebc7f0)(self, buffer, received);
    else
      game::Call<void (*)(void*, char*, int)>(0x141ec0ba0)(self->owner, buffer, received);
  }
}

namespace {
// SSPI buffer descriptors as the TLS code builds them on the stack.
struct SecBuffer {
  unsigned long size;
  unsigned long type;
  void* data;
};
struct SecBufferDesc {
  unsigned long version;
  unsigned long count;
  SecBuffer* buffers;
};
static_assert(sizeof(SecBuffer) == 0x10 && sizeof(SecBufferDesc) == 0x10);

uint8_t* SslFunctions(TcpConnectionPlatformDriver* self) {
  return *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self->platformDriver) + 0x10) + 0xE0);
}
int64_t TimeNow() {
  int64_t now;
  return *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now);
}
using LogFn = void (*)(const char*, const char*, ...);
const char* SslErrorLog() { return *reinterpret_cast<const char**>(0x142ad58d8); }  // "TcpLibrarySslErrors.log"
const char* SslLog() { return *reinterpret_cast<const char**>(0x142ad58e0); }       // "TcpLibrarySsl.log"

// Logs "<format>%s" with the SSPI status text (0x141ebdc50).
void LogSslStatus(uint64_t format, int status) {
  soeutil::StringFixed<256> text;
  soeutil::InitFixed(text, reinterpret_cast<void**>(0x142049e08));
  const char* description = game::Call<const char* (*)(int, soeutil::IString*)>(0x141ebdc50)(status, &text);
  game::Call<LogFn>(0x1402baba0)(SslErrorLog(), reinterpret_cast<const char*>(format), description);
  text.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&text);
}
}  // namespace

// 0x141ebf360 (slot 8): TerminateSslConnection - apply SCHANNEL_SHUTDOWN to
// the context, build the close_notify alert with InitializeSecurityContextW
// and send it.
void TcpConnectionPlatformDriverTerminateSsl(TcpConnectionPlatformDriver* self) {
  int64_t started = TimeNow();
  unsigned long shutdown = 1;  // SCHANNEL_SHUTDOWN
  SecBuffer buffer{4, 2, &shutdown};  // SECBUFFER_TOKEN
  SecBufferDesc description{0, 1, &buffer};
  void* context = reinterpret_cast<uint8_t*>(self) + 0x38;
  using ApplyFn = int (*)(void*, SecBufferDesc*);
  int status = (*reinterpret_cast<ApplyFn*>(SslFunctions(self) + 0x50))(context, &description);
  if (status < 0) {
    LogSslStatus(0x1425ac578, status);  // "Error terminating session ApplyControlToken - %s"
    return;
  }
  buffer = SecBuffer{0, 2, nullptr};
  description = SecBufferDesc{0, 1, &buffer};
  uint8_t* driverHandle = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self->platformDriver) + 0x10);
  unsigned long attributes;
  int64_t expiry;
  using InitContextFn = int (*)(void*, void*, const wchar_t*, unsigned long, unsigned long, unsigned long, void*, unsigned long, void*,
                                SecBufferDesc*, unsigned long*, int64_t*);
  status = (*reinterpret_cast<InitContextFn*>(*reinterpret_cast<uint8_t**>(driverHandle + 0xE0) + 0x30))(
      driverHandle + 0xD0, context, nullptr, 0xC11C, 0, 0x10, nullptr, 0, context, &description, &attributes, &expiry);
  if (status < 0) {
    LogSslStatus(0x1425ac5d8, status);  // "Error terminating session InitSecurityContext - %s"
  } else if (void* alert = buffer.data; alert && buffer.size) {
    int sent = send(self->socket, static_cast<const char*>(alert), static_cast<int>(buffer.size), 0);
    game::Call<LogFn>(0x140428ce0)(SslLog(), reinterpret_cast<const char*>(0x1425ac5b0), sent < 0 ? 0 : sent);  // "Sending close notify (%d bytes)."
    (*reinterpret_cast<int (**)(void*)>(SslFunctions(self) + 0x80))(alert);  // FreeContextBuffer
  }
  int64_t delta = TimeNow() - started;
  int elapsed = delta > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(delta);
  if (elapsed > 1000)
    game::Call<LogFn>(0x1402ef740)(SslErrorLog(), reinterpret_cast<const char*>(0x1425ac610), elapsed);  // "Warning! TerminateSslConnection took %d ms."
}

namespace {
// SoeUtil::WideStringFixed<256>: {vtable, data, length, capacity, inline}.
struct WideStringFixed256 {
  void** vtable;
  wchar_t* data;
  int length;
  int capacity;
  uint8_t inlineBuffer[256 * 2 + 4];
};
static_assert(offsetof(WideStringFixed256, inlineBuffer) == 0x18);

// Inline "make the buffer uniquely ours" step the game emits before handing
// the wide string to SSPI.
void MakeWideWritable(WideStringFixed256& text) {
  int needed = text.length + 1;
  if (needed <= text.capacity && !(text.capacity > 0 && reinterpret_cast<int*>(text.data)[-1] > 1)) return;
  int allocated = 0;
  bool isHeap = false;
  using AllocFn = int* (*)(WideStringFixed256*, int, int*, bool*);
  int* block = reinterpret_cast<AllocFn>(text.vtable[1])(&text, needed * 2 + 4, &allocated, &isHeap);
  if (block) _InterlockedExchange(reinterpret_cast<volatile long*>(block), isHeap ? 1 : 0);
  auto* copy = reinterpret_cast<wchar_t*>(block + 1);
  int length = text.length;
  std::memcpy(copy, text.data, static_cast<size_t>(length + 1) * 2);
  if (text.capacity > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(text.data) - 1, -1) - 1 <= 0)
    reinterpret_cast<void (*)(WideStringFixed256*)>(text.vtable[2])(&text);
  text.data = copy;
  text.capacity = static_cast<int>((static_cast<int64_t>(allocated) - 4) >> 1);
  text.length = length;
}
}  // namespace

// 0x141ebefe0 (slot 6): StartSslClientHandshake - first
// InitializeSecurityContextW call for the server name, send the ClientHello
// and switch to handshake state 1 with a 64 KB receive buffer (LocalAlloc).
bool TcpConnectionPlatformDriverStartSslHandshake(TcpConnectionPlatformDriver* self) {
  int64_t started = TimeNow();
  self->timeout = static_cast<uint64_t>(TimeNow());
  SecBuffer token{0, 2, nullptr};
  SecBufferDesc output{0, 1, &token};
  WideStringFixed256 serverName{reinterpret_cast<void**>(0x14204f158), reinterpret_cast<wchar_t*>(0x142ae85c8), 0, 0, {}};
  void* name = *reinterpret_cast<void**>(*reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(self) + 0x30) + 8);
  game::Call<void (*)(void*, WideStringFixed256*)>(0x140339e50)(name, &serverName);  // to wide
  MakeWideWritable(serverName);
  uint8_t* driverHandle = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(self->platformDriver) + 0x10);
  void* context = reinterpret_cast<uint8_t*>(self) + 0x38;
  unsigned long attributes;
  int64_t expiry;
  using InitContextFn = int (*)(void*, void*, const wchar_t*, unsigned long, unsigned long, unsigned long, void*, unsigned long, void*,
                                SecBufferDesc*, unsigned long*, int64_t*);
  int status = (*reinterpret_cast<InitContextFn*>(*reinterpret_cast<uint8_t**>(driverHandle + 0xE0) + 0x30))(
      driverHandle + 0xD0, nullptr, serverName.data, 0xC11C, 0, 0x10, nullptr, 0, context, &output, &attributes, &expiry);
  bool ok;
  if (status != 0x90312) {  // SEC_I_CONTINUE_NEEDED
    game::Call<LogFn>(0x1402baba0)(SslErrorLog(), reinterpret_cast<const char*>(0x1425ac300), status);  // "InitializeSecurityContext failed - %d"
    ok = false;
  } else {
    ok = true;
    if (token.size && token.data) {
      int sent;
      while ((sent = send(self->socket, static_cast<const char*>(token.data), static_cast<int>(token.size), 0)) == SOCKET_ERROR &&
             WSAGetLastError() == WSAEWOULDBLOCK)
        game::Call<void (*)(int)>(0x14032ec60)(1);  // sleep 1 ms
      if (sent == SOCKET_ERROR || sent == 0) {
        game::Call<LogFn>(0x1402baba0)(SslErrorLog(), reinterpret_cast<const char*>(0x1425ac328),
                                       WSAGetLastError());  // "Socket error %d sending data to server."
        (*reinterpret_cast<int (**)(void*)>(SslFunctions(self) + 0x48))(context);  // DeleteSecurityContext
        ok = false;
      }
      (*reinterpret_cast<int (**)(void*)>(SslFunctions(self) + 0x80))(token.data);  // FreeContextBuffer
      token.data = nullptr;
    }
    if (ok) {
      *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + 0x50) = LocalAlloc(0, 0x10000);
      self->sslState = 1;
      *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(self) + 0x5C) = 0x90312;
    }
    int64_t delta = TimeNow() - started;
    int elapsed = delta > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(delta);
    if (elapsed > 1000)
      game::Call<LogFn>(0x1402ef740)(SslErrorLog(), reinterpret_cast<const char*>(0x1425ac350), elapsed);  // "Warning! StartSslClientHandshake took %d ms."
  }
  serverName.vtable = reinterpret_cast<void**>(0x14204f138);
  if (serverName.capacity > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(serverName.data) - 1, -1) - 1 <= 0)
    reinterpret_cast<void (*)(WideStringFixed256*)>(serverName.vtable[2])(&serverName);
  return ok;
}

// 0x141ebead0: SendEncrypted - Schannel EncryptMessage into a pooled send
// buffer ({header, data, trailer} stream buffers) and send it; whatever the
// socket does not take is queued on the connection (0x141ebc710). Returns
// the bytes actually sent.
int TcpConnectionPlatformDriverSendEncrypted(TcpConnectionPlatformDriver* self, const void* data, int length) {
  soeutil::StringFixed<256> statusText;
  soeutil::InitFixed(statusText, reinterpret_cast<void**>(0x142049e08));
  int64_t started = TimeNow();
  void* context = reinterpret_cast<uint8_t*>(self) + 0x38;
  struct StreamSizes {
    unsigned long header, trailer, maximumMessage, buffers, blockSize;
  } sizes;
  using QueryFn = int (*)(void*, unsigned long, void*);
  int status = (*reinterpret_cast<QueryFn*>(SslFunctions(self) + 0x58))(context, 4, &sizes);  // SECPKG_ATTR_STREAM_SIZES
  int sentBytes = 0;
  if (status != 0) {
    const char* text = game::Call<const char* (*)(int, soeutil::IString*)>(0x141ebdc50)(status, &statusText);
    game::Call<LogFn>(0x1402baba0)(SslErrorLog(), reinterpret_cast<const char*>(0x1425abf58), text);  // "Error reading SECPKG_ATTR_STREAM_SIZES - %s"
  } else {
    // Pooled byte buffer {vtable, data +8, size +0x10, capacity +0x14,
    // shared base +0x18, counts +0x20}.
    auto* buffer = game::Call<uint8_t* (*)(void*)>(0x141ec21a0)(*reinterpret_cast<void**>(static_cast<uint8_t*>(self->owner) + 0x40));
    auto& bytes = *reinterpret_cast<uint8_t**>(buffer + 8);
    auto& size = *reinterpret_cast<int*>(buffer + 0x10);
    auto& capacity = *reinterpret_cast<int*>(buffer + 0x14);
    int needed = static_cast<int>(sizes.header + sizes.trailer) + length;
    if (needed > capacity) {
      int newCapacity;
      void** vtable = *reinterpret_cast<void***>(buffer);
      auto* grown = reinterpret_cast<uint8_t* (*)(uint8_t*, int, int*, bool)>(vtable[1])(buffer, needed, &newCapacity, true);
      if (grown != bytes) {
        if (bytes) {
          std::memcpy(grown, bytes, static_cast<size_t>(size));
          reinterpret_cast<void (*)(uint8_t*, uint8_t*, int)>((*reinterpret_cast<void***>(buffer))[2])(buffer, bytes, capacity);
        }
        bytes = grown;
        capacity = newCapacity;
      }
    }
    game::Call<void (*)(uint8_t*, unsigned long, const void*, int)>(0x14030d520)(buffer, sizes.header, data, length);  // write at offset
    uint8_t* base = size != 0 ? bytes : nullptr;
    SecBuffer streams[4] = {{sizes.header, 7, size != 0 ? bytes : nullptr},  // SECBUFFER_STREAM_HEADER
                            {static_cast<unsigned long>(length), 1, base + sizes.header},  // SECBUFFER_DATA
                            {sizes.trailer, 6, base + sizes.header + length},  // SECBUFFER_STREAM_TRAILER
                            {0, 0, nullptr}};
    SecBufferDesc description{0, 4, streams};
    using EncryptFn = int (*)(void*, unsigned long, SecBufferDesc*, unsigned long);
    status = (*reinterpret_cast<EncryptFn*>(SslFunctions(self) + 0xC8))(context, 0, &description, 0);
    if (status < 0) {
      const char* text = game::Call<const char* (*)(int, soeutil::IString*)>(0x141ebdc50)(status, &statusText);
      game::Call<LogFn>(0x1402baba0)(SslErrorLog(), reinterpret_cast<const char*>(0x1425abfa8), text);  // "Error returned by EncryptMessage - %s."
    } else {
      unsigned total = streams[2].size + streams[1].size + streams[0].size;
      int sent = send(self->socket, reinterpret_cast<const char*>(size != 0 ? bytes : nullptr), static_cast<int>(total), 0);
      sentBytes = sent < 0 ? 0 : sent;
      if (static_cast<unsigned>(sentBytes) < total) {
        uint8_t* owner = static_cast<uint8_t*>(self->owner);
        game::Call<void (*)(void*, int, const uint8_t*, unsigned)>(0x141ebc710)(owner + 0x2100, *reinterpret_cast<int*>(owner + 0x2148),
                                                                                (size != 0 ? bytes : nullptr) + sentBytes, total - sentBytes);
      }
      game::Call<LogFn>(0x140428ce0)(SslLog(), reinterpret_cast<const char*>(0x1425abf88), sentBytes,
                                     total - sentBytes);  // "%d bytes sent, %d bytes queued"
    }
    // Drop the pooled buffer's shared reference.
    auto* counts = *reinterpret_cast<volatile long**>(buffer + 0x20);
    bool lastStrong = _InterlockedExchangeAdd(&counts[0], -1) == 1;
    if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts)
      game::Call<void (*)(void*, size_t)>(0x140d0fb84)(const_cast<long*>(counts), 0x10);
    if (lastStrong) {
      uint8_t* shared = buffer + 0x18;
      (*reinterpret_cast<void (***)(uint8_t*)>(shared))[1](shared);
    }
    int64_t delta = TimeNow() - started;
    int elapsed = delta > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(delta);
    if (elapsed > 1000)
      game::Call<LogFn>(0x1402ef740)(SslErrorLog(), reinterpret_cast<const char*>(0x1425abfd0), elapsed);  // "Warning! SendEncrypted took %d ms."
  }
  statusText.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&statusText);
  return sentBytes;
}

}  // namespace rebuild::udp

using namespace rebuild::udp;
REBUILD_FUNCTION(TcpConnectionPlatformDriver_DeletingDestructor, 0x141ebc4a0, TcpConnectionPlatformDriverDeletingDestructor);
REBUILD_FUNCTION_TOO_SMALL(TcpConnectionPlatformDriver_SetOwner, 0x141ebefd0, TcpConnectionPlatformDriverSetOwner);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_GetLocalAddress, 0x141ebda30, TcpConnectionPlatformDriverGetLocalAddress);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_GetRemoteAddress, 0x141ebd930, TcpConnectionPlatformDriverGetRemoteAddress);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_Send, 0x141ebeaa0, TcpConnectionPlatformDriverSend);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_IsSecure, 0x141ebe8e0, TcpConnectionPlatformDriverIsSecure);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_GetLocalPort, 0x141ebdab0, TcpConnectionPlatformDriverGetLocalPort);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_GiveTime, 0x141ebdf20, TcpConnectionPlatformDriverGiveTime);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_TerminateSsl, 0x141ebf360, TcpConnectionPlatformDriverTerminateSsl);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_StartSslHandshake, 0x141ebefe0, TcpConnectionPlatformDriverStartSslHandshake);
REBUILD_FUNCTION(TcpConnectionPlatformDriver_SendEncrypted, 0x141ebead0, TcpConnectionPlatformDriverSendEncrypted);
