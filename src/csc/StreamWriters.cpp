// ClientServerCore serializers for 16-bit values (RPC ids and similar):
// separate template instances in the exe that all append two bytes.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"

#include "core/hook.h"
#include "soeutil/ByteStream.h"

namespace rebuild::csc {

// 0x1415f5ff0 / 0x1415f6050 / 0x1415f6340 / 0x1415f6400: write a u16.
void StreamWriteU16(soeutil::ByteStream* stream, const uint16_t* value) {
  uint16_t copy = *value;
  soeutil::StreamPut(stream, &copy, 2);
}

// 0x1415f6190: write the low 16 bits of an int.
void StreamWriteIntAsU16(soeutil::ByteStream* stream, const int* value) {
  uint16_t copy = static_cast<uint16_t>(*value);
  soeutil::StreamPut(stream, &copy, 2);
}

// 0x1415f63a0 / 0x1415f6460: write the low 16 bits of the int at +8
// (a strong-typed id wrapper).
void StreamWriteIdAsU16(soeutil::ByteStream* stream, const uint8_t* value) {
  uint16_t copy = static_cast<uint16_t>(*reinterpret_cast<const int*>(value + 8));
  soeutil::StreamPut(stream, &copy, 2);
}

namespace {
void PutBigEndian16(soeutil::ByteStream* stream, unsigned value) {
  uint8_t bytes[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
  soeutil::StreamPut(stream, bytes, 2);
}
}  // namespace

// 0x1415f60b0 / 0x1415f64c0: write a u16 big-endian (network order RPC id).
void StreamWriteU16BigEndian(soeutil::ByteStream* stream, const uint16_t* value) { PutBigEndian16(stream, *value); }

// 0x1415f61f0: write the low 16 bits of an int, big-endian.
void StreamWriteIntAsU16BigEndian(soeutil::ByteStream* stream, const unsigned* value) {
  PutBigEndian16(stream, *value);
}

// 0x1415f62d0: write the id at +8 big-endian (note: value first, stream second).
void StreamWriteIdAsU16BigEndian(const uint8_t* value, soeutil::ByteStream* stream) {
  PutBigEndian16(stream, *reinterpret_cast<const unsigned*>(value + 8));
}

// 0x1415f6790: ~ByteStream (only the inline array owns memory).
void ByteStreamDestroy(soeutil::ByteStream* stream) { soeutil::ByteArrayDestroy(&stream->inlineArray); }

// 0x1415f67c0: ~ClientServerCore::BasePacket
void BasePacketDestroy(void** packet) { *packet = reinterpret_cast<void*>(0x1424b11f8); }

// RPC handler table (hash of id -> handler, 64 buckets).
struct RpcHandlerTable {
  int unknown0;
  int maxCount;
  uint64_t unknown8;
  uint64_t unknown10;
  int count;
  int padding;
  void* buckets[64];
};
static_assert(offsetof(RpcHandlerTable, count) == 0x18);
static_assert(offsetof(RpcHandlerTable, buckets) == 0x20);

// 0x1415f6650: RpcHandlerTable()
RpcHandlerTable* RpcHandlerTableConstruct(RpcHandlerTable* table) {
  table->unknown8 = 0;
  table->unknown10 = 0;
  table->count = 0;
  std::memset(table->buckets, 0, sizeof(table->buckets));
  table->unknown0 = 0;
  table->maxCount = 0x7FFFFFFF;
  return table;
}

// RPC router: id encoding + handler table (see BaseApi.cpp RpcRoutePacket).
struct RpcRouterObject {
  int idEncoding;
  int padding;
  uint64_t unknown8;
  RpcHandlerTable handlers;
};
static_assert(offsetof(RpcRouterObject, handlers) == 0x10);

// 0x1415f6720: RpcRouter(idEncoding)
RpcRouterObject* RpcRouterConstruct(RpcRouterObject* router, int idEncoding) {
  router->handlers.unknown8 = 0;
  router->handlers.unknown10 = 0;
  router->handlers.count = 0;
  std::memset(router->handlers.buckets, 0, sizeof(router->handlers.buckets));
  router->handlers.unknown0 = 0;
  router->handlers.maxCount = 0x7FFFFFFF;
  router->idEncoding = idEncoding;
  router->unknown8 = 0;
  return router;
}

// 0x1415f67d0: ~RpcRouter
void RpcRouterDestroy(RpcRouterObject* router) {
  game::Call<void (*)(RpcHandlerTable*)>(0x1415f6b40)(&router->handlers);
}

REBUILD_FUNCTION(ClientServerCore_WriteU16BE_A, 0x1415f60b0, StreamWriteU16BigEndian);
REBUILD_FUNCTION(ClientServerCore_WriteU16BE_B, 0x1415f64c0, StreamWriteU16BigEndian);
REBUILD_FUNCTION(ClientServerCore_WriteIntAsU16BE, 0x1415f61f0, StreamWriteIntAsU16BigEndian);
REBUILD_FUNCTION(ClientServerCore_WriteIdAsU16BE, 0x1415f62d0, StreamWriteIdAsU16BigEndian);
REBUILD_FUNCTION(SoeUtil_ByteStream_Destroy, 0x1415f6790, ByteStreamDestroy);
REBUILD_FUNCTION(ClientServerCore_BasePacket_Destroy, 0x1415f67c0, BasePacketDestroy);
REBUILD_FUNCTION(ClientServerCore_RpcHandlerTable_Construct, 0x1415f6650, RpcHandlerTableConstruct);
REBUILD_FUNCTION(ClientServerCore_RpcRouter_Construct, 0x1415f6720, RpcRouterConstruct);
REBUILD_FUNCTION(ClientServerCore_RpcRouter_Destroy, 0x1415f67d0, RpcRouterDestroy);
REBUILD_FUNCTION(ClientServerCore_WriteU16_A, 0x1415f5ff0, StreamWriteU16);
REBUILD_FUNCTION(ClientServerCore_WriteU16_B, 0x1415f6050, StreamWriteU16);
REBUILD_FUNCTION(ClientServerCore_WriteU16_C, 0x1415f6340, StreamWriteU16);
REBUILD_FUNCTION(ClientServerCore_WriteU16_D, 0x1415f6400, StreamWriteU16);
REBUILD_FUNCTION(ClientServerCore_WriteIntAsU16, 0x1415f6190, StreamWriteIntAsU16);
REBUILD_FUNCTION(ClientServerCore_WriteIdAsU16_A, 0x1415f63a0, StreamWriteIdAsU16);
REBUILD_FUNCTION(ClientServerCore_WriteIdAsU16_B, 0x1415f6460, StreamWriteIdAsU16);

}  // namespace rebuild::csc
