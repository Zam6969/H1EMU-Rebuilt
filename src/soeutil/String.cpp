// SoeUtil::IString<char> / StringFixed<N>: shared core plus the per-size
// virtual functions (each template instance has its own copies in the exe).
#include "soeutil/String.h"

#include <intrin.h>

#include <cstring>

#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"

namespace rebuild::soeutil {
namespace {

int* ShareCount(char* data) { return reinterpret_cast<int*>(data - 4); }

void* VirtualAllocate(IString* string, int bytes, int* capacity, bool* isHeap) {
  using Fn = void* (*)(IString*, int, int*, bool*);
  return reinterpret_cast<Fn>(string->vtable[kStringSlotAllocate])(string, bytes, capacity, isHeap);
}

void VirtualFree(IString* string, void* buffer) {
  reinterpret_cast<void (*)(IString*, void*)>(string->vtable[kStringSlotFree])(string, buffer);
}

// A heap block grows by 25% and rounds to 16 once the string already owns one.
int GrowHeapSize(const IString* string, int bytes) {
  if (string->capacity > 0) bytes = (bytes + bytes / 4 + 15) & ~15;
  return bytes;
}

void* HeapAllocate(int bytes) {
  if (ThreadAllocatorCount() == 0) {
    // operator new[](size, std::nothrow)
    return game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(static_cast<size_t>(bytes),
                                                                    reinterpret_cast<void*>(0x143c46658));
  }
  return MemoryAllocate(bytes, 0);
}

void HeapFree(void* buffer) {
  if (ThreadAllocatorCount() != 0) {
    MemoryFree(buffer, 0);
    return;
  }
  FreeArray(buffer);  // operator delete[]
}

// Must the buffer be reallocated before writing bytes (incl. terminator)?
bool NeedsUniqueBuffer(const IString* string, int bytes) {
  return string->capacity < bytes || (string->capacity > 0 && *ShareCount(string->data) > 1);
}

void SetEmpty(IString* string) {
  StringRelease(string);
  string->data = EmptyStringData();
  string->length = 0;
  string->capacity = 0;
}

}  // namespace

// Drops this string's share of its buffer, freeing it on the last share.
void StringRelease(IString* string) {
  if (string->capacity > 0) {
    int* count = ShareCount(string->data);
    if (_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(count), -1) < 2) VirtualFree(string, count);
  }
}

// 0x1402befa0: give the string its own buffer of at least `bytes` bytes,
// keeping the contents.
void StringReserve(IString* string, int bytes) {
  int length = string->length;
  if (bytes < length + 1) bytes = length + 1;
  int capacity;
  bool isHeap;
  auto* buffer = static_cast<int*>(VirtualAllocate(string, bytes + 4, &capacity, &isHeap));
  if (buffer) _InterlockedExchange(reinterpret_cast<volatile long*>(buffer), isHeap ? 1 : 0);
  std::memcpy(buffer + 1, string->data, static_cast<size_t>(length + 1));
  StringRelease(string);
  string->data = reinterpret_cast<char*>(buffer + 1);
  string->capacity = capacity - 4;
  string->length = length;
}

// 0x1402bd670
void StringAssign(IString* string, const char* text) {
  if (!text || !*text) {
    SetEmpty(string);
    return;
  }
  if (text == string->data && string->capacity > 0) return;
  int length = static_cast<int>(std::strlen(text));
  int bytes = length + 1;
  if (NeedsUniqueBuffer(string, bytes)) StringReserve(string, bytes);
  std::memcpy(string->data, text, static_cast<size_t>(bytes));
  string->length = length;
}

