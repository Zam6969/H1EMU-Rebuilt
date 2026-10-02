#pragma once

#include <cstddef>

// UdpLibrary::UdpLinkedList<T> (vtable + 4 fields, 0x20 bytes): an intrusive
// doubly linked list. Each element embeds {T* prev; T* next;} at linkOffset.
namespace rebuild::udp {

template <class T>
struct UdpLinkedList {
  void** vtable;   // +0x00
  T* first;        // +0x08
  T* last;         // +0x10
  int linkOffset;  // +0x18 offset of the {prev, next} pair inside T
  int count;       // +0x1C

  T*& Prev(T* element) { return *reinterpret_cast<T**>(reinterpret_cast<char*>(element) + linkOffset); }
  T*& Next(T* element) {
    return *reinterpret_cast<T**>(reinterpret_cast<char*>(element) + linkOffset + sizeof(T*));
  }

  void AddHead(T* element) {
    Next(element) = first;
    if (first)
      Prev(first) = element;
    else
      last = element;
    first = element;
    ++count;
  }

  void AddTail(T* element) {
    Prev(element) = last;
    if (last)
      Next(last) = element;
    else
      first = element;
    last = element;
    ++count;
  }

  T* RemoveHead() {
    T* head = first;
    if (head) Remove(head);
    return head;
  }

  void Remove(T* element) {
    T* next = Next(element);
    if (Prev(element))
      Next(Prev(element)) = next;
    else
      first = next;
    if (next)
      Prev(next) = Prev(element);
    else
      last = Prev(element);
    Next(element) = nullptr;
    Prev(element) = nullptr;
    --count;
  }
};
static_assert(sizeof(UdpLinkedList<void>) == 0x20);

}  // namespace rebuild::udp
