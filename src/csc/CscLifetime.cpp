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

// 0x1415f4870: BaseApi field initialisation shared by its constructors.
void BaseApiInitFields(uint8_t* api) {
  game::Field<bool>(api, 0x2D9) = false;
  game::Field<void*>(api, 0x2C0) = nullptr;
  game::Field<uint16_t>(api, 0x2E0) = 0;  // connecting / connected
  game::Field<uint8_t*>(api, 0x98) = api ? api + 0x88 : nullptr;
  game::Field<int>(api, 0x2F0) = 0;
  game::Field<int64_t>(api, 0x2E8) = *reinterpret_cast<int64_t*>(0x143c77ac0);
  game::Field<int>(api, 0xB98) = 0;
  game::Field<int>(api, 0xB9C) = 100;
  game::Field<int>(api, 0x2F4) = 5000;
  game::Field<uint64_t>(api, 0x360) = 0;
  game::Field<int64_t>(api, 0x2C8) = *reinterpret_cast<int64_t*>(0x143c77ac8);
  auto* manager = game::Field<uint8_t*>(api, 0x2D0);
  if (manager && game::Field<bool>(manager, 0x288)) {
    game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(
        game::Field<void*>(manager, 0x250), reinterpret_cast<const char*>(0x1424b04f0),  // "BaseApi constructed (%s) defaultPort=%d"
        manager + 0x209, game::Field<int>(api, 0x2DC));
  }
}

// 0x1415f5bf0: register the "<prefix>/Udp" metrics group.
void BaseApiRegisterMetrics(void* /*api*/, void* registry, void* /*unused*/, const char* prefix) {
  soeutil::StringFixed<256> path;
  path.data = soeutil::EmptyStringData();
  path.length = 0;
  path.capacity = 0;
  SetVtable(&path, 0x142049e08);  // StringFixed<256>
  soeutil::StringFormat(&path, reinterpret_cast<const char*>(0x1424b0c0c), prefix);  // "%s/Udp"
  game::Call<void (*)(void*, const char*, const char*)>(0x1415f8210)(registry, path.data, prefix);
  SetVtable(&path, 0x142049de8);  // IStringFixed<char,256>
  soeutil::StringRelease(&path);
}

// 0x1415f53c0: SoeUtil::Array<IString>::RemoveRange(index, count). Later
// elements are moved down (stealing unshared buffers), then the tail shrinks.
void StringArrayRemoveRange(uint8_t* array, int index, int count) {
  auto* elements = game::Field<soeutil::IString*>(array, 8);
  int remaining = game::Field<int>(array, 0x10) - (index + count);
  soeutil::IString* source = elements + (index + count);
  soeutil::IString* destination = elements + index;
  for (; remaining != 0; --remaining, ++source, ++destination) {
    reinterpret_cast<void (*)(soeutil::IString*, int)>(destination->vtable[0])(destination, 0);  // destruct in place
    destination->vtable = soeutil::IStringVtable();
    destination->data = soeutil::EmptyStringData();
    destination->length = 0;
    destination->capacity = 0;
    bool steal = source->capacity <= 0 || *reinterpret_cast<int*>(source->data - 4) > 0;
    if (steal) {
      destination->data = source->data;
      destination->length = source->length;
      destination->capacity = source->capacity;
      source->vtable = soeutil::IStringVtable();
      source->data = soeutil::EmptyStringData();
      source->length = 0;
      source->capacity = 0;
    } else {
      soeutil::StringAssignString(destination, source);
    }
  }
  game::Call<void (*)(uint8_t*, int)>(0x1404595e0)(array, count);  // drop the tail
}

// SoeUtil::List<IString>: {vtable (slot 2 allocates a node), head, tail, count}.
struct StringListNode {
  soeutil::IString value;
  StringListNode* next;
  StringListNode* previous;
};
static_assert(offsetof(StringListNode, next) == 0x18);
static_assert(offsetof(StringListNode, previous) == 0x20);

struct StringList {
  void** vtable;
  StringListNode* head;
  StringListNode* tail;
  int count;
};
static_assert(offsetof(StringList, count) == 0x18);

// 0x1415f3130: split `text` into tokens appended to `list`; returns the
// list's count.
int SplitIntoStringList(const char* text, StringList* list, const char* delimiters, bool flagA, bool flagB) {
  soeutil::IString token{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  for (;;) {
    char buffer[0x800];
    buffer[0] = '\0';
    using TokenizeFn = int (*)(const char*, char*, int, const char*, bool, bool);
    int consumed = game::Call<TokenizeFn>(0x140330780)(text, buffer, 0x800, delimiters, flagA, flagB);
    soeutil::StringAssign(&token, buffer);
    if (consumed < 1) break;
    text += consumed;
    using AllocFn = StringListNode* (*)(StringList*);
    StringListNode* node = reinterpret_cast<AllocFn>(list->vtable[2])(list);
    if (node) {
      node->value = {soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
      soeutil::StringAssignString(&node->value, &token);
    }
    node->previous = list->tail;
    node->next = nullptr;
    if (list->tail) {
      list->tail->next = node;
    } else {
      list->head = node;
    }
    ++list->count;
    list->tail = node;
  }
  int count = list->count;
  soeutil::StringRelease(&token);
  return count;
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
REBUILD_FUNCTION(BaseApi_InitFields, 0x1415f4870, BaseApiInitFields);
REBUILD_FUNCTION(BaseApi_RegisterMetrics, 0x1415f5bf0, BaseApiRegisterMetrics);
REBUILD_FUNCTION(SoeUtil_StringArray_RemoveRange, 0x1415f53c0, StringArrayRemoveRange);
REBUILD_FUNCTION(SoeUtil_SplitIntoStringList, 0x1415f3130, SplitIntoStringList);
REBUILD_FUNCTION(BaseApi_ConnectionValue1C4, 0x1415f4340, BaseApiConnectionValue1C4);

}  // namespace rebuild::csc
