// TcpLibrary::TcpConnectionPlatformDriver: the per-connection Winsock side
// (0x70 bytes, built inline by TcpPlatformDriver Accept / Connect).
#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"

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
