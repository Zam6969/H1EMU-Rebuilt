// One-off small vtable methods across GameCommerce, Crypto, TaskManagement,
// SpeedTree, LZMA and the input manager (classes with no shared shape).
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::game_small {
namespace {

template <typename T>
T& At(void* base, size_t offset) {
  return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
void SizedDelete(void* self, size_t size) { game::Call<void (*)(void*, size_t)>(0x140d0fb84)(self, size); }
void Memset(void* target, int value, size_t size) { game::Call<void* (*)(void*, int, size_t)>(0x140d12270)(target, value, size); }
using FormatFn = void (*)(soeutil::IString*, const char*, ...);

}  // namespace

// 0x14161f150: ClientInGamePurchaseOrder: unlink every entry of the list at
// +0x128 (head at list +0x10).
void PurchaseOrderClearEntries(uint8_t* self) {
  uint8_t* list = self + 0x128;
  while (void* head = At<void*>(list, 0x10)) game::Call<void (*)(void*, void*)>(0x14161f080)(list, head);
}

// 0x1415fa2e0: Crypto::Sha256 Final(out): finish into `out`, wipe the
// context (0x68 bytes at +8), clear the started flag.
void Sha256Final(uint8_t* self, uint8_t* out) {
  game::Call<void (*)(void*, void*)>(0x1415fb100)(out, At<void*>(self, 8));
  Memset(At<void*>(self, 8), 0, 0x68);
  At<bool>(self, 0x10) = false;
}

// 0x1416218e0: StoreShortcutDefinition ToString.
const char* StoreShortcutToString(uint8_t* self, soeutil::IString* out) {
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bcb88), At<int>(self, 8), At<int>(self, 0xC), At<int>(self, 0x14));
  return out->data;
}

// 0x1417c5eb0: StoreBundleCategoryDefinition IsValid: id >= the minimum
// valid id (0x143dd19b0), sort order >= 0, and the name member validates.
bool StoreBundleCategoryIsValid(uint8_t* self) {
  if (At<int>(self, 8) < *reinterpret_cast<int*>(0x143dd19b0) || At<int>(self, 0xC) < 0) return false;
  void* name = self + 0x10;
  return (*reinterpret_cast<bool (***)(void*)>(name))[0](name);
}

// 0x141e57f60: SpeedTree::CFileSystem::FileExists: try to fopen the path.
bool SpeedTreeFileExists(void*, const char* path) {
  void* file = game::Call<void* (*)(const char*, const char*)>(0x140d25b1c)(path, reinterpret_cast<const char*>(0x14204c29c));
  bool exists = file != nullptr;
  if (file) game::Call<int (*)(void*)>(0x140d254c8)(file);
  return exists;
}

// 0x141e580c0: SpeedTree::CFileSystem::Release(buffer): return a pooled
// buffer to its slot, else free it.
void SpeedTreeReleaseFileData(void*, void* buffer) {
  if (!buffer) return;
  int slot = game::Call<int (*)(void*)>(0x141e62910)(buffer);
  if (slot > -1) {
    game::Call<void (*)(int)>(0x141e62cb0)(slot);
    return;
  }
  game::Call<void (*)(void**)>(0x141e57a10)(&buffer);
}

// 0x1416282c0: MarketingDataSourceFlatFileLoader Prepare: unless already
// done (+0x253), make sure the read buffer (+0x10) holds 0x100001 bytes.
bool FlatFileLoaderPrepare(uint8_t* self) {
  if (At<bool>(self, 0x253)) return false;
  auto* buffer = reinterpret_cast<soeutil::IString*>(self + 0x10);
  if (buffer->capacity < 0x100001) soeutil::StringReserve(buffer, 0x100001);
  return true;
}

// 0x14162bc30: MarketingDataSourceLoader scalar deleting destructor.
void* MarketingDataSourceLoaderDeletingDestructor(uint8_t* self, unsigned flags) {
  At<void*>(self, 8) = nullptr;
  At<uint64_t>(self, 0) = 0x1424bdf10;
  if (flags & 1) SizedDelete(self, 0x10);
  return self;
}

