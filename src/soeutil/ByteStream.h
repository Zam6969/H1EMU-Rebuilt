#pragma once

#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "soeutil/Memory.h"

// SoeUtil serialization stream: a pointer to the active byte array, an inline
// SoeUtil::Array<unsigned char,8192,1>, a size cap and a write cursor. Packet
// serializers build one on the stack and, when the global stream pool exists,
// swap in a pooled stream instead.
namespace rebuild::soeutil {

// SoeUtil::Array<unsigned char,8192,1>
struct ByteArray8k {
  void** vtable;
  uint8_t* data;
  int size;
  int unknown14;
  uint8_t storage[0x2008];
};
static_assert(offsetof(ByteArray8k, data) == 0x08);
static_assert(offsetof(ByteArray8k, size) == 0x10);
static_assert(sizeof(ByteArray8k) == 0x2020);

struct ByteStream {
  ByteArray8k* array;
  ByteArray8k inlineArray;
  int maxSize;
  int16_t unknown202C;
  int writePos;
  int unknown2034;
};
static_assert(offsetof(ByteStream, inlineArray) == 0x08);
static_assert(offsetof(ByteStream, maxSize) == 0x2028);
static_assert(offsetof(ByteStream, writePos) == 0x2030);
static_assert(sizeof(ByteStream) == 0x2038);

constexpr uintptr_t kVtByteArray8k = 0x14204b0a0;
constexpr int kByteStreamMaxSize = 0x10000000;

inline void* StreamPool() { return *reinterpret_cast<void**>(0x142b31b00); }

inline void ByteArrayDestroy(ByteArray8k* array) { game::Call<void (*)(ByteArray8k*)>(0x14030bf90)(array); }

// 0x14030d520: write count bytes at position (grows the array).
inline void ByteArrayWrite(ByteArray8k* array, int position, const void* source, int count) {
  game::Call<void (*)(ByteArray8k*, int, const void*, int)>(0x14030d520)(array, position, source, count);
}

// Stack stream plus the stream actually written to (pooled if a pool exists).
struct ScopedByteStream {
  ByteStream local;
  ByteStream* active;

  ScopedByteStream() {
    local.inlineArray.vtable = reinterpret_cast<void**>(kVtByteArray8k);
    local.inlineArray.data = nullptr;
    local.inlineArray.size = 0;
    local.inlineArray.unknown14 = 0;
    local.unknown202C = 0;
    local.maxSize = kByteStreamMaxSize;
    local.writePos = 0;
    local.array = &local.inlineArray;
    active = StreamPool() ? game::Call<ByteStream* (*)(void*)>(0x14063d5a0)(StreamPool()) : &local;
  }

  ~ScopedByteStream() {
    if (active != &local) {
      if (StreamPool()) {
        game::Call<void (*)(void*, ByteStream*)>(0x14063e360)(StreamPool(), active);
      } else if (active) {
        ByteArrayDestroy(&active->inlineArray);
        Free(active, sizeof(ByteStream));
      }
    }
    ByteArrayDestroy(&local.inlineArray);
  }

  ScopedByteStream(const ScopedByteStream&) = delete;
  ScopedByteStream& operator=(const ScopedByteStream&) = delete;

  // Appends bytes, clamped to the room left under maxSize (as the game does).
  void Put(const void* source, int count) {
    int room = active->maxSize - active->array->size;
    int n = room >= count ? count : room;
    ByteArrayWrite(active->array, active->writePos, source, n);
    active->writePos += n;
  }

  const uint8_t* Data() const { return active->array->size ? active->array->data : nullptr; }
  int Size() const { return active->array->size; }
};

}  // namespace rebuild::soeutil
