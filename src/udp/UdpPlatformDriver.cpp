#include "udp/UdpPlatformDriver.h"

#include <timeapi.h>
#include <ws2tcpip.h>

#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"

namespace rebuild::udp {
namespace {

template <class Fn>
Fn Virtual(UdpPlatformDriver* self, int slot) {
  return reinterpret_cast<Fn>(self->vtable[slot]);
}

bool IsPrivateAddress(const uint8_t* ip) {
  // Note the game only treats 172.16.x.x as private, not all of 172.16/12.
  return ip[0] == 10 || (ip[0] == 172 && ip[1] == 16) || (ip[0] == 192 && ip[1] == 168);
}

sockaddr_in LocalName(UdpPlatformDriver* self) {
  sockaddr_in name{};
  int size = sizeof(name);
  getsockname(self->handle->socket, reinterpret_cast<sockaddr*>(&name), &size);
  return name;
}

}  // namespace

// 0x140349f40
UdpPlatformDriver* Construct(UdpPlatformDriver* self) {
  self->vtable = kUdpPlatformDriverVtable;
  self->sendErrors.Construct(soeutil::kHashListUInt64Vtable);
  self->receiveErrors.Construct(soeutil::kHashListUInt64Vtable);
  self->handle = static_cast<UdpSocketHandle*>(soeutil::Allocate(sizeof(UdpSocketHandle)));
  self->handle->socket = INVALID_SOCKET;
  self->handle->ttl = 32;
  WSADATA wsa;
  WSAStartup(MAKEWORD(1, 1), &wsa);
  return self;
}

// 0x14034a150
void Destruct(UdpPlatformDriver* self) {
  self->vtable = kUdpPlatformDriverVtable;
  WSACleanup();
  soeutil::Free(self->handle, sizeof(UdpSocketHandle));
  self->receiveErrors.vtable = soeutil::kHashListMapIntUInt64Vtable;
  self->receiveErrors.Clear();
  self->sendErrors.vtable = soeutil::kHashListMapIntUInt64Vtable;
  self->sendErrors.Clear();
  self->vtable = kUdpDriverVtable;
}

// 0x14034a360, slot 0
UdpPlatformDriver* ScalarDeletingDestructor(UdpPlatformDriver* self, unsigned flags) {
  Destruct(self);
  if (flags & 1) soeutil::Free(self, sizeof(UdpPlatformDriver));
  return self;
}

// 0x14034ae10, slot 1
bool SocketOpen(UdpPlatformDriver* self, uint16_t port, int receiveBufferSize,
                int sendBufferSize, const char* bindAddress) {
  UdpSocketHandle* handle = self->handle;
  handle->socket = socket(AF_INET, SOCK_DGRAM, 0);
  if (handle->socket == INVALID_SOCKET) return false;

  u_long nonBlocking = 1;
  ioctlsocket(handle->socket, FIONBIO, &nonBlocking);
  setsockopt(handle->socket, SOL_SOCKET, SO_SNDBUF,
             reinterpret_cast<const char*>(&sendBufferSize), sizeof(int));
  setsockopt(handle->socket, SOL_SOCKET, SO_RCVBUF,
             reinterpret_cast<const char*>(&receiveBufferSize), sizeof(int));
  int ttlSize = sizeof(int);
  getsockopt(handle->socket, IPPROTO_IP, IP_TTL, reinterpret_cast<char*>(&handle->ttl), &ttlSize);

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bindAddress && *bindAddress) {
    unsigned long parsed = inet_addr(bindAddress);
    if (parsed != INADDR_NONE) local.sin_addr.s_addr = parsed;
  }
  if (bind(handle->socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    Virtual<void (*)(UdpPlatformDriver*)>(self, kSlotSocketClose)(self);
    return false;
  }
  return true;
}

// 0x14034ad30, slot 2
void SocketClose(UdpPlatformDriver* self) {
  if (self->handle->socket != INVALID_SOCKET) {
    closesocket(self->handle->socket);
    self->handle->socket = INVALID_SOCKET;
  }
}

// 0x14034af90, slot 3. Returns bytes read, 0 when nothing usable arrived (WSAECONNRESET
// from an ICMP port-unreachable still reports the sender), or -1 on error.
// fromAddress stays in network byte order; fromPort is host order.
int SocketReceive(UdpPlatformDriver* self, char* buffer, int bufferSize, uint32_t* fromAddress,
                  uint32_t* fromPort) {
  sockaddr_in from{};
  int fromSize = sizeof(from);
  int received = recvfrom(self->handle->socket, buffer, bufferSize, 0,
                          reinterpret_cast<sockaddr*>(&from), &fromSize);
  if (received != SOCKET_ERROR) {
    *fromAddress = from.sin_addr.s_addr;
    *fromPort = ntohs(from.sin_port);
    return received;
  }

  int error = WSAGetLastError();
  if (error != WSAEWOULDBLOCK) self->receiveErrors.Increment(error);
  if (error == WSAECONNRESET) {
    *fromAddress = from.sin_addr.s_addr;
    *fromPort = ntohs(from.sin_port);
    return 0;
  }
  return -1;
}

// 0x14034b070, slot 4. toAddress is in network byte order, toPort in host order.
bool SocketSend(UdpPlatformDriver* self, const char* data, int dataSize,
                const uint32_t* toAddress, uint16_t toPort) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = *toAddress;
  to.sin_port = htons(toPort);
  if (sendto(self->handle->socket, data, dataSize, 0, reinterpret_cast<sockaddr*>(&to),
             sizeof(to)) == SOCKET_ERROR) {
    self->sendErrors.Increment(WSAGetLastError());
    return false;
  }
  return true;
}

