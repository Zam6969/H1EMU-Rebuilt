// Scalar deleting destructors (vtable slot 0) of the networking classes:
// UdpLibrary linked lists and handlers, ClientServerCore / ClientServerCrypto
// APIs, and the Gateway / Login external packets. They come in three shapes,
// all rebuilt from templates:
//   - member-less bases: reset the vtable, then sized delete if (flags & 1);
//   - classes with a destructor: call it, then sized delete if (flags & 1);
//   - this-adjusting thunks of secondary vtables into the primary one.
// Also the default UdpManagerHandler encrypt/decrypt hooks, which only copy.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"

namespace rebuild::udp {
namespace {

void SizedDelete(void* object, size_t size) { game::Call<void (*)(void*, size_t)>(0x140d0fb84)(object, size); }

template <uint64_t Vtable, size_t Size>
void* SimpleDeletingDestructor(void* self, unsigned flags) {
  *static_cast<uint64_t*>(self) = Vtable;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

template <uint64_t Destructor, size_t Size>
void* DeletingDestructor(void* self, unsigned flags) {
  game::Call<void (*)(void*)>(Destructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

template <uint64_t Target, ptrdiff_t Adjust>
void* DeletingDestructorThunk(uint8_t* self, unsigned flags) {
  return game::Call<void* (*)(uint8_t*, unsigned)>(Target)(self + Adjust, flags);
}

// 0x1415f2e70: UdpCompressionHandler - destroy the mutex at +8, drop to the
// UdpManagerHandler vtable, sized delete 0x80.
void* CompressionHandlerDeletingDestructor(uint8_t* self, unsigned flags) {
  *reinterpret_cast<uint64_t*>(self) = 0x1424aff60;
  game::Call<void (*)(void*)>(0x14032f060)(self + 8);
  *reinterpret_cast<uint64_t*>(self) = 0x1424aff28;
  if (flags & 1) SizedDelete(self, 0x80);
  return self;
}

// Login packets holding one SoeUtil byte array {vtable 0x14204adc8, data
// +8, count +0x10} at ArrayOffset: clear it, free the data through the
// thread allocator (0x14032f980) or operator delete[] (0x1402fc170), then
// fall back to the Login BasePacket vtable.
template <size_t ArrayOffset, size_t Size>
void* ByteArrayPacketDeletingDestructor(uint8_t* self, unsigned flags) {
  uint8_t* array = self + ArrayOffset;
  *reinterpret_cast<int*>(array + 0x10) = 0;
  *reinterpret_cast<uint64_t*>(array) = 0x14204adc8;
  void* data = *reinterpret_cast<void**>(array + 8);
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    game::Call<void (*)(void*)>(0x1402fc170)(data);
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, 1);
  *reinterpret_cast<void**>(array + 8) = nullptr;
  *reinterpret_cast<uint64_t*>(self) = 0x1424bfc58;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

// 0x14163d120: PacketServerListReply - pop every server entry off the list
// at +0x10 (head +0x18) through 0x140adcbf0, then the BasePacket vtable.
void* ServerListReplyDeletingDestructor(uint8_t* self, unsigned flags) {
  *reinterpret_cast<uint64_t*>(self + 0x10) = 0x1424bfd80;
  while (void* head = *reinterpret_cast<void**>(self + 0x18))
    game::Call<void (*)(void*, void*)>(0x140adcbf0)(self + 0x10, head);
  *reinterpret_cast<uint64_t*>(self) = 0x1424bfc58;
  if (flags & 1) SizedDelete(self, 0x30);
  return self;
}

// Login API deleting destructors: restore the three-level vtable set, run
// the ExternalLoginApi part (embedded at LoginOffset), then the transport
// base destructor, then sized delete.
template <uint64_t Vtable, size_t SecondOffset, uint64_t SecondVtable, size_t ThirdOffset, uint64_t ThirdVtable, size_t LoginOffset,
          uint64_t LoginVtable, uint64_t BaseDestructor, size_t Size>
void* LoginApiDeletingDestructor(uint8_t* self, unsigned flags) {
  *reinterpret_cast<uint64_t*>(self) = Vtable;
  *reinterpret_cast<uint64_t*>(self + SecondOffset) = SecondVtable;
  if constexpr (ThirdOffset != 0) *reinterpret_cast<uint64_t*>(self + ThirdOffset) = ThirdVtable;
  *reinterpret_cast<uint64_t*>(self + LoginOffset) = LoginVtable;
  game::Call<void (*)(void*)>(0x14163bde0)(self + LoginOffset);  // ~ExternalLoginApi
  game::Call<void (*)(void*)>(BaseDestructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

// 0x140309bd0: BaseApp - clear the flag at +0x312D1.
void BaseAppClearFlag312D1(uint8_t* self) { self[0x312D1] = 0; }

// UdpManagerHandler default encrypt / decrypt / compress / decompress: copy
// the source bytes to the destination and return the length unchanged.
int HandlerCopyThrough(void* /*self*/, void* /*connection*/, uint8_t* destination, const uint8_t* source, int length) {
  std::memcpy(destination, source, static_cast<size_t>(length));
  return length;
}

}  // namespace
}  // namespace rebuild::udp

using namespace rebuild::udp;

#define SIMPLE_DTOR(name, address, vtable, size) \
  REBUILD_FUNCTION(name, address, (SimpleDeletingDestructor<vtable, size>))
#define MEMBER_DTOR(name, address, destructor, size) \
  REBUILD_FUNCTION(name, address, (DeletingDestructor<destructor, size>))
#define DTOR_THUNK(name, address, target, adjust) \
  REBUILD_FUNCTION(name, address, (DeletingDestructorThunk<target, adjust>))

// UdpLibrary
SIMPLE_DTOR(UdpLinkedList_T9027b3_DeletingDestructor, 0x14033d800, 0x1420516a8, 0x28);
SIMPLE_DTOR(UdpLinkedList_T2f050d_DeletingDestructor, 0x14033d770, 0x1420516b8, 0x20);
SIMPLE_DTOR(UdpLinkedList_T93bc63_DeletingDestructor, 0x14033d7d0, 0x1420516c8, 0x20);
SIMPLE_DTOR(UdpLinkedList_Ta4ed70_DeletingDestructor, 0x14033d7a0, 0x1420516d8, 0x20);
SIMPLE_DTOR(UdpLinkedList_Teb5b64_DeletingDestructor, 0x14033d740, 0x142051768, 0x20);
SIMPLE_DTOR(UdpLinkedList_T792f7e_DeletingDestructor, 0x14034c9b0, 0x142052bb8, 0x20);
SIMPLE_DTOR(UdpDriver_DeletingDestructor, 0x14034a330, 0x1420523a8, 0x8);
SIMPLE_DTOR(UdpManagerHandler_DeletingDestructor, 0x1415f2ec0, 0x1424aff28, 0x8);
SIMPLE_DTOR(UdpConnectionHandler_DeletingDestructor, 0x1415f3e70, 0x1424b0330, 0x8);
REBUILD_FUNCTION(UdpManagerHandler_EncryptCopy, 0x140342540, HandlerCopyThrough);
REBUILD_FUNCTION(UdpManagerHandler_DecryptCopy, 0x140342560, HandlerCopyThrough);
REBUILD_FUNCTION(UdpManagerHandler_CompressCopy, 0x140342580, HandlerCopyThrough);
REBUILD_FUNCTION(UdpManagerHandler_DecompressCopy, 0x1403425a0, HandlerCopyThrough);

// ClientServerCore / ClientServerCrypto
REBUILD_FUNCTION(UdpCompressionHandler_DeletingDestructor, 0x1415f2e70, CompressionHandlerDeletingDestructor);
SIMPLE_DTOR(RpcManagerHandler_DeletingDestructor, 0x1415f3e40, 0x1424b0368, 0x8);
SIMPLE_DTOR(BasePacket_DeletingDestructor, 0x1415f67e0, 0x1424b11f8, 0x10);
MEMBER_DTOR(BaseApp_DeletingDestructor, 0x140306680, 0x140305e10, 0x312f0);
MEMBER_DTOR(BaseConfig_DeletingDestructor, 0x14030a7d0, 0x14030a210, 0x35d0);
MEMBER_DTOR(BaseApi_DeletingDestructor, 0x1415f3dc0, 0x1415f3a40, 0xd28);
DTOR_THUNK(BaseApi_DeletingDestructor_Thunk80, 0x1415f3d9c, 0x1415f3dc0, -0x80);
DTOR_THUNK(BaseApi_DeletingDestructor_Thunk88, 0x1415f3da8, 0x1415f3dc0, -0x88);
MEMBER_DTOR(BaseUdpManager_DeletingDestructor, 0x1415f3e00, 0x1415f3c90, 0x2e0);
MEMBER_DTOR(CryptoBaseApi_DeletingDestructor, 0x1415f8cc0, 0x1415f8c20, 0xd40);
DTOR_THUNK(CryptoBaseApi_DeletingDestructor_Thunk80, 0x1415f8ca0, 0x1415f8cc0, -0x80);
DTOR_THUNK(CryptoBaseApi_DeletingDestructor_Thunk88, 0x1415f8cac, 0x1415f8cc0, -0x88);
MEMBER_DTOR(BaseTcpApi_DeletingDestructor, 0x141e954b0, 0x141e95250, 0xae0);

// Gateway
SIMPLE_DTOR(GatewayBasePacket_DeletingDestructor, 0x14063d2f0, 0x1424be218, 0x10);
MEMBER_DTOR(GatewayPacketLoginRequest_DeletingDestructor, 0x14162d030, 0x14162cdd0, 0xd8);
SIMPLE_DTOR(GatewayPacketLoginReply_DeletingDestructor, 0x14162d000, 0x1424be218, 0x18);
SIMPLE_DTOR(GatewayPacketLogout_DeletingDestructor, 0x14162d070, 0x1424be218, 0x10);
MEMBER_DTOR(GatewayPacketForcedLogout_DeletingDestructor, 0x14162cfc0, 0x14162cd40, 0x70);
SIMPLE_DTOR(GatewayPacketChannelIsRoutable_DeletingDestructor, 0x14162cf90, 0x1424be218, 0x18);
SIMPLE_DTOR(GatewayPacketTunnelToExternal_DeletingDestructor, 0x14162d0d0, 0x1424be218, 0x10);
SIMPLE_DTOR(GatewayPacketTunnelFromExternal_DeletingDestructor, 0x14162d0a0, 0x1424be218, 0x10);
DTOR_THUNK(ExternalGatewayApi_DeletingDestructor_Thunk80, 0x14162cf2c, 0x14162cf50, -0x80);
DTOR_THUNK(ExternalGatewayApi_DeletingDestructor_Thunk88, 0x14162cf38, 0x14162cf50, -0x88);
DTOR_THUNK(ExternalGatewayApi_DeletingDestructor_ThunkD40, 0x14162cf44, 0x14162cf50, -0xd40);

// Login
SIMPLE_DTOR(LoginBasePacket_DeletingDestructor, 0x14163cb10, 0x1424bfc58, 0x10);
MEMBER_DTOR(LoginPacketLoginRequest_DeletingDestructor, 0x14163d0b0, 0x14163c460, 0x178);
MEMBER_DTOR(LoginPacketLoginReply_DeletingDestructor, 0x14163d070, 0x14163c2f0, 0x1e0);
SIMPLE_DTOR(LoginPacketLogout_DeletingDestructor, 0x14163d0f0, 0x1424bfc58, 0x10);
SIMPLE_DTOR(LoginPacketForcedDisconnect_DeletingDestructor, 0x14163d040, 0x1424bfc58, 0x18);
SIMPLE_DTOR(LoginPacketCharacterCreateReply_DeletingDestructor, 0x14163cc70, 0x1424bfc58, 0x20);
SIMPLE_DTOR(LoginPacketCharacterDeleteRequest_DeletingDestructor, 0x14163cda0, 0x1424bfc58, 0x18);
SIMPLE_DTOR(LoginPacketCharacterSelectInfoRequest_DeletingDestructor, 0x14163cf10, 0x1424bfc58, 0x10);
MEMBER_DTOR(LoginPacketCharacterSelectInfoReply_DeletingDestructor, 0x14163ced0, 0x14163c170, 0x58);
SIMPLE_DTOR(LoginPacketServerListRequest_DeletingDestructor, 0x14163d1a0, 0x1424bfc58, 0x10);
MEMBER_DTOR(LoginPacketServerUpdate_DeletingDestructor, 0x14163d1d0, 0x14163c6a0, 0x20);
MEMBER_DTOR(ExternalLoginApi_DeletingDestructor, 0x14163cb40, 0x14163bde0, 0x40);
DTOR_THUNK(ExternalLoginUdpApi_DeletingDestructor_Thunk80, 0x14163c814, 0x14163cbf0, -0x80);
DTOR_THUNK(ExternalLoginUdpApi_DeletingDestructor_Thunk88, 0x14163c820, 0x14163cbf0, -0x88);
DTOR_THUNK(ExternalLoginUdpApi_DeletingDestructor_ThunkD40, 0x14163c82c, 0x14163cbf0, -0xd40);
DTOR_THUNK(ExternalLoginTcpApi_DeletingDestructor_ThunkAE0, 0x14163c808, 0x14163cb80, -0xae0);
MEMBER_DTOR(GameServerData_DeletingDestructor, 0x14163fc30, 0x14163fa60, 0x1200);

#define ARRAY_PACKET_DTOR(name, address, offset, size)   REBUILD_FUNCTION(name, address, (ByteArrayPacketDeletingDestructor<offset, size>))
ARRAY_PACKET_DTOR(LoginPacketCharacterCreateRequest_DeletingDestructor, 0x14163cca0, 0x18, 0x30);
ARRAY_PACKET_DTOR(LoginPacketCharacterLoginRequest_DeletingDestructor, 0x14163ce50, 0x28, 0x40);
ARRAY_PACKET_DTOR(LoginPacketCharacterLoginReply_DeletingDestructor, 0x14163cdd0, 0x30, 0x48);
ARRAY_PACKET_DTOR(LoginPacketCharacterDeleteReply_DeletingDestructor, 0x14163cd20, 0x28, 0x40);
ARRAY_PACKET_DTOR(LoginPacketTunnelAppClientToServer_DeletingDestructor, 0x14163d210, 0x20, 0x38);
ARRAY_PACKET_DTOR(LoginPacketTunnelAppServerToClient_DeletingDestructor, 0x14163d290, 0x20, 0x38);
ARRAY_PACKET_DTOR(LoginPacketCharacterTransferServerRequest_DeletingDestructor, 0x14163cfc0, 0x28, 0x40);
ARRAY_PACKET_DTOR(LoginPacketCharacterTransferServerReply_DeletingDestructor, 0x14163cf40, 0x30, 0x48);
REBUILD_FUNCTION(LoginPacketServerListReply_DeletingDestructor, 0x14163d120, ServerListReplyDeletingDestructor);

REBUILD_FUNCTION(ExternalLoginUdpApi_DeletingDestructor, 0x14163cbf0,
                 (LoginApiDeletingDestructor<0x1424bfe60, 0x80, 0x1424bff20, 0x88, 0x1424bff58, 0xd40, 0x1424bff70, 0x1415f8c20, 0xd80>));
REBUILD_FUNCTION(ExternalLoginTcpApi_DeletingDestructor, 0x14163cb80,
                 (LoginApiDeletingDestructor<0x1424bffa8, 0x8, 0x1424c0040, 0, 0, 0xae0, 0x1424c0068, 0x141e95250, 0xb20>));
REBUILD_FUNCTION(BaseApp_ClearFlag312D1, 0x140309bd0, BaseAppClearFlag312D1);
