// UdpManager's handling of raw incoming datagrams: routing to an existing
// connection, connect requests, address remapping, and unknown senders.
#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpManager.h"

namespace rebuild::udp {
namespace {

// UdpLibrary protocol opcodes (second byte after a leading 0x00).
constexpr uint8_t kPacketConnect = 0x01;
constexpr uint8_t kPacketTerminate = 0x05;
constexpr uint8_t kPacketKeepAlive = 0x1C;
constexpr uint8_t kPacketUnreachableConnection = 0x1D;
constexpr uint8_t kPacketRequestRemap = 0x1E;
constexpr uint8_t kPacketPortUnreachableTerminate = 0x1F;

constexpr size_t kUdpConnectionSize = 0x450;

UdpRefCount* Ref(UdpConnection* connection) { return reinterpret_cast<UdpRefCount*>(connection); }

template <class T>
T& Field(UdpConnection* connection, size_t offset) {
  return game::Field<T>(connection, offset);
}

uint32_t ReadBigEndian32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
         static_cast<uint32_t>(p[2]) << 8 | p[3];
}

bool IsOpcode(const UdpPacketBuffer* buffer, uint8_t opcode) {
  return buffer->data[0] == 0 && buffer->data[1] == opcode;
}

// Not rebuilt yet (UdpConnection and the event queue).
UdpConnection* ConstructConnection(void* memory, UdpManager* manager, UdpPacketBuffer* buffer) {
  return game::Call<UdpConnection* (*)(void*, UdpManager*, UdpPacketBuffer*)>(0x140345090)(
      memory, manager, buffer);
}
void ConnectionProcessRawPacket(UdpConnection* connection, UdpPacketBuffer* buffer) {
  game::Call<void (*)(UdpConnection*, UdpPacketBuffer*)>(0x1403491e0)(connection, buffer);
}
void ConnectionPortUnreachable(UdpConnection* connection, const UdpIpAddress* ip, int port) {
  game::Call<void (*)(UdpConnection*, const UdpIpAddress*, int)>(0x1403496c0)(connection, ip, port);
}

}  // namespace

// 0x1403412c0
int ConnectionGetStatus(UdpConnection* connection) {
  auto& guard = Field<UdpPlatformGuardObject>(connection, UdpConnectionInternals::kGuard);
  guard.Enter();
  int status = Field<int>(connection, UdpConnectionInternals::kStatus);
  guard.Leave();
  return status;
}

// 0x14033f190. Returns the connection with this connect code, AddRef'd.
UdpConnection* GetConnectionByCode(UdpManager* self, uint32_t connectCode) {
  self->ConnectionGuard().Enter();
  UdpConnection* found = nullptr;
  for (UdpConnection* c = self->codeTable->BucketFor(connectCode); c; c = ConnectionCodeTable::Next(c)) {
    if (ConnectionCodeTable::Key(c) != connectCode) continue;
    if (Field<uint32_t>(c, UdpConnectionFields::kConnectCode) == connectCode) {
      Ref(c)->VirtualAddRef();
      found = c;
      break;
    }
  }
  self->ConnectionGuard().Leave();
  return found;
}

// 0x1403438d0. If ip:port has a disconnect-pending entry, unlinks it and
// returns true. (The entry is not freed here, matching the original.)
bool TakeDisconnectPending(UdpManager* self, const UdpIpAddress* ip, int port) {
  for (DisconnectPendingEntry* entry = self->disconnectPending.first; entry;
       entry = self->disconnectPending.Next(entry)) {
    if (entry->ip == *ip && entry->port == port) {
      self->disconnectPending.Remove(entry);
      return true;
    }
  }
  return false;
}

// 0x140342a00. A "port unreachable" terminate from an address with no
// connection: hand it to the still-negotiating connection with that code.
void ProcessUnknownTerminate(UdpManager* self, const UdpIpAddress* ip, int port, uint32_t code) {
  self->ConnectionGuard().Enter();
  for (UdpConnection* c = self->connectionList.first; c; c = self->connectionList.Next(c)) {
    if (ConnectionGetStatus(c) == 0 && Field<uint32_t>(c, UdpConnectionInternals::kTerminateCode) == code) {
      UdpIpAddress copy = *ip;
      ConnectionPortUnreachable(c, &copy, port);
      break;
    }
  }
  self->ConnectionGuard().Leave();
}

