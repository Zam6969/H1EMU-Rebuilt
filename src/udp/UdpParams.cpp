#include "udp/UdpParams.h"

#include "core/hook.h"

namespace rebuild::udp {

// 0x14033c720. Stores are in the original order; a few 8-byte stores set two
// adjacent 4-byte fields at once and are kept as 8-byte stores.
UdpParams* ConstructParams(UdpParams* p, int role) {
  p->At<uint32_t>(0x14) = 0x10000;
  p->At<uint64_t>(0x00) = 0;
  p->At<uint8_t>(0x4C) = 0;
  p->At<uint32_t>(0x50) = 0;
  p->At<uint64_t>(0x1A4) = 0;
  p->At<uint32_t>(0x18) = 0x10000;
  p->At<uint64_t>(0x1C) = 4;
  p->At<uint32_t>(0x3C) = 50;
  p->At<uint32_t>(0x40) = 0xFFFFFFFF;
  p->At<uint32_t>(0x44) = 0x200;
  p->At<uint32_t>(0x48) = 100;
  p->At<uint32_t>(0x1AC) = 0;
  p->At<uint32_t>(0x24) = 0;
  p->At<uint64_t>(0x2C) = 5000;
  p->At<uint64_t>(0x08) = 10;
  p->At<uint32_t>(0x10) = 0;
  p->At<uint64_t>(UdpParams::kPooledPacketMax) = 1000;  // also clears +0x5C
  p->At<uint32_t>(0x60) = 0xFFFFFFFF;
  p->At<uint16_t>(0x28) = 0x101;
  p->At<uint8_t>(0x2A) = 0;
  p->At<uint64_t>(0x34) = 120000;
  p->At<uint16_t>(0x68) = 1;
  p->At<uint32_t>(0x6C) = 1000;
  p->At<uint32_t>(0x54) = 10;
  p->At<uint8_t>(0x74) = 0;
  p->At<uint64_t>(0x170) = 0;
  p->At<uint32_t>(0x64) = 5000;
  p->At<uint8_t>(0x178) = 0;
  p->At<uint32_t>(0x70) = 20;
  p->At<uint32_t>(0x17C) = 0x1400000;
  p->At<uint16_t>(0x180) = 0;
  p->At<uint64_t>(0x164) = 0;
  p->At<uint8_t>(0x1B0) = 0;
  // reliable channel 0
  p->At<uint32_t>(0x9C) = 400;
  p->At<uint32_t>(0x94) = 0x32000;
  p->At<uint32_t>(0x98) = 400;
  p->At<uint16_t>(0xC4) = 0x100;
  p->At<uint8_t>(0xC6) = 1;
  p->At<uint64_t>(0xA0) = 0;
  p->At<uint32_t>(0xAC) = 300;
  p->At<uint32_t>(0xB0) = 125;
  p->At<uint64_t>(0xB4) = 8000;
  p->At<uint64_t>(0xBC) = 0x2000;
  p->At<uint32_t>(0xA8) = 0;

  bool internalPreset = false;
  switch (role) {
    case kRoleInternalServer:
      p->At<uint32_t>(0x14) = 0x400000;
      p->At<uint32_t>(0x18) = 0x400000;
      p->At<uint32_t>(0x48) = 10000;
      p->At<uint32_t>(0x08) = 2000;
      p->At<uint32_t>(UdpParams::kPooledPacketMax) = 20000;
      p->At<uint32_t>(UdpParams::kPooledPacketInitial) = 1000;
      internalPreset = true;
      break;
    case kRoleInternalClient:
      p->At<uint32_t>(0x14) = 0x100000;
      p->At<uint32_t>(0x18) = 0x100000;
      p->At<uint32_t>(0x48) = 10;
      p->At<uint32_t>(0x08) = 2;
      p->At<uint32_t>(UdpParams::kPooledPacketMax) = 2000;
      p->At<uint32_t>(UdpParams::kPooledPacketInitial) = 100;
      internalPreset = true;
      break;
    case kRoleExternalServer:
      p->At<uint32_t>(0x14) = 0x200000;
      p->At<uint32_t>(0x18) = 0x200000;
      p->At<uint32_t>(0x1A4) = 2;
      p->At<uint32_t>(0x2C) = 2500;
      p->At<uint32_t>(0x48) = 10000;
      p->At<uint32_t>(0x20) = 30000;
      p->At<uint32_t>(0x30) = 90000;
      p->At<uint32_t>(0x08) = 2000;
      p->At<uint32_t>(UdpParams::kPooledPacketMax) = 20000;
      p->At<uint32_t>(UdpParams::kPooledPacketInitial) = 1000;
      break;
    case kRoleExternalClient:
      p->At<uint32_t>(0x1A4) = 2;
      p->At<uint32_t>(0x2C) = 2500;
      p->At<uint32_t>(0x48) = 10;
      p->At<uint32_t>(0x20) = 30000;
      p->At<uint32_t>(0x30) = 90000;
      p->At<uint32_t>(0x08) = 2;
      p->At<uint32_t>(UdpParams::kPooledPacketMax) = 2000;
      p->At<uint32_t>(UdpParams::kPooledPacketInitial) = 10;
      break;
    case kRoleInternalBulk:
      p->At<uint32_t>(0x14) = 0x1000000;
      p->At<uint32_t>(0x18) = 0x1000000;
      p->At<uint32_t>(0x1A4) = 2;
      p->At<uint32_t>(0x2C) = 1500;
      p->At<uint32_t>(0x44) = 1460;
      p->At<uint32_t>(0x20) = 30000;
      p->At<uint32_t>(0x30) = 90000;
      p->At<uint32_t>(0x08) = 2;
      p->At<uint32_t>(UdpParams::kPooledPacketMax) = 50000;
      p->At<uint32_t>(UdpParams::kPooledPacketInitial) = 5000;
      p->At<uint8_t>(0x29) = 0;
      p->At<uint32_t>(0x17C) = 0xC800000;
      p->At<uint32_t>(0x64) = 50000;
      p->At<uint32_t>(0x9C) = 32000;
      p->At<uint32_t>(0x94) = 0x3200000;
      p->At<uint32_t>(0x98) = 32000;
      p->At<uint32_t>(0xB8) = 300000;
      p->At<uint32_t>(0xBC) = 300000;
      p->At<uint32_t>(0xC0) = 100;
      break;
    default:
      break;
  }

  // Shared tail of the two "internal" presets.
  if (internalPreset) {
    p->At<uint32_t>(0x1A4) = 2;
    p->At<uint32_t>(0x2C) = 500;
    p->At<uint32_t>(0x44) = 1460;
    p->At<uint32_t>(0x20) = 30000;
    p->At<uint32_t>(0x30) = 90000;
    p->At<uint8_t>(0x29) = 0;
    p->At<uint32_t>(0xAC) = 150;
    p->At<uint32_t>(0xBC) = 0x4000;
    p->At<uint32_t>(0xB8) = 0x1000;
    p->At<uint32_t>(0x98) = 1000;
    p->At<uint32_t>(0x94) = 0x100000;
    p->At<uint32_t>(0x9C) = 1000;
  }

  UdpReliableConfig* reliable = p->Reliable();
  reliable[1] = reliable[0];
  reliable[2] = reliable[0];
  reliable[3] = reliable[0];
  return p;
}

REBUILD_FUNCTION(UdpManagerParams_Construct, 0x14033c720, ConstructParams);

}  // namespace rebuild::udp
