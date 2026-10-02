#include "soeutil/HashListMap.h"

#include <cstring>

#include "core/hook.h"

namespace rebuild::soeutil {

void HashListMap::Construct(void** vtableAddress) {
  count = 0;
  first = nullptr;
  last = nullptr;
  std::memset(buckets, 0, sizeof(buckets));
  unknown08 = 0;
  maxCount = 0x7FFFFFFF;
  vtable = vtableAddress;
}

void HashListMap::Increment(int key) {
  HashListMapNode* node = Bucket(key);
  while (node && node->key != key) node = node->hashNext;

  if (!node) {
    // The game does not check for allocation failure either.
    node = AllocateNode();
    node->value = 0;
    node->key = key;
    node->prev = nullptr;
    node->next = first;
    if (first)
      first->prev = node;
    else
      last = node;
    first = node;
    node->hashNext = Bucket(node->key);
    Bucket(node->key) = node;
    ++count;
  }
  ++node->value;
}

void HashListMap::Clear() {
  while (HashListMapNode* node = first) {
    HashListMapNode** link = &Bucket(node->key);
    for (HashListMapNode* it = *link; it; link = &it->hashNext, it = it->hashNext) {
      if (it == node) {
        *link = it->hashNext;
        break;
      }
    }
    if (node->prev)
      node->prev->next = node->next;
    else
      first = node->next;
    if (node->next)
      node->next->prev = node->prev;
    else
      last = node->prev;
    --count;
    FreeNode(node);
  }
}

// 0x14034aaa0, HashListMap<int,uint64,1024,-1> destructor body
static void HashListMap_Clear(HashListMap* self) { self->Clear(); }

REBUILD_FUNCTION(HashListMap_IntUInt64_Clear, 0x14034aaa0, HashListMap_Clear);

}  // namespace rebuild::soeutil
