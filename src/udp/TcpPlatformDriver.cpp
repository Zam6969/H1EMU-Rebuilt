// TcpLibrary::TcpPlatformDriver: the Winsock side of the TCP library -
// listen socket, outgoing connects, proxy discovery and local addresses.
// Object layout: {vtable, ?, Handle* +0x10}; size 0x18.
#include <winsock2.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::udp {
namespace {

// Driver state behind +0x10 (0xF0 bytes, destroyed by 0x141ebc380).
struct TcpDriverHandle {
  void* manager;      // +0x00 TcpManager*
  SOCKET listen;      // +0x08
  int optionB;        // +0x10 socket option value passed second to 0x141ebf630
  int optionA;        // +0x14 socket option value passed first
  void** hostVtable;  // +0x18 IString bind host
  const char* host;   // +0x20
  int hostLength;     // +0x28
};
static_assert(offsetof(TcpDriverHandle, host) == 0x20 && offsetof(TcpDriverHandle, hostLength) == 0x28);

struct TcpPlatformDriver {
  void** vtable;
  uint64_t unknown08;
  TcpDriverHandle* handle;
};
static_assert(sizeof(TcpPlatformDriver) == 0x18);

// Six-byte address as the library passes it: four address bytes, port.
struct TcpAddress {
  uint8_t ip[4];
  uint16_t port;
};

bool ApplySocketOptions(SOCKET socket, int a, int b) { return game::Call<bool (*)(SOCKET, int, int)>(0x141ebf630)(socket, a, b); }

// TcpConnectionPlatformDriver (0x70) for an accepted or connected socket.
void* NewConnectionDriver(TcpPlatformDriver* driver, SOCKET socket) {
  auto* object = static_cast<uint8_t*>(game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x70));
  if (!object) return nullptr;
  auto field = [&](size_t offset) -> uint64_t& { return *reinterpret_cast<uint64_t*>(object + offset); };
  field(0) = 0x1425abcb0;
  field(0x60) = 0;
  field(0x20) = static_cast<uint64_t>(socket);
  field(0x08) = reinterpret_cast<uint64_t>(driver);
  field(0x28) = 0;
  field(0x10) = 0;
  field(0x18) = 0;
  field(0x30) = 0;
  field(0x38) = 0;
  field(0x40) = 0;
  field(0x48) = 0;
  object[0x68] = 1;
  field(0x58) = 0;
  field(0x60) = *reinterpret_cast<uint64_t*>(0x143e08020);
  return object;
}

}  // namespace

// 0x141ebc4e0 (slot 0): scalar deleting destructor.
void* TcpPlatformDriverDeletingDestructor(TcpPlatformDriver* self, unsigned flags) {
  self->vtable = reinterpret_cast<void**>(0x1425abc30);
  WSACleanup();
  if (TcpDriverHandle* handle = self->handle) {
    game::Call<void (*)(void*)>(0x141ebc380)(handle);
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(handle, 0xF0);
  }
  self->handle = nullptr;
  game::Call<void (*)(void*)>(0x141ec2a90)(self);  // ~TcpDriver
  if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, 0x18);
  return self;
}

// 0x141ebe940 (slot 3): Listen(port) on the configured bind host (any
// address when empty); on failure closes the socket through slot 4.
bool TcpPlatformDriverListen(TcpPlatformDriver* self, uint16_t port) {
  self->handle->listen = socket(AF_INET, SOCK_STREAM, 0);
  TcpDriverHandle* handle = self->handle;
  if (handle->listen == INVALID_SOCKET) return false;
  if (ApplySocketOptions(handle->listen, handle->optionA, handle->optionB)) {
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0);
    bool resolved = true;
    if (self->handle->hostLength > 0) {
      u_long ip = inet_addr(self->handle->host);
      if (ip == INADDR_NONE) {
        hostent* host = gethostbyname(self->handle->host);
        if (host)
          ip = *reinterpret_cast<u_long*>(host->h_addr_list[0]);
        else
          resolved = false;
      }
      address.sin_addr.s_addr = ip;
    }
    if (resolved && bind(self->handle->listen, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
        listen(self->handle->listen, 0x7FFFFFFF) == 0)
      return true;
  }
  (*reinterpret_cast<void (***)(TcpPlatformDriver*)>(self))[0x20 / 8](self);
  return false;
}

// 0x141ebe910 (slot 4): close the listen socket.
void TcpPlatformDriverCloseListen(TcpPlatformDriver* self) {
  SOCKET socket = self->handle->listen;
  if (socket != INVALID_SOCKET) {
    closesocket(socket);
    self->handle->listen = INVALID_SOCKET;
  }
}