// 0x140342ad0. Entry point for every datagram the socket hands up.
void ProcessRawPacket(UdpManager* self, UdpPacketBuffer* buffer) {
  if (buffer->length == 2 && IsOpcode(buffer, kPacketKeepAlive)) return;

  UdpIpAddress from = buffer->ip;
  UdpConnection* connection = GetConnection(self, &from, buffer->port);
  if (connection) {
    ConnectionProcessRawPacket(connection, buffer);
    Ref(connection)->VirtualRelease();
    return;
  }

  if (buffer->length == 0) return;

  if (buffer->length > 5 && IsOpcode(buffer, kPacketPortUnreachableTerminate)) {
    UdpIpAddress ip = buffer->ip;
    ProcessUnknownTerminate(self, &ip, buffer->port, ReadBigEndian32(buffer->data + 2));
    return;
  }

  if (!IsOpcode(buffer, kPacketConnect)) {
    // A known connection moved to a new address/port (NAT rebinding).
    if (self->AllowAddressRemapping() && IsOpcode(buffer, kPacketRequestRemap)) {
      uint32_t encryptCode = ReadBigEndian32(buffer->data + 6);
      UdpConnection* remapped = GetConnectionByCode(self, ReadBigEndian32(buffer->data + 2));
      if (remapped) {
        if ((self->AllowRemapFromAnyAddress() ||
             Field<UdpIpAddress>(remapped, UdpConnectionFields::kIp) == buffer->ip) &&
            Field<int>(remapped, UdpConnectionInternals::kEncryptCode) == static_cast<int>(encryptCode)) {
          self->ConnectionGuard().Enter();
          self->addressTable->Remove(remapped);
          Field<UdpIpAddress>(remapped, UdpConnectionFields::kIp) = buffer->ip;
          uint32_t port = static_cast<uint32_t>(buffer->port);
          Field<int>(remapped, UdpConnectionFields::kPort) = static_cast<int>(port);
          self->addressTable->Insert(remapped,
                                     (Field<UdpIpAddress>(remapped, UdpConnectionFields::kIp) ^ port) & 0x7FFFFFFF);
          Ref(remapped)->VirtualRelease();
          self->ConnectionGuard().Leave();
          return;
        }
        Ref(remapped)->VirtualRelease();
      }
    }
    // Tell the sender we have no connection for it (unless it is already
    // saying goodbye or reporting the same thing).
    if (self->ReplyUnreachableConnection() &&
        !(buffer->data[0] == 0 &&
          (buffer->data[1] == kPacketUnreachableConnection || buffer->data[1] == kPacketTerminate))) {
      uint8_t reply[2] = {0x00, kPacketUnreachableConnection};
      UdpIpAddress ip = buffer->ip;
      ActualSend(self, reply, sizeof(reply), &ip, buffer->port);
    }
    return;
  }

  // Connect request.
  if (self->MaxConnections() <= self->connectionList.count) return;
  if (!self->Handler()) return;
  UdpIpAddress ip = buffer->ip;
  bool wasPending = TakeDisconnectPending(self, &ip, buffer->port);
  if (self->OnlyAcceptPendingAddresses() && !wasPending) return;

  void* memory = soeutil::Allocate(kUdpConnectionSize);
  connection = memory ? ConstructConnection(memory, self, buffer) : nullptr;
  if (ConnectionGetStatus(connection) == 1) CallbackConnectRequest(self, connection);
  Ref(connection)->VirtualRelease();
}

// 0x140343350 / 0x140341a20: the address table's out-of-line Remove/Insert.
static bool AddressTableRemove(ConnectionAddressTable* table, UdpConnection* connection) {
  return table->Remove(connection);
}
static void AddressTableInsert(ConnectionAddressTable* table, UdpConnection* connection, uint32_t key) {
  table->Insert(connection, key);
}

REBUILD_FUNCTION(UdpConnection_GetStatus, 0x1403412c0, ConnectionGetStatus);
REBUILD_FUNCTION(UdpManager_GetConnectionByCode, 0x14033f190, GetConnectionByCode);
REBUILD_FUNCTION(UdpManager_TakeDisconnectPending, 0x1403438d0, TakeDisconnectPending);
REBUILD_FUNCTION(UdpManager_ProcessUnknownTerminate, 0x140342a00, ProcessUnknownTerminate);
REBUILD_FUNCTION(UdpManager_ProcessRawPacket, 0x140342ad0, ProcessRawPacket);
REBUILD_FUNCTION(UdpManager_AddressTable_Remove, 0x140343350, AddressTableRemove);
REBUILD_FUNCTION(UdpManager_AddressTable_Insert, 0x140341a20, AddressTableInsert);

}  // namespace rebuild::udp
