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

#include <intrin.h>

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

// 0x141ebe490 (slot 1): Init(manager, socket options, bind host).
void TcpPlatformDriverInit(TcpPlatformDriver* self, void* manager, int optionA, int optionB, const char* host) {
  self->handle->manager = manager;
  self->handle->optionA = optionA;
  self->handle->optionB = optionB;
  soeutil::StringAssign(reinterpret_cast<soeutil::IString*>(&self->handle->hostVtable), host);
}

// 0x141ebd6e0 (slot 10): resolve "host[:port]" (dotted or DNS name) into
// `out`; the port defaults to `port`. Returns whether a name was given.
bool TcpPlatformDriverResolve(TcpPlatformDriver* /*self*/, TcpAddress* out, const char* name, uint16_t port) {
  soeutil::StringFixed<256> host;
  soeutil::InitFixed(host, reinterpret_cast<void**>(0x142049de8));
  soeutil::StringAssign(&host, name);
  host.vtable = reinterpret_cast<void**>(0x142049e08);
  if (host.length > 0) {
    char* colon = host.data;
    while (*colon && *colon != ':') ++colon;
    if (*colon == ':') {
      int index = static_cast<int>(colon - host.data);
      if (index != -1) {
        int parsed;
        port = static_cast<uint16_t>(*game::Call<int* (*)(const char*, int*)>(0x1402ecf30)(host.data + index + 1, &parsed));
        int needed = index + 1;
        if (host.capacity < needed || (host.capacity > 0 && reinterpret_cast<int*>(host.data)[-1] > 1)) soeutil::StringReserve(&host, needed);
        host.length = index;
        host.data[index] = 0;
      }
    }
  }
  u_long ip = inet_addr(host.data);
  if (ip == INADDR_NONE) {
    hostent* entry = gethostbyname(host.data);
    ip = entry ? *reinterpret_cast<u_long*>(entry->h_addr_list[0]) : 0;
  }
  out->port = port;
  if (name) std::memcpy(out->ip, &ip, 4);
  host.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&host);
  return name != nullptr;
}

namespace {
const char* SslLog() { return *reinterpret_cast<const char**>(0x142ad58d8); }  // "TcpLibrarySslErrors.log"
using LogFn = void (*)(const char*, const char*, ...);
int64_t TimeNow() {
  int64_t now;
  return *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now);
}
}  // namespace

// 0x141ebe4c0 (slot 13): InitSsl - load Secur32.dll, get the SSPI function
// table, and acquire outbound Schannel credentials ("Microsoft Unified
// Security Protocol Provider", SCHANNEL_CRED version 4, flags 0x20 | 0x40).
bool TcpPlatformDriverInitSsl(TcpPlatformDriver* self) {
  int64_t started = TimeNow();
  (void)started;
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = 0x114;
#pragma warning(suppress : 4996)
  if (!GetVersionExW(&version)) {
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abd68), GetLastError());  // "Failed to retrieve OS version info - 0x%x"
    return false;
  }
  if (version.dwMajorVersion < 5) {
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abd98), version.dwMajorVersion,
                                   version.dwPlatformId);  // "Unsupported OS or platform - majorVersion=%d, platform=%d"
    return false;
  }
  uint8_t* handle = reinterpret_cast<uint8_t*>(self->handle);
  if (!game::Call<bool (*)(void*, const char*, int)>(0x14166c150)(handle + 0x78, reinterpret_cast<const char*>(0x1425abdd8), 0)) {  // "Secur32.dll"
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abde8), GetLastError());  // "Error loading Secur32.dll - 0x%x"
    return false;
  }
  auto init = game::Call<void* (*)(void*, const char*)>(0x14166c0b0)(reinterpret_cast<uint8_t*>(self->handle) + 0x78,
                                                                   reinterpret_cast<const char*>(0x1425abe10));  // "InitSecurityInterfaceW"
  if (!init) {
    // The original passes only the error code to a "%s - 0x%x" format.
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abe28), GetLastError());
    return false;
  }
  void* table = reinterpret_cast<void* (*)()>(init)();
  handle = reinterpret_cast<uint8_t*>(self->handle);
  *reinterpret_cast<void**>(handle + 0xE0) = table;
  if (!*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self->handle) + 0xE0)) {
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abe60), GetLastError());  // "Failed to read security interface - 0x%x"
    return false;
  }
  std::memset(reinterpret_cast<uint8_t*>(self->handle) + 0x80, 0, 0x50);  // SCHANNEL_CRED
  *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(self->handle) + 0x80) = 4;
  *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(self->handle) + 0xB8) = 0x800;
  // The algorithm list points at this stack slot, as in the original (it
  // dangles once this function returns).
  int algorithm;
  if (int wanted = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(self->handle) + 0xE8)) {
    algorithm = wanted;
    *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(self->handle) + 0xA8) = 1;
    *reinterpret_cast<int**>(reinterpret_cast<uint8_t*>(self->handle) + 0xB0) = &algorithm;
  }
  *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(self->handle) + 0xC8) |= 0x20;
  *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(self->handle) + 0xC8) |= 0x40;
  int64_t acquireStart = TimeNow();
  handle = reinterpret_cast<uint8_t*>(self->handle);
  uint8_t* functions = *reinterpret_cast<uint8_t**>(handle + 0xE0);
  int64_t expiry;
  using AcquireFn = int (*)(void*, const wchar_t*, unsigned long, void*, void*, void*, void*, void*, int64_t*);
  if ((*reinterpret_cast<AcquireFn*>(functions + 0x18))(nullptr, reinterpret_cast<const wchar_t*>(0x1425abe90), 2, nullptr, handle + 0x80, nullptr,
                                                         nullptr, handle + 0xD0, &expiry) != 0) {
    game::Call<LogFn>(0x1402baba0)(SslLog(), reinterpret_cast<const char*>(0x1425abef0), GetLastError());  // "AcquireCredentialsHandle failed - 0x%x"
    return false;
  }
  int64_t since = acquireStart;
  int elapsed = game::Call<int (*)(int64_t*)>(0x1403f71e0)(&since);
  if (elapsed > 1000) game::Call<LogFn>(0x1402ef740)(SslLog(), reinterpret_cast<const char*>(0x1425abf18), elapsed);  // "Warning! InitSsl took %d ms."
  return true;
}

