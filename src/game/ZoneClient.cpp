// The game's zone client (vtable 0x1420dd6e0, 0x2B8 bytes): the gateway
// listener. Gateway::ExternalGatewayApi calls these slots:
//   0 dtor, 1 OnConnect, 2 OnDisconnect, 3 OnFailed, 4 OnLoginReply,
//   5 OnForcedLogout, 6 OnChannelIsRoutable, 7 OnConnectionIsNotRoutable,
//   8 OnTunnelData (zone packets -> game), 9 (unused).
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/ByteStream.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"

namespace rebuild::game_net {
namespace {

// "NetInfo" log channel name (char* global).
const char* NetInfoLog() { return *reinterpret_cast<const char**>(0x142053fd8); }

template <typename... Args>
void Log(uintptr_t format, Args... args) {
  game::Call<void (*)(const char*, const char*, ...)>(0x1402bab70)(NetInfoLog(), reinterpret_cast<const char*>(format),
                                                                   args...);
}

void* GameClient() { return *reinterpret_cast<void**>(0x142b19780); }

}  // namespace

// 0x14063dbb0 (slot 1): OnConnect - tell the game, log the character id.
void ZoneClientOnConnect(uint8_t* client, void* /*api*/) {
  game::Call<void (*)(void*)>(0x140430490)(GameClient());  // game: zone connected
  uint64_t characterId;
  uint64_t* id = game::Call<uint64_t* (*)(void*, uint64_t*)>(0x14063bc10)(game::Field<void*>(client, 8), &characterId);
  Log(0x1420dd730, *id);  // "RECEIVED=OnConnect %lld"
}

// 0x14063db80 (slot 6): OnChannelIsRoutable - logged only.
void ZoneClientOnChannelIsRoutable(uint8_t* /*client*/, void* /*api*/, int channel, bool isRoutable, bool flag) {
  Log(0x1420dd780, channel, static_cast<int>(isRoutable), flag);  // "RECEIVED=OnChannelIsRoutable channel=%d isRoutable=%d"
}

// 0x14063dc00 (slot 7): OnConnectionIsNotRoutable - logged only.
void ZoneClientOnConnectionIsNotRoutable(uint8_t* /*client*/, void* /*api*/) {
  Log(0x1420dd7b8);  // "RECEIVED=OnConnectionIsNotRoutable"
}

namespace {

// Bounds-checked read cursor used by the zone packet readers.
struct ZoneReader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(ZoneReader, failed) == 0x20);

// Channel-2 packet (opcode 0x79) built on the stack.
struct Channel2Packet {
  void** vtable;
  int opcode;
  int padding;
  uint8_t body[0x1A0];  // +0x10, ctor 0x1417f2640 / dtor 0x1417f2750
  uint8_t* bodyPtr;     // +0x1B0 (read by game client slot 73, 0x14040b750)
  int value;            // +0x1B8
};
static_assert(offsetof(Channel2Packet, bodyPtr) == 0x1B0);
static_assert(offsetof(Channel2Packet, value) == 0x1B8);

// Header-only view of an incoming zone packet passed to the game.
struct ZonePacketHeader {
  void** vtable;
  uint32_t opcode;
};

constexpr uint8_t kOpcodeF5 = 0xF5;
constexpr size_t kOpcodeF5Value = 0x2B0;  // float set by opcode 0xF5 (1200.0 at construction)
constexpr size_t kLastTime = 0x278;       // u64 time, set at construction

}  // namespace