// 0x140312cb0: SoeUtil::TRateTracker Reset(window): clear the 256 buckets
// (+8) and the running totals.
void RateTrackerReset(uint8_t* self, int window) {
  At<int>(self, 0x414) = window;
  Memset(self + 8, 0, 0x400);
  At<uint64_t>(self, 0x408) = 0;
  At<int>(self, 0x410) = 0;
}

// 0x141625fa0: MarketingDataElementInstance (by-value elements) RemoveAt.
void MarketingElementRemoveAt(uint8_t* self, int index) {
  if (index < (*reinterpret_cast<int (***)(void*)>(self))[2](self)) game::Call<void (*)(void*, int, int)>(0x141625390)(self + 8, index, 1);
}

// 0x141501700: Audio::FakeIoLayer scalar deleting destructor.
void* FakeIoLayerDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 8) = 0x14248bbc8;
  At<uint64_t>(self, 0) = 0x14248bc38;
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x1416790f0: TaskManagement::ScheduledTaskNode NextRunTime(out, now):
// now + the virtual delay (slot 2) computed from a copy of now.
int64_t* ScheduledTaskNextRun(void** self, int64_t* out, const int64_t* now) {
  int64_t copy = *now;
  int64_t delay = reinterpret_cast<int64_t (*)(void*, int64_t*)>(reinterpret_cast<void**>(*self)[2])(self, &copy);
  *out = delay + *now;
  return out;
}

// 0x1403517a0: GameClientInputManager: run 0x141667970 under the optional
// mutex (+0x81F0).
void InputManagerLockedUpdate(uint8_t* self) {
  if (void* lock = At<void*>(self, 0x81F0)) game::Call<void (*)(void*)>(0x14032f270)(lock);
  game::Call<void (*)(void*)>(0x141667970)(self);
  if (void* lock = At<void*>(self, 0x81F0)) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

// 0x141647f90: 7-Zip CLZInWindow::MovePos.
uint32_t LzInWindowMovePos(uint8_t* self) {
  uint32_t pos = ++At<uint32_t>(self, 0x2C);
  if (pos <= At<uint32_t>(self, 0x10)) return 0;
  if (pos + At<uint64_t>(self, 0x20) > At<uint64_t>(self, 0x18)) game::Call<void (*)(void*)>(0x14164a5c0)(self);
  return At<uint8_t>(self, 0x14) != 0 ? 0 : 0x80004005;
}

// 0x141674810: TaskManagement::ScheduledTaskNode scalar deleting destructor.
void* ScheduledTaskNodeDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0x50) = 0x1424c5958;
  game::Call<void (*)(void*)>(0x141678a10)(self);
  if (flags & 1) SizedDelete(self, 0x80);
  return self;
}

// 0x1416747d0: TaskManagement::EventTaskNode scalar deleting destructor.
void* EventTaskNodeDeletingDestructor(uint8_t* self, unsigned flags) {
  game::Call<void (*)(void*)>(0x141673cf0)(self + 0x50);
  game::Call<void (*)(void*)>(0x141678a10)(self);
  if (flags & 1) SizedDelete(self, 0x90);
  return self;
}

namespace {
template <typename R = void, typename... A>
R Virtual(void* self, int slot, A... args) {
  return reinterpret_cast<R (*)(void*, A...)>((*reinterpret_cast<void***>(self))[slot])(self, args...);
}
}  // namespace

// 0x1417dd940: StoreBillboardPanelDefinition IsValid: ids >= 0 and both
// members (+0x10, +0x48) validate.
bool StoreBillboardPanelIsValid(uint8_t* self) {
  return At<int>(self, 8) >= 0 && At<int>(self, 0xC) >= 0 && Virtual<bool>(self + 0x10, 0) && Virtual<bool>(self + 0x48, 0);
}

// 0x141621ca0: StorePortalCategoryDefinition IsValid.
bool StorePortalCategoryIsValid(uint8_t* self) {
  return At<int>(self, 0xC) > 0 && At<int>(self, 0x14) >= 0 && At<int>(self, 0x10) >= 0 && Virtual<bool>(self + 0x18, 0) &&
         Virtual<bool>(self + 0x50, 0);
}