// 0x141ebd9b0 (slot 5): local address of the listen socket.
bool TcpPlatformDriverGetLocalAddress(TcpPlatformDriver* self, TcpAddress* out) {
  int length = sizeof(sockaddr);
  sockaddr_in address{};
  if (getsockname(self->handle->listen, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return false;
  std::memcpy(out->ip, &address.sin_addr, 4);
  out->port = ntohs(address.sin_port);
  return true;
}

// 0x141ebdb00 (slot 6): local port of the listen socket (0 on error).
uint16_t TcpPlatformDriverGetLocalPort(TcpPlatformDriver* self) {
  int length = sizeof(sockaddr);
  sockaddr_in address{};
  if (getsockname(self->handle->listen, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) return 0;
  return ntohs(address.sin_port);
}

// 0x141ebdde0 (slot 7): the Internet Explorer proxy server, if enabled.
bool TcpPlatformDriverGetProxy(TcpPlatformDriver* /*self*/, soeutil::IString* out) {
  soeutil::StringRelease(out);
  out->data = soeutil::EmptyStringData();
  out->length = 0;
  out->capacity = 0;
  DWORD enabled = 0;
  HKEY key;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, reinterpret_cast<const char*>(0x1425abd08), 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    DWORD size = 4;
    if (RegQueryValueExA(key, reinterpret_cast<const char*>(0x1425abd48), nullptr, nullptr, reinterpret_cast<BYTE*>(&enabled), &size) ==
            ERROR_SUCCESS &&
        enabled) {  // "ProxyEnable"
      BYTE server[0x2000];
      size = sizeof(server);
      if (RegQueryValueExA(key, reinterpret_cast<const char*>(0x1425abd58), nullptr, nullptr, server, &size) == ERROR_SUCCESS &&
          server[0])  // "ProxyServer"
        soeutil::StringAssign(out, reinterpret_cast<const char*>(server));
    }
    RegCloseKey(key);
  }
  return out->length > 0;
}

// 0x141ebd390 (slot 8): non-blocking connect to an address; returns the new
// TcpConnection (0x2170, constructed by 0x141ebf7a0) or null.
void* TcpPlatformDriverConnect(TcpPlatformDriver* self, const TcpAddress* to, int flags) {
  SOCKET socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket == INVALID_SOCKET) {
    WSAGetLastError();
    return nullptr;
  }
  if (ApplySocketOptions(socket, self->handle->optionA, self->handle->optionB)) {
    sockaddr_in address;
    address.sin_family = AF_INET;
    std::memcpy(&address.sin_addr, to->ip, 4);
    address.sin_port = htons(to->port);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != SOCKET_ERROR || WSAGetLastError() == WSAEWOULDBLOCK) {
      void* driver = NewConnectionDriver(self, socket);
      void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x2170);
      if (!memory) return nullptr;
      return game::Call<void* (*)(void*, void*, void*, bool, int)>(0x141ebf7a0)(memory, driver, self->handle->manager, false, flags);
    }
  }
  closesocket(socket);
  return nullptr;
}

// 0x141ebdb60 (slot 11): pick a local IPv4 address. The first address is
// always a candidate; later ones replace it when (excluding 127.x) they are
// private (10.x, 172.16.x, 192.168.x) if `wantPrivate`, else public.
bool TcpPlatformDriverGetHostAddress(TcpPlatformDriver* /*self*/, TcpAddress* out, bool wantPrivate) {
  char name[0x400];
  uint32_t chosen = 0;
  if (gethostname(name, sizeof(name)) != 0) return false;
  hostent* host = gethostbyname(name);
  if (!host || !host->h_addr_list[0]) return false;
  for (size_t i = 0; host->h_addr_list[i]; ++i) {
    auto* bytes = reinterpret_cast<const uint8_t*>(host->h_addr_list[i]);
    bool take = chosen == 0;
    if (!take && bytes[0] != 0x7F) {
      bool isPublic = !(bytes[0] == 10 || (bytes[0] == 0xAC && bytes[1] == 0x10) || (bytes[0] == 0xC0 && bytes[1] == 0xA8));
      take = wantPrivate ? !isPublic : isPublic;
    }
    if (take) std::memcpy(&chosen, bytes, 4);
  }
  if (!chosen) return false;
  std::memcpy(out->ip, &chosen, 4);
  out->port = 0;
  return true;
}

}  // namespace rebuild::udp

using namespace rebuild::udp;
REBUILD_FUNCTION(TcpPlatformDriver_DeletingDestructor, 0x141ebc4e0, TcpPlatformDriverDeletingDestructor);
REBUILD_FUNCTION(TcpPlatformDriver_Listen, 0x141ebe940, TcpPlatformDriverListen);
REBUILD_FUNCTION(TcpPlatformDriver_CloseListen, 0x141ebe910, TcpPlatformDriverCloseListen);
REBUILD_FUNCTION(TcpPlatformDriver_GetLocalAddress, 0x141ebd9b0, TcpPlatformDriverGetLocalAddress);
REBUILD_FUNCTION(TcpPlatformDriver_GetLocalPort, 0x141ebdb00, TcpPlatformDriverGetLocalPort);
REBUILD_FUNCTION(TcpPlatformDriver_GetProxy, 0x141ebdde0, TcpPlatformDriverGetProxy);
REBUILD_FUNCTION(TcpPlatformDriver_Connect, 0x141ebd390, TcpPlatformDriverConnect);
REBUILD_FUNCTION(TcpPlatformDriver_GetHostAddress, 0x141ebdb60, TcpPlatformDriverGetHostAddress);
