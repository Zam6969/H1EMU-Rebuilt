#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// UdpLibrary::UdpManager::Params (0x1B8 bytes), copied into UdpManager at
// +0x18. Most field names are not known yet, so fields are addressed by
// offset; the ones whose use has been seen are named.
namespace rebuild::udp {

// One per reliable channel (4 channels); channel 0 is the template the
// constructor copies into 1-3.
struct UdpReliableConfig {
  uint8_t raw[0x34];
};
static_assert(sizeof(UdpReliableConfig) == 0x34);

struct UdpParams {
  uint8_t raw[0x1B8];

  template <class T>
  T& At(size_t offset) {
    return *reinterpret_cast<T*>(raw + offset);
  }

  static constexpr size_t kPooledPacketMax = 0x58;
  static constexpr size_t kPooledPacketInitial = 0x5C;
  static constexpr size_t kReliable = 0x94;  // UdpReliableConfig[4]

  UdpReliableConfig* Reliable() { return reinterpret_cast<UdpReliableConfig*>(raw + kReliable); }
};
static_assert(sizeof(UdpParams) == 0x1B8);

// The role passed to the Params constructor selects a preset. The names are
// inferred from the preset values (buffer sizes, connection counts), not known.
enum UdpManagerRole {
  kRoleDefault = 0,
  kRoleInternalServer = 1,
  kRoleInternalClient = 2,
  kRoleExternalServer = 3,
  kRoleExternalClient = 4,
  kRoleInternalBulk = 5,
};

UdpParams* ConstructParams(UdpParams* self, int role);  // 0x14033c720

}  // namespace rebuild::udp