// 0x1402bd560: share the source's heap buffer when possible, else copy.
void StringAssignString(IString* string, const IString* source) {
  if (source == string) return;
  bool share = source->capacity == -1;  // static text: share the pointer
  if (source->capacity > 0) {
    // Take a share unless the source buffer is already being torn down.
    volatile long* count = reinterpret_cast<volatile long*>(ShareCount(source->data));
    for (long seen = *count; seen > 0;) {
      long previous = _InterlockedCompareExchange(count, seen + 1, seen);
      if (previous == seen) {
        share = true;
        break;
      }
      seen = previous;
    }
  }
  if (share) {
    StringRelease(string);
    string->data = source->data;
    string->length = source->length;
    string->capacity = source->capacity;
    return;
  }
  if (source->length != 0) {
    if (NeedsUniqueBuffer(string, source->length + 1)) StringReserve(string, source->length + 1);
    int length = source->length;
    string->length = length;
    std::memcpy(string->data, source->data, static_cast<size_t>(length + 1));
    return;
  }
  SetEmpty(string);
}

// 0x1402be6c0
void StringAssignN(IString* string, const char* text, int length) {
  if (length == 0) {
    SetEmpty(string);
    return;
  }
  if (NeedsUniqueBuffer(string, length + 1)) StringReserve(string, length + 1);
  std::memcpy(string->data, text, static_cast<size_t>(length));
  string->data[length] = '\0';
  string->length = length;
}

// 0x1402bd820: IString<char>::Allocate (always heap).
void* IStringAllocate(IString* string, int bytes, int* capacity, bool* isHeap) {
  bytes = GrowHeapSize(string, bytes);
  *isHeap = true;
  *capacity = bytes;
  return HeapAllocate(bytes);
}

// 0x1402bd880: IString<char>::Free
void IStringFree(IString* /*string*/, void* buffer) { HeapFree(buffer); }

// IStringFixed<char,N>::Allocate: the inline buffer while it fits.
template <int N>
void* StringFixedAllocate(StringFixed<N>* string, int bytes, int* capacity, bool* isHeap) {
  if (bytes < StringFixed<N>::kInlineBytes + 1) {
    *isHeap = false;
    *capacity = StringFixed<N>::kInlineBytes;
    return string->inlineBuffer;
  }
  bytes = GrowHeapSize(string, bytes);
  *isHeap = true;
  *capacity = bytes;
  return HeapAllocate(bytes);
}

template <int N>
void StringFixedFree(StringFixed<N>* string, void* buffer) {
  if (buffer == string->inlineBuffer) return;
  HeapFree(buffer);
}

// Scalar deleting destructor; IStringFixed<char,N> and StringFixed<N> share
// the body (StringFixed's first restores the IStringFixed vtable).
template <int N, uintptr_t kIStringFixedVtable>
StringFixed<N>* StringFixedDestroy(StringFixed<N>* string, unsigned flags) {
  string->vtable = reinterpret_cast<void**>(kIStringFixedVtable);
  SetEmpty(string);
  string->vtable = IStringVtable();
  if (flags & 1) Free(string, sizeof(StringFixed<N>));
  return string;
}

// SetEmpty writes length and capacity as one 8-byte zero, matching the game.
static_assert(offsetof(IString, capacity) == offsetof(IString, length) + 4);

REBUILD_FUNCTION(SoeUtil_String_Reserve, 0x1402befa0, StringReserve);
REBUILD_FUNCTION(SoeUtil_String_Assign, 0x1402bd670, static_cast<void (*)(IString*, const char*)>(StringAssign));
REBUILD_FUNCTION(SoeUtil_String_AssignString, 0x1402bd560, StringAssignString);
REBUILD_FUNCTION(SoeUtil_String_AssignN, 0x1402be6c0, StringAssignN);
REBUILD_FUNCTION(SoeUtil_IString_Allocate, 0x1402bd820, IStringAllocate);
REBUILD_FUNCTION(SoeUtil_IString_Free, 0x1402bd880, IStringFree);

