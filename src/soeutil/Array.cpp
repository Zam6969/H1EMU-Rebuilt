// SoeUtil::Array<T,0,...> growth and the unserialize cursor's raw byte read.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::soeutil {
namespace {

// Byte-element SoeUtil array: {vtable, data, size, capacity}. Slot 1
// allocates (count, &capacity, alignment), slot 2 frees (data, capacity).
struct DynamicArray {
  void** vtable;
  uint8_t* data;
  int size;
  int capacity;
};
static_assert(sizeof(DynamicArray) == 0x18);

// Unserialize cursor: {start, length, cursor, end, failed}.
struct ReadCursor {
  const uint8_t* start;
  int length;
  const uint8_t* cursor;
  const uint8_t* end;
  uint8_t failed;
};
static_assert(offsetof(ReadCursor, cursor) == 0x10);
static_assert(offsetof(ReadCursor, failed) == 0x20);

}  // namespace

// 0x140339a70: Array::Resize(count). Grows the buffer if needed; the size
// becomes min(count, capacity).
void ArrayResize(DynamicArray* array, int count) {
  if (array->capacity < count) {
    int newCapacity;
    using AllocateFn = uint8_t* (*)(DynamicArray*, int, int*, int);
    uint8_t* data = reinterpret_cast<AllocateFn>(array->vtable[1])(array, count, &newCapacity, 1);
    uint8_t* old = array->data;
    if (data != old) {
      if (old) {
        std::memcpy(data, old, static_cast<size_t>(array->size));
        using FreeFn = void (*)(DynamicArray*, uint8_t*, int);
        reinterpret_cast<FreeFn>(array->vtable[2])(array, array->data, array->capacity);
      }
      array->capacity = newCapacity;
      array->data = data;
    }
  }
  array->size = array->capacity <= count ? array->capacity : count;
}

// 0x1403545d0: copy `count` raw bytes out of the cursor, or fail.
void ReadBytes(ReadCursor* in, void* destination, int count) {
  if (count >= 0 && reinterpret_cast<uintptr_t>(in->cursor) + static_cast<uintptr_t>(count) <=
                        reinterpret_cast<uintptr_t>(in->end)) {
    std::memcpy(destination, in->cursor, static_cast<size_t>(count));
    in->cursor += count;
    return;
  }
  in->cursor = in->end;
  in->failed = 1;
}

REBUILD_FUNCTION(SoeUtil_Array_Resize, 0x140339a70, ArrayResize);
REBUILD_FUNCTION(SoeUtil_ReadBytes, 0x1403545d0, ReadBytes);

}  // namespace rebuild::soeutil
