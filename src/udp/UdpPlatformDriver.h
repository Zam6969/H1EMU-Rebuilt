#pragma once

#include <winsock2.h>

#include <cstddef>
#include <cstdint>

#include "soeutil/HashListMap.h"

// UdpLibrary::UdpPlatformDriver (RTTI .?AVUdpPlatformDriver@UdpLibrary@@),
// the Winsock implementation of the abstract UdpLibrary::UdpDriver. Owns the
// non-blocking UDP socket under SOE's UdpLibrary, the reliable-UDP layer all
// game traffic goes through. Fully rebuilt: constructor, destructor and all
// 16 vtable slots.
namespace rebuild::udp {

struct UdpSocketHandle {
  SOCKET socket;  // +0x00, INVALID_SOCKET when closed
  int ttl;        // +0x08, default IP_TTL, restored after a port-alive send
};
static_assert(sizeof(UdpSocketHandle) == 0x10);

struct UdpPlatformDriver {
  void** vtable;                       // +0x0000
  soeutil::HashListMap sendErrors;     // +0x0008 WSA error -> count
  soeutil::HashListMap receiveErrors;  // +0x2030 WSA error -> count
  UdpSocketHandle* handle;             // +0x4058
};
static_assert(offsetof(UdpPlatformDriver, sendErrors) == 0x0008);
static_assert(offsetof(UdpPlatformDriver, receiveErrors) == 0x2030);
static_assert(offsetof(UdpPlatformDriver, handle) == 0x4058);
static_assert(sizeof(UdpPlatformDriver) == 0x4060);

inline void** const kUdpPlatformDriverVtable = reinterpret_cast<void**>(0x1420524a0);
inline void** const kUdpDriverVtable = reinterpret_cast<void**>(0x1420523a8);

// vtable 0x1420524a0; UdpDriver declares slots 1-15 pure virtual.
enum UdpPlatformDriverSlot {
  kSlotScalarDeletingDestructor = 0,
  kSlotSocketOpen = 1,
  kSlotSocketClose = 2,
  kSlotSocketReceive = 3,
  kSlotSocketSend = 4,
  kSlotSocketSendPortAlive = 5,
  kSlotGetLocalIp = 6,
  kSlotGetLocalPort = 7,
  kSlotGetHostByName = 8,
  kSlotGetSelectedIp = 9,
  kSlotSleep = 10,
  kSlotClock = 11,
  kSlotSendErrors = 12,
  kSlotReceiveErrors = 13,
  kSlotSendErrorText = 14,
  kSlotReceiveErrorText = 15,
};

UdpPlatformDriver* Construct(UdpPlatformDriver* self);
void Destruct(UdpPlatformDriver* self);
UdpPlatformDriver* ScalarDeletingDestructor(UdpPlatformDriver* self, unsigned flags);

bool SocketOpen(UdpPlatformDriver* self, uint16_t port, int receiveBufferSize,
                int sendBufferSize, const char* bindAddress);
void SocketClose(UdpPlatformDriver* self);
int SocketReceive(UdpPlatformDriver* self, char* buffer, int bufferSize, uint32_t* fromAddress,
                  uint32_t* fromPort);
bool SocketSend(UdpPlatformDriver* self, const char* data, int dataSize,
                const uint32_t* toAddress, uint16_t toPort);
void SocketSendPortAlive(UdpPlatformDriver* self, const char* data, int dataSize,
                         const uint32_t* toAddress, uint16_t toPort);
bool GetLocalIp(UdpPlatformDriver* self, uint32_t* address);
uint16_t GetLocalPort(UdpPlatformDriver* self);
bool GetHostByName(UdpPlatformDriver* self, uint32_t* address, const char* name);
bool GetSelectedIp(UdpPlatformDriver* self, uint32_t* address, bool preferPrivate);
void Sleep(UdpPlatformDriver* self, DWORD milliseconds);
uint64_t Clock(UdpPlatformDriver* self);
soeutil::HashListMap* SendErrors(UdpPlatformDriver* self);
soeutil::HashListMap* ReceiveErrors(UdpPlatformDriver* self);
void ErrorText(UdpPlatformDriver* self, DWORD error, void* outString);

}  // namespace rebuild::udp