// Per-size instances: {IStringFixed dtor, StringFixed dtor, Allocate, Free}.
#define STRING_FIXED_INSTANCE(N, ivt, idtor, dtor, alloc, free)                                        \
  REBUILD_FUNCTION(SoeUtil_IStringFixed_##N##_Destroy, idtor, (StringFixedDestroy<N, ivt>));           \
  REBUILD_FUNCTION(SoeUtil_StringFixed_##N##_Destroy, dtor, (StringFixedDestroy<N, ivt>));             \
  REBUILD_FUNCTION(SoeUtil_StringFixed_##N##_Allocate, alloc, StringFixedAllocate<N>);                 \
  REBUILD_FUNCTION(SoeUtil_StringFixed_##N##_Free, free, StringFixedFree<N>)

STRING_FIXED_INSTANCE(5, 0x1424bfc18, 0x14163c880, 0x14163c9f0, 0x14163d3e0, 0x14163da80);
STRING_FIXED_INSTANCE(6, 0x1424bfde8, 0x14163c900, 0x14163ca80, 0x14163d450, 0x14163dab0);
STRING_FIXED_INSTANCE(8, 0x1424b9ee0, 0x1403bdfc0, 0x1403c02d0, 0x1403cc360, 0x1403f1700);
STRING_FIXED_INSTANCE(10, 0x1424beb98, 0x1406286c0, 0x140bb9760, 0x140629f80, 0x14062dad0);
STRING_FIXED_INSTANCE(16, 0x14204ba88, 0x1403157e0, 0x140315930, 0x140315a90, 0x140316050);
STRING_FIXED_INSTANCE(32, 0x14204a358, 0x14030a6c0, 0x14030a740, 0x14030a810, 0x14030a930);
STRING_FIXED_INSTANCE(40, 0x142051ff8, 0x140345690, 0x140345710, 0x1403458c0, 0x140346a30);
STRING_FIXED_INSTANCE(48, 0x14204adf0, 0x14030cb80, 0x14030cde0, 0x14030d2b0, 0x14030df90);
STRING_FIXED_INSTANCE(64, 0x142049ce0, 0x1402be2b0, 0x1403064d0, 0x1402bd980, 0x1402bd9f0);
STRING_FIXED_INSTANCE(128, 0x142049da8, 0x1402be3b0, 0x1402bd0a0, 0x1402bdda0, 0x1402bde10);
STRING_FIXED_INSTANCE(256, 0x142049de8, 0x1403061b0, 0x1403063b0, 0x140306990, 0x140307610);
STRING_FIXED_INSTANCE(512, 0x14204ae80, 0x14030cb00, 0x14030cd50, 0x14030d240, 0x14030df60);
STRING_FIXED_INSTANCE(1024, 0x14204b2d0, 0x1402ef2c0, 0x1402ef340, 0x1402ef590, 0x1402ef710);
STRING_FIXED_INSTANCE(2048, 0x142049e28, 0x1403062b0, 0x140306560, 0x140306a70, 0x140307670);
STRING_FIXED_INSTANCE(4096, 0x14204b018, 0x14030ca80, 0x14030ccc0, 0x14030d1d0, 0x14030df30);
STRING_FIXED_INSTANCE(4098, 0x14204baf0, 0x140315860, 0x1403159c0, 0x140315b00, 0x140316080);
STRING_FIXED_INSTANCE(8192, 0x14204a248, 0x140306230, 0x140306440, 0x140306a00, 0x140307640);
STRING_FIXED_INSTANCE(32768, 0x14204a0f0, 0x140306330, 0x1403065f0, 0x140306ae0, 0x1403076a0);
STRING_FIXED_INSTANCE(65536, 0x14204f5b0, 0x140331970, 0x140331b70, 0x140331e70, 0x140332b10);

// StringFixed<24> only exists as IStringFixed in the exe.
REBUILD_FUNCTION(SoeUtil_IStringFixed_24_Destroy, 0x140618e40, (StringFixedDestroy<24, 0x1424b9ea0>));
REBUILD_FUNCTION(SoeUtil_StringFixed_24_Allocate, 0x140619510, StringFixedAllocate<24>);
REBUILD_FUNCTION(SoeUtil_StringFixed_24_Free, 0x14061a850, StringFixedFree<24>);

}  // namespace rebuild::soeutil