// 0x141ebc660 (slot 14): CleanupSsl - free the credentials and Secur32.dll.
void TcpPlatformDriverCleanupSsl(TcpPlatformDriver* self) {
  int64_t started = TimeNow();
  uint8_t* handle = reinterpret_cast<uint8_t*>(self->handle);
  if (uint8_t* functions = *reinterpret_cast<uint8_t**>(handle + 0xE0))
    (*reinterpret_cast<int (**)(void*)>(functions + 0x20))(handle + 0xD0);  // FreeCredentialsHandle
  game::Call<void (*)(void*)>(0x14166c3e0)(reinterpret_cast<uint8_t*>(self->handle) + 0x78);
  int64_t delta = TimeNow() - started;
  int elapsed = delta > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(delta);
  if (elapsed > 1000) game::Call<LogFn>(0x1402ef740)(SslLog(), reinterpret_cast<const char*>(0x1425abf38), elapsed);  // "Warning! CleanupSsl took %d ms."
}

namespace {
// Shared-reference release for objects carrying a {strong, weak} count block
// at +8: drop both counts, free the 0x10-byte block when the last weak
// reference goes, and run the object's vfunc 1 when the last strong one does.
void ReleaseShared(uint8_t* object) {
  auto* counts = *reinterpret_cast<volatile long**>(object + 8);
  bool lastStrong = _InterlockedExchangeAdd(&counts[0], -1) == 1;
  if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts)
    game::Call<void (*)(void*, size_t)>(0x140d0fb84)(const_cast<long*>(counts), 0x10);
  if (lastStrong) (*reinterpret_cast<void (***)(uint8_t*)>(object))[1](object);
}
}  // namespace

// 0x141ebe2c0 (slot 2): accept every pending connection on the listen
// socket and hand each new TcpConnection to the manager (0x141ec26d0).
void TcpPlatformDriverAcceptConnections(TcpPlatformDriver* self) {
  game::Call<void (*)(TcpPlatformDriver*)>(0x141ec2d10)(self);
  SOCKET listenSocket = self->handle->listen;
  if (listenSocket == INVALID_SOCKET) return;
  sockaddr address;
  int length = sizeof(sockaddr);
  for (SOCKET accepted = accept(listenSocket, &address, &length); accepted != INVALID_SOCKET;
       accepted = accept(self->handle->listen, &address, &length)) {
    void* driver = NewConnectionDriver(self, accepted);
    uint8_t* connection = nullptr;
    if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x2170))
      connection = game::Call<uint8_t* (*)(void*, void*, void*, bool, int)>(0x141ebf7a0)(memory, driver, self->handle->manager, true, 0);
    game::Call<void (*)(void*, uint8_t*)>(0x141ec26d0)(self->handle->manager, connection);
    ReleaseShared(connection);  // not null-checked in the original
  }
}

