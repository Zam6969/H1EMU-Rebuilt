#include "udp/UdpConnection.h"

#include <cstring>

#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/LogicalPacket.h"

namespace rebuild::udp {
namespace {

using O = ConnectionOffsets;

UdpRefCount* Ref(UdpConnection* c) { return reinterpret_cast<UdpRefCount*>(c); }
UdpManager* Manager(UdpConnection* c) { return ConnField<UdpManager*>(c, O::kManager); }

// Not rebuilt yet.
void ResetStatistics(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140348290)(c); }
void InternalGiveTime(UdpConnection* c) { game::Call<void (*)(UdpConnection*)>(0x140347360)(c); }
void Disconnect(UdpConnection* c, int flushTimeout, int reason) {
  game::Call<void (*)(UdpConnection*, int, int)>(0x1403471e0)(c, flushTimeout, reason);
}

// An MSVC pointer-to-member-function slot as UdpConnection stores them:
// {code address, this adjustment, padding}.
struct MethodSlot {
  uintptr_t function;
  int64_t thisAdjust;
  int64_t padding;
};
static_assert(sizeof(MethodSlot) == 0x18);

struct EncryptMethodTable {
  uintptr_t encrypt;
  uintptr_t decrypt;
};
// Per method: UdpConnection::Encrypt*/Decrypt* member functions.
constexpr EncryptMethodTable kMethods[] = {
    {0x140345d30, 0x140346220},  // none
    {0x140345da0, 0x140346290},  // user supplied
    {0x140345d60, 0x140346250},  // user supplied 2
    {0x140345f00, 0x1403463f0},  // xor buffer
    {0x140345de0, 0x1403462d0},  // xor
};

}  // namespace

// 0x140346fd0. Puts a connection into its initial state for `manager`.
void ConnectionInit(UdpConnection* c, UdpManager* manager, const UdpIpAddress* ip, int port) {
  ConnField<UdpManager*>(c, O::kManager) = manager;
  ConnField<UdpIpAddress>(c, O::kIp) = *ip;
  ConnField<int>(c, O::kPort) = port;
  ConnField<int>(c, 0xD0) = 0;
  ConnField<uint8_t>(c, 0xAC) = Manager(c)->params.At<uint8_t>(0x1B0);
  ConnField<uint8_t>(c, 0x1C8) = 0;
  ConnField<int64_t>(c, 0x238) = 0;
  ConnField<int64_t>(c, 0x248) = 0;
  ConnField<int64_t>(c, O::kCachedTime) = 0;
  ConnField<int64_t>(c, 0x240) = Manager(c) ? Manager(c)->CachedClock() : 0;
  ConnField<int64_t>(c, 0x228) = 0;
  ConnField<int64_t>(c, 0x230) = 0;
  ConnField<uint8_t>(c, O::kInGiveTime) = 0;
  ConnField<void*>(c, O::kHandler) = nullptr;
  ConnField<int>(c, 0x204) = 0;
  ConnField<uint8_t>(c, 0x208) = 0;
  ConnField<int>(c, 0x1BC) = Manager(c)->params.At<int>(0x30);
  ConnField<int>(c, 0x2B8) = Manager(c)->params.At<int>(0x20);
  void* buffer = soeutil::AllocateArray(static_cast<size_t>(Manager(c)->MaxRawPacketSize()));
  ConnField<void*>(c, 0x250) = buffer;
  ConnField<void*>(c, 0x258) = buffer;
  ConnField<int64_t>(c, 0x2C0) = 0;
  ConnField<int64_t>(c, 0x2C8) = 0;
  ConnField<void*>(c, O::kXorBuffer) = nullptr;
  ConnField<int>(c, O::kEncryptExpansionBytes) = 0;
  ConnField<int64_t>(c, 0x268) = 0;
  ConnField<int>(c, 0x270) = 0;
  ConnField<int64_t>(c, 0x1C0) = 0;
  ConnField<int>(c, 0x1B8) = 0;
  ConnField<int64_t>(c, 0x1B0) = Manager(c) ? Manager(c)->CachedClock() : ConnField<int64_t>(c, O::kCachedTime);
  ConnField<int>(c, 0xA8) = 0;
  ConnField<int64_t>(c, 0xD8) = 0;
  ConnField<uint8_t>(c, 0x1C9) = 0;
  ConnField<int64_t>(c, 0x2F8) = 0;
  ConnField<int64_t>(c, 0x300) = 0;
  ConnField<int64_t>(c, 0x308) = 0;
  std::memset(&ConnField<uint8_t>(c, 0x310), 0, 0x140);
  ResetStatistics(c);
  ConnField<int>(c, 0x284) = 0;
  ConnField<int64_t>(c, 0x1D0) = 0;
  ConnField<int64_t>(c, 0x1D8) = 0;
  ConnField<int64_t>(c, 0x1E0) = 0;
  ConnField<int64_t>(c, 0x1E8) = 0;
  std::memset(&ConnField<uint8_t>(c, 0xF0), 0, 0xC0);
}