// 0x140351750: GameClientInputManager: forward to 0x1416678f0 under the
// optional mutex (+0x81F0).
void InputManagerLockedForward(uint8_t* self, void* argument) {
  if (void* lock = At<void*>(self, 0x81F0)) game::Call<void (*)(void*)>(0x14032f270)(lock);
  game::Call<void (*)(void*, void*)>(0x1416678f0)(self, argument);
  if (void* lock = At<void*>(self, 0x81F0)) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

// 0x141e96510: StoreBundleDefinition IsValid: the MarketingBundleDefinition
// base check (0x14161bf80; its result is ignored) and five counts >= 0.
bool StoreBundleIsValid(uint8_t* self) {
  game::Call<bool (*)(void*)>(0x14161bf80)(self);
  for (size_t offset = 0x1A8; offset <= 0x1B8; offset += 4)
    if (At<int>(self, offset) < 0) return false;
  return true;
}

// 0x14161e1e0: ClientInGamePurchaseOrder scalar deleting destructor.
void* ClientPurchaseOrderDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424bc788;
  game::Call<void (*)(void*)>(0x14161df50)(self + 0x128);
  game::Call<void (*)(void*)>(0x141628a00)(self);
  if (flags & 1) SizedDelete(self, 0x218);
  return self;
}

// 0x1416032a0: Crypto::Prng scalar deleting destructor (two hash states
// +0x70 / +0x40, cipher +8).
void* PrngDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424b8d00;
  game::Call<void (*)(void*)>(0x141603180)(self + 0x70);
  game::Call<void (*)(void*)>(0x141603180)(self + 0x40);
  game::Call<void (*)(void*)>(0x1415fc540)(self + 8);
  if (flags & 1) SizedDelete(self, 0xB8);
  return self;
}

// 0x1415fa320: Crypto::Sha256 Final(array): size the byte array to 32 and
// finish into it through slot 4.
uint64_t Sha256FinalToArray(void* self, uint8_t* array) {
  int count = At<int>(array, 0x10);
  if (count < 0x20)
    game::Call<void (*)(void*, int)>(0x140655d70)(array, 0x20);
  else if (count > 0x20)
    At<int>(array, 0x10) = 0x20;
  int size = At<int>(array, 0x10);
  return Virtual<uint64_t>(self, 4, size ? At<void*>(array, 8) : nullptr, size);
}

// 0x141678e10: TaskManagement::SerialTaskNode: return the current (+0x40)
// and pending (+0x38) tasks to the manager's pools.
void SerialTaskNodeReleaseTasks(uint8_t* self, uint8_t* manager) {
  if (void* task = At<void*>(self, 0x40)) {
    game::Call<void (*)(void*, void*)>(0x1416509b0)(manager + 0xE118, task);
    At<void*>(self, 0x40) = nullptr;
  }
  if (void* task = At<void*>(self, 0x38)) {
    game::Call<void (*)(void*, void*)>(0x1403f2330)(manager + 0xE138, task);
    At<void*>(self, 0x38) = nullptr;
  }
}

// 0x140d19e54: undname pairNode: true if either child (right +0x10, then
// left +8) reports so through slot 1 (CFG-checked indirect calls).
bool PairNodeEither(uint8_t* self) {
  auto childSays = [](void* child) {
    void* target = (*reinterpret_cast<void***>(child))[1];
    game::Call<void (*)(void*)>(0x140d11434)(target);  // _guard_check_icall
    return reinterpret_cast<bool (*)(void*)>(target)(child);
  };
  if (childSays(At<void*>(self, 0x10))) return true;
  return childSays(At<void*>(self, 8));
}

// 0x14160e980: UramApiCreditCardTypesResponse scalar deleting destructor.
void* CreditCardTypesResponseDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0x48) = 0x1424ba340;
  game::Call<void (*)(void*)>(0x141613890)(self + 0x48);
  game::Call<void (*)(void*)>(0x14160ddd0)(self);
  if (flags & 1) SizedDelete(self, 0x68);
  return self;
}

// 0x1402ed3e0: IString-derived 0x18-byte string scalar deleting destructor.
void* PlainIStringDeletingDestructor(soeutil::IString* self, unsigned flags) {
  self->vtable = reinterpret_cast<void**>(0x14204efd0);
  soeutil::StringRelease(self);
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x1415fa1d0: Crypto::Sha256 Hash(data, length, out, outLength): Init,
// Update, Final through slots 2-4.
uint64_t Sha256Hash(void* self, const void* data, int length, void* out, int outLength) {
  Virtual(self, 2);
  Virtual(self, 3, data, length);
  return Virtual<uint64_t>(self, 4, out, outLength);
}

}  // namespace rebuild::game_small