// 0x141ebd520 (slot 9): start a connection whose address is resolved
// asynchronously (slot 12 result, shared-referenced by the driver).
void* TcpPlatformDriverConnectAsync(TcpPlatformDriver* self, void* name, int defaultPort, int flags) {
  auto* lookup = (*reinterpret_cast<uint8_t* (***)(TcpPlatformDriver*, void*, int)>(self))[0x60 / 8](self, name, defaultPort);
  auto* driver = static_cast<uint8_t*>(game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x70));
  if (driver) {
    auto field = [&](size_t offset) -> uint64_t& { return *reinterpret_cast<uint64_t*>(driver + offset); };
    int optionB = self->handle->optionB;
    int optionA = self->handle->optionA;
    field(0) = 0x1425abcb0;
    field(0x60) = 0;
    field(0x20) = static_cast<uint64_t>(INVALID_SOCKET);
    field(0x08) = reinterpret_cast<uint64_t>(self);
    field(0x28) = 0;
    field(0x10) = reinterpret_cast<uint64_t>(lookup);
    auto* counts = *reinterpret_cast<volatile long**>(lookup + 8);  // lookup not null-checked in the original
    _InterlockedIncrement(&counts[1]);
    _InterlockedIncrement(&counts[0]);
    *reinterpret_cast<int*>(driver + 0x18) = optionB;
    *reinterpret_cast<int*>(driver + 0x1C) = optionA;
    field(0x30) = 0;
    field(0x38) = 0;
    field(0x40) = 0;
    field(0x48) = 0;
    driver[0x68] = 1;
    field(0x58) = 0;
    field(0x60) = *reinterpret_cast<uint64_t*>(0x143e08020);
  }
  void* connection = nullptr;
  if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x2170))
    connection = game::Call<void* (*)(void*, void*, void*, bool, int)>(0x141ebf7a0)(memory, driver, self->handle->manager, false, flags);
  if (lookup) ReleaseShared(lookup);
  return connection;
}

// 0x141ec2bd0 (TcpDriver slot 12): queue an asynchronous address lookup
// (AsyncAddressResult, 0x1b0) on the driver's lookup thread pool, created on
// first use as "TcpDriverAsyncAddress".
void* TcpDriverResolveAsync(uint8_t* self, void* name, int defaultPort) {
  auto& pool = *reinterpret_cast<void**>(self + 8);
  if (!pool) {
    void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x190);
    pool = memory ? game::Call<void* (*)(void*)>(0x141668930)(memory) : nullptr;
    game::Call<void (*)(void*, int, int, int, const char*)>(0x14166a6f0)(pool, 1, 0x10000, 2, reinterpret_cast<const char*>(0x1425ad1e8));
  }
  void* result = nullptr;
  if (void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x1B0))
    result = game::Call<void* (*)(void*, uint8_t*, void*, int)>(0x141ec2900)(memory, self, name, defaultPort);
  game::Call<void (*)(void*, void*, void*, void*)>(0x141668d70)(pool, result, nullptr, nullptr);
  return result;
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
REBUILD_FUNCTION(TcpPlatformDriver_Init, 0x141ebe490, TcpPlatformDriverInit);
REBUILD_FUNCTION(TcpPlatformDriver_Resolve, 0x141ebd6e0, TcpPlatformDriverResolve);
REBUILD_FUNCTION(TcpPlatformDriver_InitSsl, 0x141ebe4c0, TcpPlatformDriverInitSsl);
REBUILD_FUNCTION(TcpPlatformDriver_CleanupSsl, 0x141ebc660, TcpPlatformDriverCleanupSsl);
REBUILD_FUNCTION(TcpPlatformDriver_AcceptConnections, 0x141ebe2c0, TcpPlatformDriverAcceptConnections);
REBUILD_FUNCTION(TcpPlatformDriver_ConnectAsync, 0x141ebd520, TcpPlatformDriverConnectAsync);
REBUILD_FUNCTION(TcpDriver_ResolveAsync, 0x141ec2bd0, TcpDriverResolveAsync);