// 0x140349ab0. Fills the two encrypt/decrypt passes from the negotiated
// methods and totals how many bytes they add to each packet. The XOR-buffer
// method lazily builds a key stream from the encrypt code.
void ConnectionSetupEncryption(UdpConnection* c) {
  ConnField<int>(c, O::kEncryptExpansionBytes) = 0;
  for (int pass = 0; pass < 2; ++pass) {
    int method = ConnField<int>(c, O::kEncryptMethods + pass * 4);
    if (method < kEncryptNone || method > kEncryptXor) continue;
    auto& encrypt = ConnField<MethodSlot>(c, O::kEncryptPasses + pass * sizeof(MethodSlot));
    auto& decrypt = ConnField<MethodSlot>(c, O::kDecryptPasses + pass * sizeof(MethodSlot));
    // The original leaves the high half of the padding word uninitialized;
    // it is never read.
    encrypt = {kMethods[method].encrypt, 0, 0};
    decrypt = {kMethods[method].decrypt, 0, 0};

    if (method == kEncryptUserSupplied) {
      ConnField<int>(c, O::kEncryptExpansionBytes) += Manager(c)->params.At<int>(0x164);
    } else if (method == kEncryptUserSupplied2) {
      ConnField<int>(c, O::kEncryptExpansionBytes) += Manager(c)->params.At<int>(0x168);
    } else if (method == kEncryptXorBuffer && !ConnField<uint8_t*>(c, O::kXorBuffer)) {
      int raw = Manager(c)->MaxRawPacketSize() + 1;
      int size = (((raw >> 31) & 3) + raw) & ~3;  // raw / 4 * 4, truncating toward zero
      auto* key = static_cast<uint8_t*>(soeutil::AllocateArray(size));
      int seed = ConnField<int>(c, O::kEncryptCode);
      ConnField<uint8_t*>(c, O::kXorBuffer) = key;
      for (int i = 0; i < size; ++i) key[i] = static_cast<uint8_t>(Random(&seed));
    }
  }
}

// 0x140346e80. The manager's per-connection tick. When the manager's two
// references are the only ones left, the application has let go of the
// connection, so it is disconnected instead (reason 14).
void ConnectionGiveTime(UdpConnection* c, bool fromManager) {
  auto& guard = ConnField<UdpPlatformGuardObject>(c, O::kStatusGuard);
  guard.Enter();
  if (Manager(c)) {
    if (fromManager &&
        reinterpret_cast<int (*)(UdpConnection*)>(Ref(c)->vtable[UdpRefCount::kGetRefCount])(c) == 2) {
      Disconnect(c, 0, 14);
    } else {
      UdpManager* manager = Manager(c);
      manager->VirtualAddRef();
      ConnField<uint8_t>(c, O::kInGiveTime) = 1;
      InternalGiveTime(c);
      ConnField<uint8_t>(c, O::kInGiveTime) = 0;
      manager->VirtualRelease();
    }
  }
  guard.Leave();
}

REBUILD_FUNCTION(UdpConnection_Init, 0x140346fd0, ConnectionInit);
REBUILD_FUNCTION(UdpConnection_SetupEncryption, 0x140349ab0, ConnectionSetupEncryption);
REBUILD_FUNCTION(UdpConnection_GiveTime, 0x140346e80, ConnectionGiveTime);

}  // namespace rebuild::udp