// 0x14034b110, slot 5. Sends with a TTL of 5 so the packet opens the local
// NAT mapping but dies before reaching the far side, then restores the TTL.
void SocketSendPortAlive(UdpPlatformDriver* self, const char* data, int dataSize,
                         const uint32_t* toAddress, uint16_t toPort) {
  int shortTtl = 5;
  setsockopt(self->handle->socket, IPPROTO_IP, IP_TTL, reinterpret_cast<const char*>(&shortTtl),
             sizeof(int));
  using SendFn = bool (*)(UdpPlatformDriver*, const char*, int, const uint32_t*, uint16_t);
  Virtual<SendFn>(self, kSlotSocketSend)(self, data, dataSize, toAddress, toPort);
  setsockopt(self->handle->socket, IPPROTO_IP, IP_TTL,
             reinterpret_cast<const char*>(&self->handle->ttl), sizeof(int));
}

// 0x14034ad70, slot 6. Always reports success, even if getsockname fails.
bool GetLocalIp(UdpPlatformDriver* self, uint32_t* address) {
  *address = LocalName(self).sin_addr.s_addr;
  return true;
}

// 0x14034adc0, slot 7
uint16_t GetLocalPort(UdpPlatformDriver* self) { return ntohs(LocalName(self).sin_port); }

// 0x14034a630, slot 8. Accepts dotted quads or host names.
bool GetHostByName(UdpPlatformDriver*, uint32_t* address, const char* name) {
  unsigned long parsed = inet_addr(name);
  if (parsed == INADDR_NONE) {
    hostent* host = gethostbyname(name);
    if (!host) {
      *address = 0;
      return false;
    }
    parsed = *reinterpret_cast<uint32_t*>(host->h_addr_list[0]);
  }
  *address = parsed;
  return parsed != 0;
}

// 0x14034a6a0, slot 9. Picks one of this machine's addresses: the first one
// listed, overridden by any later non-loopback address that is public (or
// private when preferPrivate is set). The last match wins.
bool GetSelectedIp(UdpPlatformDriver*, uint32_t* address, bool preferPrivate) {
  uint32_t selected = 0;
  char hostName[1024];
  hostent* host;
  if (gethostname(hostName, sizeof(hostName)) == 0 && (host = gethostbyname(hostName))) {
    for (char** entry = host->h_addr_list; *entry; ++entry) {
      auto* ip = reinterpret_cast<const uint8_t*>(*entry);
      uint32_t value = *reinterpret_cast<const uint32_t*>(ip);
      if (selected == 0) {
        selected = value;
      } else if (ip[0] != 127) {
        bool isPublic = !IsPrivateAddress(ip);
        if (preferPrivate ? !isPublic : isPublic) selected = value;
      }
    }
  }
  *address = selected;
  return selected != 0;
}

// 0x14034ad20, slot 10
void Sleep(UdpPlatformDriver*, DWORD milliseconds) { ::Sleep(milliseconds); }