using namespace rebuild::game_small;
REBUILD_FUNCTION(ClientInGamePurchaseOrder_ClearEntries, 0x14161f150, PurchaseOrderClearEntries);
REBUILD_FUNCTION(Crypto_Sha256_Final, 0x1415fa2e0, Sha256Final);
REBUILD_FUNCTION(StoreShortcutDefinition_ToString, 0x1416218e0, StoreShortcutToString);
REBUILD_FUNCTION(StoreBundleCategoryDefinition_IsValid, 0x1417c5eb0, StoreBundleCategoryIsValid);
REBUILD_FUNCTION(SpeedTree_CFileSystem_FileExists, 0x141e57f60, SpeedTreeFileExists);
REBUILD_FUNCTION(SpeedTree_CFileSystem_Release, 0x141e580c0, SpeedTreeReleaseFileData);
REBUILD_FUNCTION(MarketingDataSourceFlatFileLoader_Prepare, 0x1416282c0, FlatFileLoaderPrepare);
REBUILD_FUNCTION(MarketingDataSourceLoader_DeletingDestructor, 0x14162bc30, MarketingDataSourceLoaderDeletingDestructor);
REBUILD_FUNCTION(TRateTracker_Reset, 0x140312cb0, RateTrackerReset);
REBUILD_FUNCTION(MarketingDataElementInstance_RemoveAtByValue, 0x141625fa0, MarketingElementRemoveAt);
REBUILD_FUNCTION(Audio_FakeIoLayer_DeletingDestructor, 0x141501700, FakeIoLayerDeletingDestructor);
REBUILD_FUNCTION(ScheduledTaskNode_NextRunTime, 0x1416790f0, ScheduledTaskNextRun);
REBUILD_FUNCTION(GameClientInputManager_LockedUpdate, 0x1403517a0, InputManagerLockedUpdate);
REBUILD_FUNCTION(CLZInWindow_MovePos, 0x141647f90, LzInWindowMovePos);
REBUILD_FUNCTION(ScheduledTaskNode_DeletingDestructor, 0x141674810, ScheduledTaskNodeDeletingDestructor);
REBUILD_FUNCTION(EventTaskNode_DeletingDestructor, 0x1416747d0, EventTaskNodeDeletingDestructor);
REBUILD_FUNCTION(StoreBillboardPanelDefinition_IsValid, 0x1417dd940, StoreBillboardPanelIsValid);
REBUILD_FUNCTION(StorePortalCategoryDefinition_IsValid, 0x141621ca0, StorePortalCategoryIsValid);
REBUILD_FUNCTION(GameClientInputManager_LockedForward, 0x140351750, InputManagerLockedForward);
REBUILD_FUNCTION(StoreBundleDefinition_IsValid, 0x141e96510, StoreBundleIsValid);
REBUILD_FUNCTION(ClientInGamePurchaseOrder_DeletingDestructor, 0x14161e1e0, ClientPurchaseOrderDeletingDestructor);
REBUILD_FUNCTION(Crypto_Prng_DeletingDestructor, 0x1416032a0, PrngDeletingDestructor);
REBUILD_FUNCTION(Crypto_Sha256_FinalToArray, 0x1415fa320, Sha256FinalToArray);
REBUILD_FUNCTION(SerialTaskNode_ReleaseTasks, 0x141678e10, SerialTaskNodeReleaseTasks);
REBUILD_FUNCTION(pairNode_Either, 0x140d19e54, PairNodeEither);
REBUILD_FUNCTION(UramApiCreditCardTypesResponse_DeletingDestructor, 0x14160e980, CreditCardTypesResponseDeletingDestructor);
REBUILD_FUNCTION(IString_T3416a5_DeletingDestructor, 0x1402ed3e0, PlainIStringDeletingDestructor);
REBUILD_FUNCTION(Crypto_Sha256_Hash, 0x1415fa1d0, Sha256Hash);
