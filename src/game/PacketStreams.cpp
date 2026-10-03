// Small serialization helpers next to the zone connection code
// (0x14063c600..0x14063cb70): stream writers for tunnel messages, the
// channel-2 packet readers, an {opcode, int} packet reader, and the pooled
// stream holder.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/ByteStream.h"

namespace rebuild::game_net {
namespace {

using soeutil::ByteStream;
using soeutil::StreamPut;

// Same layout as the reader used by the zone packet code.
struct Reader {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint16_t failed;
};
static_assert(offsetof(Reader, failed) == 0x20);

}  // namespace

// 0x14063c600 / 0x14063c660: write the low byte / low u16 of an int.
void StreamPutU8(ByteStream* stream, const int* value) {
  uint8_t byte = static_cast<uint8_t>(*value);
  StreamPut(stream, &byte, 1);
}
void StreamPutU16(ByteStream* stream, const int* value) {
  uint16_t half = static_cast<uint16_t>(*value);
  StreamPut(stream, &half, 2);
}

using WriteHeaderFn = void (*)(const uint8_t*, ByteStream*);
constexpr uintptr_t kWriteTunnelHeader = 0x14063c730;  // u16 +0x08, u32 +0x0C

// 0x14063c6c0: tunnel header + the byte at +0x10.
void TunnelMessageWriteHeaderByte(const uint8_t* message, ByteStream* stream) {
  game::Call<WriteHeaderFn>(kWriteTunnelHeader)(message, stream);
  uint8_t byte = static_cast<uint8_t>(game::Field<int>(const_cast<uint8_t*>(message), 0x10));
  StreamPut(stream, &byte, 1);
}

// 0x14063c8f0: a chunk list {+0x08 head, +0x18 count}: the count, then each node via 0x140467ca0.
void StreamPutChunkList(ByteStream* stream, const uint8_t* list) {
  int count = game::Field<int>(const_cast<uint8_t*>(list), 0x18);
  StreamPut(stream, &count, 4);
  for (uint8_t* node = game::Field<uint8_t*>(const_cast<uint8_t*>(list), 8); node;
       node = game::Field<uint8_t*>(node, 0x10))
    game::Call<void (*)(ByteStream*, uint8_t*, uint8_t*)>(0x140467ca0)(stream, node, node + 8);
}

// 0x14063c7f0: tunnel header + byte at +0x10 + chunk list at +0x18.
void TunnelMessageWriteHeaderByteList(const uint8_t* message, ByteStream* stream) {
  game::Call<WriteHeaderFn>(kWriteTunnelHeader)(message, stream);
  uint8_t byte = static_cast<uint8_t>(game::Field<int>(const_cast<uint8_t*>(message), 0x10));
  StreamPut(stream, &byte, 1);
  game::Call<void (*)(ByteStream*, const uint8_t*)>(0x14063c8f0)(stream, message + 0x18);
}

// 0x14063c870: pack (+0x08 & 0x1F) | (+0x0C << 5) into one byte.
void StreamPutPackedByte(ByteStream** writer, const uint8_t* item) {
  ByteStream* stream = *writer;
  uint8_t byte = static_cast<uint8_t>((item[8] & 0x1F) | (item[0xC] << 5));
  StreamPut(stream, &byte, 1);
}

// 0x14063c9b0: read a channel-2 packet (value at +0x1B8, body at *(+0x1B0)).
void Channel2PacketRead(Reader* reader, uint8_t* packet) {
  game::Call<void (*)(Reader*, int*)>(0x140357100)(reader, reinterpret_cast<int*>(packet + 0x1B8));
  game::Call<void (*)(uint8_t*, Reader*)>(0x1403717d0)(game::Field<uint8_t*>(packet, 0x1B0), reader);
}

// 0x14063ca50: same from raw bytes; returns the reader's failure flag.
bool Channel2PacketReadBytes(const uint8_t* data, int length, uint8_t* packet) {
  Reader reader{data, length, data, data + length, 0};
  game::Call<void (*)(Reader*, int*)>(0x140357100)(&reader, reinterpret_cast<int*>(packet + 0x1B8));
  game::Call<void (*)(uint8_t*, Reader*)>(0x1403717d0)(game::Field<uint8_t*>(packet, 0x1B0), &reader);
  return static_cast<uint8_t>(reader.failed) != 0;
}

// 0x14063cab0: read {opcode byte -> +0x08, int -> +0x10}; unless
// allowTrailing, the packet must end right after the int.
bool OpcodeIntPacketRead(uint8_t* packet, const uint8_t* data, int length, bool allowTrailing) {
  if (!packet || !data) return false;
  const uint8_t* end = data + length;
  const uint8_t* cursor = data + 1;
  bool ok = cursor <= end;
  if (ok) {
    packet[8] = data[0];
  } else {
    packet[8] = 0;
    cursor = end;
  }
  if (cursor + 4 > end) {
    game::Field<int>(packet, 0x10) = 0;
    return false;
  }
  game::Field<int>(packet, 0x10) = *reinterpret_cast<const int*>(cursor);
  return ok && (allowTrailing || static_cast<int>(end - (cursor + 4)) < 1);
}

// {stream in use, local stream}: a pooled stream when the pool exists.
struct PooledStream {
  ByteStream* active;
  ByteStream local;
};
static_assert(offsetof(PooledStream, local) == 8 && sizeof(PooledStream) == 0x2040);

// 0x14063cb70: PooledStream constructor.
PooledStream* PooledStreamConstruct(PooledStream* self) {
  self->local.inlineArray.data = nullptr;
  self->local.inlineArray.size = 0;
  self->local.inlineArray.unknown14 = 0;
  self->local.inlineArray.vtable = reinterpret_cast<void**>(soeutil::kVtByteArray8k);
  self->local.unknown202C = 0;
  self->local.maxSize = soeutil::kByteStreamMaxSize;
  self->local.writePos = 0;
  self->local.array = &self->local.inlineArray;
  if (!soeutil::StreamPool())
    self->active = &self->local;
  else
    self->active = game::Call<ByteStream* (*)(void*)>(0x14063d5a0)(soeutil::StreamPool());
  return self;
}

REBUILD_FUNCTION(Stream_PutU8, 0x14063c600, StreamPutU8);
REBUILD_FUNCTION(Stream_PutU16, 0x14063c660, StreamPutU16);
REBUILD_FUNCTION(TunnelMessage_WriteHeaderByte, 0x14063c6c0, TunnelMessageWriteHeaderByte);
REBUILD_FUNCTION(TunnelMessage_WriteHeaderByteList, 0x14063c7f0, TunnelMessageWriteHeaderByteList);
REBUILD_FUNCTION(Stream_PutPackedByte, 0x14063c870, StreamPutPackedByte);
REBUILD_FUNCTION(Stream_PutChunkList, 0x14063c8f0, StreamPutChunkList);
REBUILD_FUNCTION(Channel2Packet_Read, 0x14063c9b0, Channel2PacketRead);
REBUILD_FUNCTION(Channel2Packet_ReadBytes, 0x14063ca50, Channel2PacketReadBytes);
REBUILD_FUNCTION(OpcodeIntPacket_Read, 0x14063cab0, OpcodeIntPacketRead);
REBUILD_FUNCTION(PooledStream_Construct, 0x14063cb70, PooledStreamConstruct);

}  // namespace rebuild::game_net