// 0x14034a410, slot 11. 64-bit millisecond clock: timeGetTime() in the low
// half, a process-wide wrap counter (starting at 1) in the high half. Each
// thread remembers its last reading to notice the 49.7-day wrap.
uint64_t Clock(UdpPlatformDriver*) {
  static DWORD s_wrapSlot = TlsAlloc();
  static DWORD s_lastSlot = TlsAlloc();
  static volatile LONG s_wraps = 0;

  DWORD now = timeGetTime();
  auto wraps = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(TlsGetValue(s_wrapSlot)));
  if (wraps == 0) {
    // First call on this thread; the first thread overall raises timer resolution.
    wraps = static_cast<uint32_t>(InterlockedCompareExchange(&s_wraps, 1, 0));
    if (wraps == 0) {
      wraps = 1;
      timeBeginPeriod(1);
    }
    TlsSetValue(s_wrapSlot, reinterpret_cast<void*>(static_cast<uintptr_t>(wraps)));
  } else if (now < static_cast<DWORD>(reinterpret_cast<uintptr_t>(TlsGetValue(s_lastSlot)))) {
    // Wrapped. Faithful quirk: the new count is stored but this call still
    // returns the old one.
    s_wraps = static_cast<LONG>(wraps + 1);
    TlsSetValue(s_wrapSlot, reinterpret_cast<void*>(static_cast<uintptr_t>(wraps + 1)));
  }
  TlsSetValue(s_lastSlot, reinterpret_cast<void*>(static_cast<uintptr_t>(now)));
  return static_cast<uint64_t>(wraps) << 32 | now;
}

// 0x14034a760, slot 12
soeutil::HashListMap* SendErrors(UdpPlatformDriver* self) { return &self->sendErrors; }

// 0x14034a690, slot 13
soeutil::HashListMap* ReceiveErrors(UdpPlatformDriver* self) { return &self->receiveErrors; }

// 0x14034ab50 / 0x14034a8c0, slots 14 and 15 (identical bodies). Writes the
// system message for a WSA error, or "id=<n>" when there is none.
void ErrorText(UdpPlatformDriver*, DWORD error, void* outString) {
  char text[256];
  std::memset(text, 0, sizeof(text));
  if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK, nullptr, error, 0,
                     text, 255, nullptr)) {
    soeutil::StringAssign(outString, text);
  } else {
    soeutil::StringFormat(outString, "id=%d", error);
  }
}

// 0x14034abf0 / 0x14034a960: the inlined HashListMap insert-or-increment.
static void CountSendError(UdpPlatformDriver* self, int error) { self->sendErrors.Increment(error); }
static void CountReceiveError(UdpPlatformDriver* self, int error) {
  self->receiveErrors.Increment(error);
}

REBUILD_FUNCTION(UdpPlatformDriver_Construct, 0x140349f40, Construct);
REBUILD_FUNCTION(UdpPlatformDriver_Destruct, 0x14034a150, Destruct);
REBUILD_FUNCTION(UdpPlatformDriver_ScalarDeletingDestructor, 0x14034a360, ScalarDeletingDestructor);
REBUILD_FUNCTION(UdpPlatformDriver_SocketOpen, 0x14034ae10, SocketOpen);
REBUILD_FUNCTION(UdpPlatformDriver_SocketClose, 0x14034ad30, SocketClose);
REBUILD_FUNCTION(UdpPlatformDriver_SocketReceive, 0x14034af90, SocketReceive);
REBUILD_FUNCTION(UdpPlatformDriver_SocketSend, 0x14034b070, SocketSend);
REBUILD_FUNCTION(UdpPlatformDriver_SocketSendPortAlive, 0x14034b110, SocketSendPortAlive);
REBUILD_FUNCTION(UdpPlatformDriver_GetLocalIp, 0x14034ad70, GetLocalIp);
REBUILD_FUNCTION(UdpPlatformDriver_GetLocalPort, 0x14034adc0, GetLocalPort);
REBUILD_FUNCTION(UdpPlatformDriver_GetHostByName, 0x14034a630, GetHostByName);
REBUILD_FUNCTION(UdpPlatformDriver_GetSelectedIp, 0x14034a6a0, GetSelectedIp);
REBUILD_FUNCTION(UdpPlatformDriver_Sleep, 0x14034ad20, Sleep);
REBUILD_FUNCTION(UdpPlatformDriver_Clock, 0x14034a410, Clock);
REBUILD_FUNCTION(UdpPlatformDriver_SendErrors, 0x14034a760, SendErrors);
REBUILD_FUNCTION(UdpPlatformDriver_ReceiveErrors, 0x14034a690, ReceiveErrors);
REBUILD_FUNCTION(UdpPlatformDriver_SendErrorText, 0x14034ab50, ErrorText);
REBUILD_FUNCTION(UdpPlatformDriver_ReceiveErrorText, 0x14034a8c0, ErrorText);
REBUILD_FUNCTION(UdpPlatformDriver_CountSendError, 0x14034abf0, CountSendError);
REBUILD_FUNCTION(UdpPlatformDriver_CountReceiveError, 0x14034a960, CountReceiveError);

}  // namespace rebuild::udp