// 0x14063de90 (slot 8): OnTunnelData - zone packets from the gateway.
// Channel 2 carries opcode 0x79 packets; opcode 0xF5 updates a float kept
// on the client; everything else goes to the game's packet handler (slot 42).
void ZoneClientOnTunnelData(uint8_t* client, void* /*api*/, int channel, const uint8_t* data, int length) {
  void* game = GameClient();
  if (channel == 2) {
    Channel2Packet packet;
    packet.opcode = 0x79;
    packet.vtable = reinterpret_cast<void**>(0x142064070);
    game::Call<void (*)(uint8_t*)>(0x1417f2640)(packet.body);
    packet.bodyPtr = packet.body;
    packet.value = *reinterpret_cast<int*>(0x142b319a8);
    packet.vtable = reinterpret_cast<void**>(0x1420dd668);
    ZoneReader reader{data, length, data, data + length, 0};
    game::Call<void (*)(ZoneReader*, int*)>(0x140357100)(&reader, &packet.value);
    game::Call<void (*)(uint8_t*, ZoneReader*)>(0x1403717d0)(packet.bodyPtr, &reader);
    using HandleFn = void (*)(void*, Channel2Packet*);
    reinterpret_cast<HandleFn>((*static_cast<void***>(game))[0x248 / 8])(game, &packet);
    game::Call<void (*)(uint8_t*)>(0x1417f2750)(packet.body);
    return;
  }
  ZonePacketHeader header{reinterpret_cast<void**>(0x1420633d0), 0};
  if (data) {
    const uint8_t* end = data + length;
    const uint8_t* cursor = data + 1;
    header.opcode = cursor > end ? 0 : data[0];
    if (header.opcode == kOpcodeF5) {
      bool failed = false;
      if (cursor > end) {
        failed = true;
        cursor = end;
      }
      if (cursor + 4 > end) return;
      float value = *reinterpret_cast<const float*>(cursor);
      if (failed) return;
      game::Field<float>(client, kOpcodeF5Value) = value;
      return;
    }
  }
  using HandleFn = void (*)(void*, int, ZonePacketHeader*, const uint8_t*, int);
  reinterpret_cast<HandleFn>((*static_cast<void***>(game))[0x150 / 8])(game, channel, &header, data, length);
}

// 0x14032fd30: *out = current time; returns out.
uint64_t* TimeNow(uint64_t* out) { return game::Call<uint64_t* (*)(uint64_t*)>(0x14032fd30)(out); }

// 0x14063cc10: ZoneClient(characterId, ticket, threaded, compression, port,
// params, unused) - creates the gateway connection (protocol from
// "ClientProtocol_1080", build from *0x1429fbd88) and registers as its listener.
uint8_t* ZoneClientConstruct(uint8_t* self, const uint64_t* characterId, const char* ticket, bool threaded,
                             bool compression, int port, void* params, int unused) {
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x1420dd6e0);
  game::Call<void (*)(uint8_t*)>(0x1417a5ee0)(self + 0x10);
  game::Field<uint64_t>(self, kLastTime) = 0;
  game::Field<int>(self, 0x280) = 0x32;
  game::Field<void*>(self, 0x288) = reinterpret_cast<void*>(0x1420dd6c0);  // list {vtable, head, tail, count}
  game::Field<int>(self, 0x2A0) = 0;
  game::Field<void*>(self, 0x290) = nullptr;
  game::Field<void*>(self, 0x298) = nullptr;
  TimeNow(reinterpret_cast<uint64_t*>(self + 0x2A8));
  soeutil::StringFixed<256> protocol;
  protocol.vtable = reinterpret_cast<void**>(0x142049e08);
  protocol.data = soeutil::EmptyStringData();
  protocol.length = 0;
  protocol.capacity = 0;
  soeutil::StringFormat(&protocol, reinterpret_cast<const char*>(0x142046fb8),
                        *reinterpret_cast<const char**>(0x142ac17a0));  // "%s", "ClientProtocol_1080"
  uint8_t* connection = nullptr;
  if (void* memory = soeutil::Allocate(0x80)) {
    uint64_t id = *characterId;
    using ConstructFn = uint8_t* (*)(void*, uint64_t*, const char*, const char*, const char*, bool, bool, int, void*,
                                     int, bool);
    connection = game::Call<ConstructFn>(0x14063b470)(memory, &id, ticket, protocol.data,
                                                      *reinterpret_cast<const char**>(0x1429fbd88), threaded,
                                                      compression, port, params, unused, true);
  }
  game::Field<uint8_t*>(self, 8) = connection;
  game::Call<void (*)(uint8_t*, void*)>(0x14063c2a0)(connection, self);  // SetListener
  game::Call<void (*)(uint8_t*, bool)>(0x14063c290)(game::Field<uint8_t*>(self, 8), false);  // SetLoginPending
  game::Field<int>(game::Call<uint8_t* (*)(uint8_t*)>(0x14063bcd0)(game::Field<uint8_t*>(self, 8)), 0x70) = 1;
  game::Field<int>(game::Call<uint8_t* (*)(uint8_t*)>(0x14063bcd0)(game::Field<uint8_t*>(self, 8)), 0x3C) = 5;
  game::Field<float>(self, 0x270) = 1.0f;
  game::Field<float>(self, 0x274) = 1.0f;
  uint64_t now;
  game::Field<uint64_t>(self, kLastTime) = *TimeNow(&now);
  game::Field<float>(self, kOpcodeF5Value) = 1200.0f;
  protocol.vtable = reinterpret_cast<void**>(0x142049de8);  // IStringFixed<char,256>
  soeutil::StringRelease(&protocol);
  return self;
}

