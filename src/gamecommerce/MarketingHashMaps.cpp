// GameCommerce marketing-data containers: SoeUtil HashListMap instances
// (hash buckets + an insertion-ordered list). Each template instance in the
// exe differs only in its node layout, so the bodies are templates over the
// node field offsets.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Allocator.h"
#include "soeutil/Memory.h"
#include "soeutil/String.h"

namespace rebuild::gamecommerce {
namespace {

// Map header: {vtable (slot 5 frees a node), bucketCount +8, maxBuckets +0xC,
// head +0x10, tail +0x18, count +0x20, buckets +0x28 (inline array or pointer)}.
constexpr size_t kBucketCount = 0x08;
constexpr size_t kMaxBuckets = 0x0C;
constexpr size_t kHead = 0x10;
constexpr size_t kTail = 0x18;
constexpr size_t kCount = 0x20;
constexpr size_t kBuckets = 0x28;

uint8_t*& Ptr(uint8_t* object, size_t offset) { return *reinterpret_cast<uint8_t**>(object + offset); }

void FreeNode(uint8_t* map, uint8_t* node) {
  reinterpret_cast<void (*)(uint8_t*, uint8_t*)>((*reinterpret_cast<void***>(map))[5])(map, node);
}

// Remove from a map with an inline bucket array (kMask + 1 buckets).
template <size_t kHash, size_t kBucketNext, size_t kListNext, size_t kListPrev, unsigned kMask>
void UnlinkInline(uint8_t* map, uint8_t* node) {
  uint8_t** link = reinterpret_cast<uint8_t**>(map + kBuckets) + (game::Field<unsigned>(node, kHash) & kMask);
  for (uint8_t* it = *link; it; it = Ptr(it, kBucketNext)) {
    if (it == node) {
      *link = Ptr(it, kBucketNext);
      break;
    }
    link = &Ptr(it, kBucketNext);
  }
  uint8_t* next = Ptr(node, kListNext);
  uint8_t* previous = Ptr(node, kListPrev);
  if (previous) Ptr(previous, kListNext) = next; else Ptr(map, kHead) = next;
  if (next) Ptr(next, kListPrev) = previous; else Ptr(map, kTail) = previous;
  --game::Field<int>(map, kCount);
}

// Rehash a map whose buckets live in a separately allocated array.
template <size_t kHash, size_t kBucketNext>
void Rehash(uint8_t* map, int requested) {
  int oldCount = game::Field<int>(map, kBucketCount);
  uint8_t** oldBuckets = reinterpret_cast<uint8_t**>(Ptr(map, kBuckets));
  unsigned size = static_cast<unsigned>(requested - 1);
  size |= static_cast<int>(size) >> 1;
  size |= static_cast<int>(size) >> 2;
  size |= static_cast<int>(size) >> 4;
  size |= static_cast<int>(size) >> 8;
  size = (static_cast<int>(size) >> 16 | size) + 1;
  int count = static_cast<int>(size) > 7 ? static_cast<int>(size) : 8;
  if (count > game::Field<int>(map, kMaxBuckets)) return;
  game::Field<int>(map, kBucketCount) = count;
  void* buckets;
  if (soeutil::ThreadAllocatorCount() == 0) {
    buckets = game::Call<void* (*)(size_t, const void*)>(0x1402fc150)(static_cast<size_t>(count * 8),
                                                                       reinterpret_cast<void*>(0x143c46658));
  } else {
    buckets = soeutil::MemoryAllocate(count * 8, 0);
  }
  Ptr(map, kBuckets) = static_cast<uint8_t*>(buckets);
  std::memset(buckets, 0, static_cast<size_t>(game::Field<int>(map, kBucketCount)) << 3);
  if (game::Field<int>(map, kCount) > 0) {
    for (int i = 0; i < oldCount; ++i) {
      uint8_t* chain = oldBuckets[i];
      if (!chain) continue;
      // Reverse the chain so re-insertion at the head keeps the original order.
      uint8_t* reversed = nullptr;
      uint8_t* node;
      do {
        node = chain;
        chain = Ptr(node, kBucketNext);
        Ptr(node, kBucketNext) = reversed;
        reversed = node;
      } while (chain);
      while (node) {
        uint8_t* next = Ptr(node, kBucketNext);
        unsigned index = game::Field<unsigned>(node, kHash) & static_cast<unsigned>(game::Field<int>(map, kBucketCount) - 1);
        auto** table = reinterpret_cast<uint8_t**>(Ptr(map, kBuckets));
        Ptr(node, kBucketNext) = table[index];
        table[index] = node;
        node = next;
      }
    }
  }
  if (soeutil::ThreadAllocatorCount() == 0) {
    soeutil::FreeArray(oldBuckets);
  } else {
    soeutil::MemoryFree(oldBuckets, 0);
  }
}

}  // namespace

// 0x14162b6b0: HashListMap<.., StringFixed<32> value, 16 buckets>::Remove(node)
void MarketingStringMapRemove(uint8_t* map, uint8_t* node) {
  UnlinkInline<0x58, 0x60, 0x48, 0x50, 0xF>(map, node);
  auto* value = reinterpret_cast<soeutil::IString*>(node + 8);  // ~StringFixed<32>
  value->vtable = reinterpret_cast<void**>(0x14204a358);
  soeutil::StringRelease(value);
  value->data = soeutil::EmptyStringData();
  value->length = 0;
  value->capacity = 0;
  value->vtable = soeutil::IStringVtable();
  FreeNode(map, node);
}

// 0x14162b7a0: HashListMap<.., MarketingData record, 32 buckets>::Remove(node)
void MarketingRecordMapRemove(uint8_t* map, uint8_t* node) {
  UnlinkInline<0x260, 0x268, 0x250, 0x258, 0x1F>(map, node);
  game::Call<void (*)(uint8_t*)>(0x141629bc0)(node);  // ~record
  FreeNode(map, node);
}

// 0x14162b870
void MarketingStringMapClear(uint8_t* map) {
  while (uint8_t* head = Ptr(map, kHead)) MarketingStringMapRemove(map, head);
}

// 0x14162b8a0
void MarketingRecordMapClear(uint8_t* map) {
  while (uint8_t* head = Ptr(map, kHead)) MarketingRecordMapRemove(map, head);
}

// 0x14162b930 / 0x14162ba90: Rehash(requestedBuckets)
void MarketingStringHashRehash(uint8_t* map, int requested) { Rehash<0x58, 0x60>(map, requested); }
void MarketingRecordHashRehash(uint8_t* map, int requested) { Rehash<0x260, 0x268>(map, requested); }

REBUILD_FUNCTION(GameCommerce_MarketingStringMap_Remove, 0x14162b6b0, MarketingStringMapRemove);
REBUILD_FUNCTION(GameCommerce_MarketingRecordMap_Remove, 0x14162b7a0, MarketingRecordMapRemove);
REBUILD_FUNCTION(GameCommerce_MarketingStringMap_Clear, 0x14162b870, MarketingStringMapClear);
REBUILD_FUNCTION(GameCommerce_MarketingRecordMap_Clear, 0x14162b8a0, MarketingRecordMapClear);
REBUILD_FUNCTION(GameCommerce_MarketingStringHash_Rehash, 0x14162b930, MarketingStringHashRehash);
REBUILD_FUNCTION(GameCommerce_MarketingRecordHash_Rehash, 0x14162ba90, MarketingRecordHashRehash);

}  // namespace rebuild::gamecommerce
