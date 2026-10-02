#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "soeutil/Memory.h"

// UdpLibrary's intrusive object hash table (0x18 bytes): T stores its own
// uint32 key at KeyOffset and its chain link at NextOffset. Bucket count is
// always a prime from UdpMisc NextPrime.
namespace rebuild::udp {

uint32_t NextPrime(uint32_t value);  // 0x140342400

template <class T, size_t KeyOffset, size_t NextOffset>
struct UdpHashTable {
  T** buckets;           // +0x00 operator new[]
  uint32_t bucketCount;  // +0x08
  int count;             // +0x0C objects in the table
  int usedBuckets;       // +0x10 non-empty buckets

  static uint32_t& Key(T* object) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(object) + KeyOffset);
  }
  static T*& Next(T* object) {
    return *reinterpret_cast<T**>(reinterpret_cast<uint8_t*>(object) + NextOffset);
  }

  T*& BucketFor(uint32_t key) { return buckets[static_cast<int>(key % bucketCount)]; }

  void Insert(T* object, uint32_t key) {
    Key(object) = key;
    T*& bucket = BucketFor(key);
    if (!bucket) {
      Next(object) = nullptr;
      bucket = object;
      ++usedBuckets;
    } else {
      Next(object) = bucket;
      bucket = object;
    }
    ++count;
  }

  // Unlinks `object` from its bucket. Returns false if it was not there.
  bool Remove(T* object) {
    T** link = &BucketFor(Key(object));
    int index = static_cast<int>(Key(object) % bucketCount);
    for (T* it = *link; it; link = &Next(it), it = *link) {
      if (it == object) {
        *link = Next(it);
        Next(it) = nullptr;
        --count;
        if (!buckets[index]) --usedBuckets;
        return true;
      }
    }
    return false;
  }

  // Rehashes every object into NextPrime(size) buckets.
  void Resize(int size) {
    T** oldBuckets = buckets;
    int oldCount = static_cast<int>(bucketCount);
    bucketCount = NextPrime(static_cast<uint32_t>(size));
    uint64_t bytes = static_cast<uint64_t>(static_cast<int>(bucketCount)) * 8;
    if (static_cast<int>(bucketCount) < 0) bytes = ~0ull;  // the original's overflow guard
    buckets = static_cast<T**>(soeutil::AllocateArray(bytes));
    std::memset(buckets, 0, static_cast<size_t>(static_cast<int>(bucketCount)) * 8);
    usedBuckets = 0;
    if (count > 0) {
      for (int64_t i = 0; i < oldCount; ++i) {
        for (T* object = oldBuckets[i]; object;) {
          T* next = Next(object);
          T*& bucket = BucketFor(Key(object));
          if (!bucket) {
            Next(object) = nullptr;
            bucket = object;
            ++usedBuckets;
          } else {
            Next(object) = bucket;
            bucket = object;
          }
          object = next;
        }
      }
    }
    soeutil::FreeArray(oldBuckets);
  }
};

}  // namespace rebuild::udp
