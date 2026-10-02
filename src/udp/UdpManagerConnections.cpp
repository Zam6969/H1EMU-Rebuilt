// UdpManager's connection bookkeeping: the address and connect-code hash
// tables and the connection list, all under guards[11].
#include "core/game.h"
#include "core/hook.h"
#include "udp/UdpManager.h"

namespace rebuild::udp {
namespace {

UdpRefCount* AsRefCount(UdpConnection* connection) { return reinterpret_cast<UdpRefCount*>(connection); }

template <class T>
T& ConnectionField(UdpConnection* connection, size_t offset) {
  return game::Field<T>(connection, offset);
}

uint32_t AddressKey(UdpIpAddress ip, int port) {
  return (ip ^ static_cast<uint32_t>(port)) & 0x7FFFFFFF;
}

// The game's prime table used by NextPrime (1000 entries starting at 2).
const int* const kPrimes = reinterpret_cast<const int*>(0x1429fa8d0);

}  // namespace

// 0x140342400. Smallest odd number >= value with no factor among the first
// 1000 primes (or that is below the square of the prime being tested).
uint32_t NextPrime(uint32_t value) {
  uint32_t candidate = (value & 1) ? value : value + 1;
  for (;;) {
    bool composite = false;
    for (uint32_t i = 0;; ++i) {
      int prime = kPrimes[i];
      if (static_cast<int>(candidate) <= prime * prime) return candidate;
      if (static_cast<int>(candidate) % prime == 0) {
        composite = true;
        break;
      }
      if (i + 1 > 999) return candidate;
    }
    if (composite) candidate += 2;
  }
}

// 0x14033e1f0. Returns the connection for ip:port with a reference added,
// or null.
UdpConnection* GetConnection(UdpManager* self, const UdpIpAddress* ip, int port) {
  self->ConnectionGuard().Enter();
  UdpConnection* found = nullptr;
  uint32_t key = AddressKey(*ip, port);
  for (UdpConnection* c = self->addressTable->BucketFor(key); c; c = ConnectionAddressTable::Next(c)) {
    if (ConnectionAddressTable::Key(c) != key) continue;
    if (ConnectionField<UdpIpAddress>(c, UdpConnectionFields::kIp) == *ip &&
        ConnectionField<int>(c, UdpConnectionFields::kPort) == port) {
      AsRefCount(c)->VirtualAddRef();
      found = c;
      break;
    }
  }
  self->ConnectionGuard().Leave();
  return found;
}

// 0x14033e090. Takes a reference and files the connection in the list and
// both hash tables.
void AddNewConnection(UdpManager* self, UdpConnection* connection) {
  self->ConnectionGuard().Enter();
  AsRefCount(connection)->VirtualAddRef();
  self->connectionList.AddHead(connection);
  self->addressTable->Insert(connection,
                             AddressKey(ConnectionField<UdpIpAddress>(connection, UdpConnectionFields::kIp),
                                        ConnectionField<int>(connection, UdpConnectionFields::kPort)));
  self->codeTable->Insert(connection, ConnectionField<uint32_t>(connection, UdpConnectionFields::kConnectCode));
  self->ConnectionGuard().Leave();
}

// 0x140344480 / 0x1403445b0
static void ResizeAddressTable(ConnectionAddressTable* table, int size) { table->Resize(size); }
static void ResizeCodeTable(ConnectionCodeTable* table, int size) { table->Resize(size); }

REBUILD_FUNCTION(UdpMisc_NextPrime, 0x140342400, NextPrime);
REBUILD_FUNCTION(UdpManager_GetConnection, 0x14033e1f0, GetConnection);
REBUILD_FUNCTION(UdpManager_AddNewConnection, 0x14033e090, AddNewConnection);
REBUILD_FUNCTION(UdpManager_AddressTable_Resize, 0x140344480, ResizeAddressTable);
REBUILD_FUNCTION(UdpManager_CodeTable_Resize, 0x1403445b0, ResizeCodeTable);

}  // namespace rebuild::udp
