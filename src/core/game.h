#pragma once

#include <cstdint>

// Facts about the one H1Z1.exe build this rebuild targets. Every address in
// the project is a Ghidra address from that build; the exe has no ASLR
// (DYNAMICBASE is off), so they are also the live runtime addresses.
namespace rebuild::game {

inline constexpr uintptr_t kImageBase = 0x140000000;
inline constexpr uint32_t kTimestamp = 0x5859C0E4;  // 2016-12-20 23:38:12 UTC
inline constexpr uint32_t kImageSize = 0x4762800;

// True when the running exe is exactly the targeted build.
bool IsTargetBuild();

// Calls into the original game code that has not been rebuilt yet:
//   game::Call<int (*)(void*, int)>(0x140123450)(self, 5);
template <class Fn>
Fn Call(uintptr_t address) {
  return reinterpret_cast<Fn>(address);
}

// Reads a field at a raw offset, for structs only partly reconstructed.
template <class T>
T& Field(void* object, uintptr_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

}  // namespace rebuild::game