// Frees container storage the way SoeUtil containers do: through the SoeUtil
// allocator while any thread has its own, otherwise delete[].
void FreeContainerStorage(void* data) {
  if (*reinterpret_cast<uint64_t*>(0x143e09638) == 0)
    soeutil::FreeArray(data);
  else
    soeutil::MemoryFree(data, 8);
}

// Small array {vtable 0x1420dd6a0, data, count}.
struct ZoneArray {
  void** vtable;
  void* data;
  int count;
  int padding;
};

// 0x14063ce60: ZoneArray destructor.
void ZoneArrayDestroy(ZoneArray* array) {
  array->count = 0;
  array->vtable = reinterpret_cast<void**>(0x1420dd6a0);
  FreeContainerStorage(array->data);
  array->data = nullptr;
}

// One of the client's four records (+0xE8, +0x150, +0x1B0, +0x210): an array
// at +8 and a string at +0x30.
struct ZoneRecord {
  void* unknown0;
  ZoneArray array;        // +0x08
  uint8_t unknown20[0x10];
  soeutil::IString text;  // +0x30
};
static_assert(offsetof(ZoneRecord, array) == 8 && offsetof(ZoneRecord, text) == 0x30);

// 0x14063cf70: ZoneRecord destructor.
void ZoneRecordDestroy(ZoneRecord* record) {
  record->text.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&record->text);
  record->array.vtable = reinterpret_cast<void**>(0x1420dd6a0);
  record->array.count = 0;
  FreeContainerStorage(record->array.data);
  record->array.data = nullptr;
}

// Intrusive list whose links live at fixed offsets inside the node.
template <size_t kNext, size_t kPrev>
void UnlinkHead(uint8_t* list) {
  uint8_t* node = game::Field<uint8_t*>(list, 8);
  uint8_t* next = game::Field<uint8_t*>(node, kNext);
  uint8_t* prev = game::Field<uint8_t*>(node, kPrev);
  if (!prev)
    game::Field<uint8_t*>(list, 8) = next;
  else
    game::Field<uint8_t*>(prev, kNext) = next;
  if (!next)
    game::Field<uint8_t*>(list, 0x10) = prev;
  else
    game::Field<uint8_t*>(next, kPrev) = prev;
  --game::Field<int>(list, 0x18);
}

void FreeListNode(uint8_t* list, uint8_t* node) {
  reinterpret_cast<void (*)(uint8_t*, uint8_t*)>((*reinterpret_cast<void***>(list))[3])(list, node);
}

