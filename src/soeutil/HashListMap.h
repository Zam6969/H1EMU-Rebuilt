#pragma once

#include <cstddef>
#include <cstdint>

// SoeUtil::HashList<V, 1024, -1> / SoeUtil::HashListMap<int, V, 1024, -1>:
// an intrusive map keyed by int with 1024 buckets (key & 0x3FF) whose nodes
// are also kept on a doubly linked list, newest first. Node memory comes from
// the allocator in the vtable, so the layout must match the game exactly.
namespace rebuild::soeutil {

struct HashListMapNode {
  uint64_t value;             // +0x00
  HashListMapNode* next;      // +0x08 older entry
  HashListMapNode* prev;      // +0x10 newer entry
  int key;                    // +0x18
  HashListMapNode* hashNext;  // +0x20 bucket chain
};
static_assert(sizeof(HashListMapNode) == 0x28);

struct HashListMap {
  static constexpr int kBuckets = 1024;

  void** vtable;                        // +0x0000
  int unknown08;                        // +0x0008 zeroed by the constructor
  int maxCount;                         // +0x000C 0x7FFFFFFF
  HashListMapNode* first;               // +0x0010 newest
  HashListMapNode* last;                // +0x0018 oldest
  int count;                            // +0x0020
  HashListMapNode* buckets[kBuckets];   // +0x0028

  // vtable slots
  HashListMapNode* AllocateNode() {
    return reinterpret_cast<HashListMapNode* (*)(HashListMap*)>(vtable[4])(this);
  }
  void FreeNode(HashListMapNode* node) {
    reinterpret_cast<void (*)(HashListMap*, HashListMapNode*)>(vtable[5])(this, node);
  }

  HashListMapNode*& Bucket(int key) { return buckets[static_cast<uint32_t>(key) & (kBuckets - 1)]; }

  void Construct(void** vtableAddress);
  // Adds 1 to the value for `key`, inserting a zero entry first if missing.
  void Increment(int key);
  // Unlinks and frees every node (the body of the destructor).
  void Clear();
};
static_assert(offsetof(HashListMap, first) == 0x10);
static_assert(offsetof(HashListMap, count) == 0x20);
static_assert(offsetof(HashListMap, buckets) == 0x28);
static_assert(sizeof(HashListMap) == 0x2028);

// vtables in .rdata
inline void** const kHashListUInt64Vtable = reinterpret_cast<void**>(0x142052468);     // HashList<uint64,1024,-1>
inline void** const kHashListMapIntUInt64Vtable = reinterpret_cast<void**>(0x142052430);  // HashListMap<int,uint64,1024,-1>

}  // namespace rebuild::soeutil
