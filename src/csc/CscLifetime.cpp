// ClientServerCore destructors, compression statistics and connection
// status accessors.
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Mutex.h"
#include "soeutil/String.h"
#include "udp/UdpRefCount.h"

namespace rebuild::csc {
namespace {

constexpr uintptr_t kVtUdpManagerHandler = 0x1424aff28;
constexpr uintptr_t kVtUdpCompressionHandler = 0x1424aff60;
constexpr uintptr_t kVtUdpConnectionHandler = 0x1424b0330;
constexpr uintptr_t kVtRpcManagerHandler = 0x1424b0368;
constexpr uintptr_t kVtBaseUdpManager = 0x1424b04a8;
constexpr uintptr_t kVtIStringFixed32 = 0x14204a358;

void SetVtable(void* object, uintptr_t vtable) { *static_cast<void**>(object) = reinterpret_cast<void*>(vtable); }

CRITICAL_SECTION* StatsMutex(uint8_t* handler) { return reinterpret_cast<CRITICAL_SECTION*>(handler + 8); }

// ~StringFixed<32> as inlined into the owning destructors.
void DestroyStringFixed32(uint8_t* string) {
  auto* s = reinterpret_cast<soeutil::IString*>(string);
  SetVtable(s, kVtIStringFixed32);
  soeutil::StringRelease(s);
  s->data = soeutil::EmptyStringData();
  s->length = 0;
  s->capacity = 0;
  s->vtable = soeutil::IStringVtable();
}

udp::UdpPlatformGuardObject* ConnectionGuard(uint8_t* connection) {
  return reinterpret_cast<udp::UdpPlatformGuardObject*>(connection + 0x2E0);
}

}  // namespace

// 0x1415f2e30: ~UdpCompressionHandler
void UdpCompressionHandlerDestroy(uint8_t* handler) {
  SetVtable(handler, kVtUdpCompressionHandler);
  soeutil::MutexDestroy(StatsMutex(handler));
  SetVtable(handler, kVtUdpManagerHandler);
}

// 0x1415f2ef0: copy the 0x30-byte compression statistics block.
uint8_t* UdpCompressionHandlerGetStats(uint8_t* handler, uint8_t* out) {
  soeutil::MutexLock(StatsMutex(handler));
  std::memcpy(out, handler + 0x50, 0x30);
  soeutil::MutexUnlock(StatsMutex(handler));
  return out;
}

// 0x1415f2f50
void UdpCompressionHandlerClearStats(uint8_t* handler) {
  soeutil::MutexLock(StatsMutex(handler));
  std::memset(handler + 0x50, 0, 0x30);
  soeutil::MutexUnlock(StatsMutex(handler));
}

// 0x1415f3c90: ~BaseUdpManager (ini section and log channel strings).
void BaseUdpManagerDestroy(uint8_t* manager) {
  SetVtable(manager, kVtBaseUdpManager);
  DestroyStringFixed32(manager + 0x290);
  DestroyStringFixed32(manager + 0x248);
  UdpCompressionHandlerDestroy(manager);
}

// 0x1415f3d80: ~RpcManagerHandler
void RpcManagerHandlerDestroy(void* handler) { SetVtable(handler, kVtRpcManagerHandler); }

// 0x1415f3d90: ~UdpConnectionHandler
void UdpConnectionHandlerDestroy(void* handler) { SetVtable(handler, kVtUdpConnectionHandler); }

// 0x1415f4240: SetServer(address, port, timeoutMs) - stored for the next Connect.
void BaseApiSetServer(uint8_t* api, const char* address, int port, int timeoutMs) {
  soeutil::StringAssign(reinterpret_cast<soeutil::IString*>(api + 0xBA0), address);
  game::Field<int>(api, 0xB9C) = timeoutMs;
  game::Field<int>(api, 0xB98) = port;
}

// 0x1415f4290: reliable channel 0 statistics of the current connection,
// with derived resend ratios.
void BaseApiGetReliableStats(uint8_t* api, uint8_t* out) {
  auto* connection = game::Field<uint8_t*>(api, 0x2C0);
  if (!connection) return;
  ConnectionGuard(connection)->Enter();
  auto* channel = game::Field<uint8_t*>(connection, 0xE0);
  if (channel) {
    std::memcpy(out, connection + 0xF0, 0xA0);
    std::memcpy(out + 0xA0, connection + 0x190, 0x20);
    if (game::Field<int>(channel, 0x68) == 0) {
      *reinterpret_cast<int*>(out + 0x78) = -1;
    } else {
      *reinterpret_cast<int*>(out + 0x78) =
          game::Call<int (*)(uint8_t*, int64_t)>(0x14030d440)(connection, game::Field<int64_t>(connection, 0x2A0));
    }
    auto i64 = [out](size_t o) { return *reinterpret_cast<int64_t*>(out + o); };
    *reinterpret_cast<float*>(out + 0xB8) = 1.0f;
    *reinterpret_cast<float*>(out + 0xBC) = 1.0f;
    if (i64(0x98) > 0) *reinterpret_cast<float*>(out + 0xB8) = static_cast<float>(i64(0xB0)) / static_cast<float>(i64(0x98));
    if (i64(0xA8) > 0) *reinterpret_cast<float*>(out + 0xBC) = static_cast<float>(i64(0xA0)) / static_cast<float>(i64(0xA8));
    *reinterpret_cast<int*>(out + 0x90) = 0;
    if (auto* multi = game::Field<uint8_t*>(connection, 0x1D0)) {
      *reinterpret_cast<int*>(out + 0x90) = game::Field<int>(multi, 0x98);
    }
  }
  ConnectionGuard(connection)->Leave();
}

// 0x1415f42b0 / 0x1415f4340: connection counters, cached after disconnect.
int BaseApiConnectionValue1C0(uint8_t* api) {
  auto* connection = game::Field<uint8_t*>(api, 0x2C0);
  if (!connection) return game::Field<int>(api, 0x360);
  ConnectionGuard(connection)->Enter();
  int value = game::Field<int>(connection, 0x1C0);
  ConnectionGuard(connection)->Leave();
  return value;
}

int BaseApiConnectionValue1C4(uint8_t* api) {
  auto* connection = game::Field<uint8_t*>(api, 0x2C0);
  if (!connection) return game::Field<int>(api, 0x364);
  ConnectionGuard(connection)->Enter();
  int value = game::Field<int>(connection, 0x1C4);
  ConnectionGuard(connection)->Leave();
  return value;
}

REBUILD_FUNCTION(UdpCompressionHandler_Destroy, 0x1415f2e30, UdpCompressionHandlerDestroy);
REBUILD_FUNCTION(UdpCompressionHandler_GetStats, 0x1415f2ef0, UdpCompressionHandlerGetStats);
REBUILD_FUNCTION(UdpCompressionHandler_ClearStats, 0x1415f2f50, UdpCompressionHandlerClearStats);
REBUILD_FUNCTION(BaseUdpManager_Destroy, 0x1415f3c90, BaseUdpManagerDestroy);
REBUILD_FUNCTION(RpcManagerHandler_Destroy, 0x1415f3d80, RpcManagerHandlerDestroy);
REBUILD_FUNCTION(UdpConnectionHandler_Destroy, 0x1415f3d90, UdpConnectionHandlerDestroy);
REBUILD_FUNCTION(BaseApi_SetServer, 0x1415f4240, BaseApiSetServer);
REBUILD_FUNCTION(BaseApi_GetReliableStats, 0x1415f4290, BaseApiGetReliableStats);
REBUILD_FUNCTION(BaseApi_ConnectionValue1C0, 0x1415f42b0, BaseApiConnectionValue1C0);
REBUILD_FUNCTION(BaseApi_ConnectionValue1C4, 0x1415f4340, BaseApiConnectionValue1C4);

}  // namespace rebuild::csc