// 0x14063cec0: list (vtable 0x1420dd670, links at +0x10/+0x18) destructor.
void ZoneListDestroy(uint8_t* list) {
  game::Field<void*>(list, 0) = reinterpret_cast<void*>(0x1420dd670);
  while (uint8_t* node = game::Field<uint8_t*>(list, 8)) {
    UnlinkHead<0x10, 0x18>(list);
    FreeListNode(list, node);
  }
}

// 0x14063e1c0: list of byte arrays (array at +8, links at +0x2038/+0x2040).
void ZoneStreamListClear(uint8_t* list) {
  while (uint8_t* node = game::Field<uint8_t*>(list, 8)) {
    UnlinkHead<0x2038, 0x2040>(list);
    soeutil::ByteArrayDestroy(reinterpret_cast<soeutil::ByteArray8k*>(node + 8));
    FreeListNode(list, node);
  }
}

// 0x14063d0c0: ZoneClient destructor.
void ZoneClientDestroy(uint8_t* self) {
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x1420dd6e0);
  if (uint8_t* connection = game::Field<uint8_t*>(self, 8)) {
    game::Call<void (*)(uint8_t*)>(0x14063b6d0)(connection);
    soeutil::Free(connection, 0x80);
  }
  game::Field<void*>(self, 0x288) = reinterpret_cast<void*>(0x1420dd6c0);
  ZoneStreamListClear(self + 0x288);
  ZoneRecordDestroy(reinterpret_cast<ZoneRecord*>(self + 0x210));
  ZoneRecordDestroy(reinterpret_cast<ZoneRecord*>(self + 0x1B0));
  ZoneRecordDestroy(reinterpret_cast<ZoneRecord*>(self + 0x150));
  ZoneRecordDestroy(reinterpret_cast<ZoneRecord*>(self + 0xE8));
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x1420dd620);
}

// Container node allocation, matching FreeContainerStorage: SoeUtil allocator
// (8-aligned) while any thread has its own, else new[](nothrow).
void* AllocateContainerStorage(size_t size) {
  if (*reinterpret_cast<uint64_t*>(0x143e09638) == 0)
    return game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(size, reinterpret_cast<const void*>(0x143c46658));
  return soeutil::MemoryAllocate(static_cast<int>(size), 8);
}

// Vtable slot 3 of the three zone client containers: free one node.
void ZoneContainerFreeNode(void*, void* node) { FreeContainerStorage(node); }  // 0x14063d750/770/790

// Slot 2 of the two lists: allocate a node.
void* ZoneListAllocateNode(void*) { return AllocateContainerStorage(0x20); }          // 0x14063d530
void* ZoneStreamListAllocateNode(void*) { return AllocateContainerStorage(0x2048); }  // 0x14063d560

// Scalar deleting destructors.
ZoneArray* ZoneArrayDeletingDestructor(ZoneArray* self, unsigned flags) {  // 0x14063d1f0
  ZoneArrayDestroy(self);
  if (flags & 1) soeutil::Free(self, sizeof(ZoneArray));
  return self;
}
uint8_t* ZoneListDeletingDestructor(uint8_t* self, unsigned flags) {  // 0x14063d260
  ZoneListDestroy(self);
  if (flags & 1) soeutil::Free(self, 0x20);
  return self;
}
uint8_t* ZoneStreamListDeletingDestructor(uint8_t* self, unsigned flags) {  // 0x14063d2a0
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x1420dd6c0);
  ZoneStreamListClear(self);
  if (flags & 1) soeutil::Free(self, 0x20);
  return self;
}
uint8_t* ZoneClientDeletingDestructor(uint8_t* self, unsigned flags) {  // 0x14063d370
  ZoneClientDestroy(self);
  if (flags & 1) soeutil::Free(self, 0x2B8);
  return self;
}
// 0x14063d3b0: the gateway listener base (vtable 0x1420dd620, 8 bytes).
uint8_t* GatewayListenerDeletingDestructor(uint8_t* self, unsigned flags) {
  game::Field<void*>(self, 0) = reinterpret_cast<void*>(0x1420dd620);
  if (flags & 1) soeutil::Free(self, 8);
  return self;
}

bool ZoneAlwaysTrue(void*) { return true; }  // 0x14063db30 / 0x14063db40

// Slots 3, 4, 5, 9: `ret 0` - nothing to do.
void ZoneClientIgnore() {}

REBUILD_FUNCTION(ZoneClient_Construct, 0x14063cc10, ZoneClientConstruct);
REBUILD_FUNCTION(ZoneClient_Destroy, 0x14063d0c0, ZoneClientDestroy);
REBUILD_FUNCTION(ZoneClient_RecordDestroy, 0x14063cf70, ZoneRecordDestroy);
REBUILD_FUNCTION(ZoneClient_ArrayDestroy, 0x14063ce60, ZoneArrayDestroy);
REBUILD_FUNCTION(ZoneClient_ListDestroy, 0x14063cec0, ZoneListDestroy);
REBUILD_FUNCTION(ZoneClient_StreamListClear, 0x14063e1c0, ZoneStreamListClear);
REBUILD_FUNCTION(ZoneClient_DeletingDestructor, 0x14063d370, ZoneClientDeletingDestructor);
REBUILD_FUNCTION(GatewayListener_DeletingDestructor, 0x14063d3b0, GatewayListenerDeletingDestructor);
REBUILD_FUNCTION(ZoneClient_ArrayDeletingDestructor, 0x14063d1f0, ZoneArrayDeletingDestructor);
REBUILD_FUNCTION(ZoneClient_ListDeletingDestructor, 0x14063d260, ZoneListDeletingDestructor);
REBUILD_FUNCTION(ZoneClient_StreamListDeletingDestructor, 0x14063d2a0, ZoneStreamListDeletingDestructor);
REBUILD_FUNCTION(ZoneClient_ArrayFreeNode, 0x14063d750, ZoneContainerFreeNode);
REBUILD_FUNCTION(ZoneClient_ListFreeNode, 0x14063d770, ZoneContainerFreeNode);
REBUILD_FUNCTION(ZoneClient_StreamListFreeNode, 0x14063d790, ZoneContainerFreeNode);
REBUILD_FUNCTION(ZoneClient_ListAllocateNode, 0x14063d530, ZoneListAllocateNode);
REBUILD_FUNCTION(ZoneClient_StreamListAllocateNode, 0x14063d560, ZoneStreamListAllocateNode);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_ListAlwaysTrue, 0x14063db30, ZoneAlwaysTrue);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_StreamListAlwaysTrue, 0x14063db40, ZoneAlwaysTrue);
REBUILD_FUNCTION_TOO_SMALL(GatewayListener_Slot1, 0x14063dbf0, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(GatewayListener_Slot2, 0x14063de10, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(GatewayListener_Slot3, 0x14063de30, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_ArraySlot3, 0x14063db50, ZoneClientIgnore);
REBUILD_FUNCTION(ZoneClient_OnConnect, 0x14063dbb0, ZoneClientOnConnect);
REBUILD_FUNCTION(ZoneClient_OnChannelIsRoutable, 0x14063db80, ZoneClientOnChannelIsRoutable);
REBUILD_FUNCTION(ZoneClient_OnConnectionIsNotRoutable, 0x14063dc00, ZoneClientOnConnectionIsNotRoutable);
REBUILD_FUNCTION(ZoneClient_OnTunnelData, 0x14063de90, ZoneClientOnTunnelData);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnFailed, 0x14063de20, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnForcedLogout, 0x14063de40, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_OnLoginReply, 0x14063de60, ZoneClientIgnore);
REBUILD_FUNCTION_TOO_SMALL(ZoneClient_Slot9, 0x14063de80, ZoneClientIgnore);

}  // namespace rebuild::game_net
