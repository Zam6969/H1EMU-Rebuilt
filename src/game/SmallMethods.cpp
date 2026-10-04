// One-off small vtable methods across GameCommerce, Crypto, TaskManagement,
// SpeedTree, LZMA and the input manager (classes with no shared shape).
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <intrin.h>

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

namespace {
void FreeHeapOrThread(void* data, int threadFlag) {
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    game::Call<void (*)(void*)>(0x1402fc170)(data);
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, threadFlag);
}
}  // namespace

// 0x141628220: MarketingDataSourceFlatFileLoader Reset: drop the read
// buffer (+0x10), clear the read position (+0x248) and done flag (+0x253).
void FlatFileLoaderReset(uint8_t* self) {
  auto* buffer = reinterpret_cast<soeutil::IString*>(self + 0x10);
  soeutil::StringRelease(buffer);
  buffer->data = soeutil::EmptyStringData();
  At<uint64_t>(self, 0x20) = 0;
  At<uint64_t>(self, 0x248) = 0;
  At<uint8_t>(self, 0x253) = 0;
}

// 0x1403187a0: wws::CrashReport::Private::Logger::Log(level, format, ...):
// forward to the installed callback (settings at 0x142b06950) when the
// level passes, or to the default printer when none is installed.
void CrashReportLoggerLog(void*, int level, const char* format, ...) {
  va_list args;
  va_start(args, format);
  uint8_t* settings = *reinterpret_cast<uint8_t**>(0x142b06950);
  if (settings) {
    if (level > At<int>(settings, 0xB0)) return;
    if (auto callback = At<void (*)(void*, int, const char*, va_list)>(settings, 0xB8)) {
      callback(At<void*>(settings, 0xC0), level, format, args);
      return;
    }
  }
  game::Call<void (*)(int, const char*, va_list)>(0x1403188b0)(level, format, args);
}

// 0x140318740: BufferLogger::Log: vsprintf_s into the 512-byte buffer (+8)
// and remember the level (+0x208).
void CrashReportBufferLoggerLog(uint8_t* self, int level, const char* format, ...) {
  va_list args;
  va_start(args, format);
  uint64_t options = *game::Call<uint64_t* (*)()>(0x1402f1040)();
  game::Call<int (*)(uint64_t, char*, size_t, const char*, void*, va_list)>(0x140d3d160)(options | 2, reinterpret_cast<char*>(self + 8), 0x200, format,
                                                                                          nullptr, args);
  At<int>(self, 0x208) = level;
}

// 0x141ebfca0: Deque scalar deleting destructor.
void* DequeDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425acd78;
  game::Call<void (*)(void*, int)>(0x141ec11b0)(self, At<int>(self, 0x48));
  game::Call<void (*)(void*)>(0x141ec0060)(self);
  game::Call<void (*)(void*)>(0x141ebf990)(self + 8);
  if (flags & 1) SizedDelete(self, 0x60);
  return self;
}

// 0x140312040: SoeUtil::TRateTracker Add(value): refresh, then add to the
// current bucket (128 buckets of sums +8 and counts +0x208) and the totals.
void RateTrackerAdd(uint8_t* self, int value) {
  game::Call<void (*)(void*)>(0x140311910)(self);
  At<int>(self, 8 + (At<int>(self, 0x408) % 128) * 4) += value;
  ++At<int>(self, 0x208 + (At<int>(self, 0x408) % 128) * 4);
  At<int>(self, 0x40C) += value;
  ++At<int>(self, 0x410);
}

// 0x141e5eac0: SpeedTree CBlockPool scalar deleting destructor.
void* BlockPoolDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a1578;
  game::Call<void (*)(void*)>(0x1402fc170)(At<void*>(self, 8));
  game::Call<void (*)(void*)>(0x1402fc170)(At<void*>(self, 0x10));
  for (size_t offset = 8; offset <= 0x20; offset += 8) At<uint64_t>(self, offset) = 0;
  if (flags & 1) SizedDelete(self, 0x30);
  return self;
}

// 0x140d1cb70: undname pairNode::length: cached sum of both children's
// lengths (right +0x10 first, then left +8).
int PairNodeLength(uint8_t* self) {
  if (At<int>(self, 0x18) < 0) {
    auto childLength = [](void* child) {
      void* target = (*reinterpret_cast<void***>(child))[0];
      game::Call<void (*)(void*)>(0x140d11434)(target);  // _guard_check_icall
      return reinterpret_cast<int (*)(void*)>(target)(child);
    };
    void* right = At<void*>(self, 0x10);
    void* left = At<void*>(self, 8);
    int sum = childLength(right);
    At<int>(self, 0x18) = sum + childLength(left);
  }
  return At<int>(self, 0x18);
}

// 0x141ec3080: DataManagement::FlatFileDataLoader Open(path): reset, load
// the file into +0xF0, remember the path (+0x108); warn on failure.
bool FlatFileDataLoaderOpen(uint8_t* self, const char* path) {
  game::Call<void (*)(void*)>(0x141ec3aa0)(self);
  if (!game::Call<bool (*)(const char*, void*, bool)>(0x140339860)(path, self + 0xF0, true)) {
    game::Call<void (*)(void*, const char*, ...)>(0x1402baba0)(*reinterpret_cast<void**>(0x142ad5948), reinterpret_cast<const char*>(0x1425ad380), path);
    return false;
  }
  soeutil::StringAssign(self + 0x108, path);
  return true;
}

// 0x1403b5b70: Array (64-byte elements) scalar deleting destructor: destroy
// the elements, free the storage (thread allocator alignment 8).
void* Array64DeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424bd140;
  game::Call<void (*)(void*, int)>(0x140459660)(self, At<int>(self, 0x10));
  FreeHeapOrThread(At<void*>(self, 8), 8);
  At<void*>(self, 8) = nullptr;
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x1417108d0: HashMap scalar deleting destructor.
void* HashMapDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425ad2e0;
  game::Call<void (*)(void*)>(0x1417117b0)(self);
  FreeHeapOrThread(At<void*>(self, 0x18), 0);
  if (flags & 1) SizedDelete(self, 0x20);
  return self;
}

// 0x1415fa150: Crypto::Sha256 scalar deleting destructor: wipe and free the
// 0x68-byte context, drop to the HashBase vtable.
void* Sha256DeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424b32f8;
  Memset(At<void*>(self, 8), 0, 0x68);
  At<bool>(self, 0x10) = false;
  SizedDelete(At<void*>(self, 8), 0x68);
  At<void*>(self, 8) = nullptr;
  At<uint64_t>(self, 0) = 0x1424b32a8;
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x14160ec60: UramApiIsSteamCustomerResponse scalar deleting destructor.
void* IsSteamCustomerResponseDeletingDestructor(uint8_t* self, unsigned flags) {
  auto* text = reinterpret_cast<soeutil::IString*>(self + 0x50);
  text->vtable = soeutil::IStringVtable();
  soeutil::StringRelease(text);
  game::Call<void (*)(void*)>(0x14160ddd0)(self);
  if (flags & 1) SizedDelete(self, 0x68);
  return self;
}

// 0x14164d4a0: HashListSet scalar deleting destructor.
void* HashListSetDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424c1450;
  if (At<void*>(self, 0x10)) {
    do {
      if (void* head = At<void*>(self, 0x10)) game::Call<void (*)(void*, void*)>(0x141655640)(self, head);
    } while (At<void*>(self, 0x10));
  }
  game::Call<void (*)(void*)>(0x14164c890)(self);
  if (flags & 1) SizedDelete(self, 0x38);
  return self;
}

// 0x14161e360: ClientInGamePurchaseOrder AddQuantity(itemId, offerId, n):
// bump the entry in the 4-bucket hash (+0x128) whose folded key matches
// (only the fold is compared), else insert a new entry.
void PurchaseOrderAddQuantity(uint8_t* self, const int* first, const int* second, int quantity) {
  uint8_t* map = self + 0x128;
  int64_t key = (static_cast<int64_t>(*first) << 32) | static_cast<int64_t>(*second);
  int hash = static_cast<int>(key >> 32) ^ static_cast<int>(key);
  for (uint8_t* node = At<uint8_t*>(map, 0x28 + (hash & 3) * 8); node; node = At<uint8_t*>(node, 0x78)) {
    if (At<int>(node, 0x70) == hash) {
      At<int>(node, 0x10) += quantity;
      return;
    }
  }
  game::Call<void (*)(void*, int*, const int*, const int*, int*)>(0x14161dbc0)(map, &hash, first, second, &quantity);
}

// 0x1416678f0: SoeUtil::InputManager Push(event): append a 64-byte event to
// the 512-entry ring (+8), flushing through slot 11 when full.
void InputManagerPushEvent(uint8_t* self, const uint8_t* event) {
  if (At<int>(self, 0x8008) == 0x200) Virtual(self, 11);
  int index = (At<int>(self, 0x800C) + At<int>(self, 0x8008)) % 512;
  for (int i = 0; i < 64; ++i) self[8 + index * 64 + i] = event[i];
  ++At<int>(self, 0x8008);
}

// 0x1403b5af0: Array (destroyed elements, 8-aligned) scalar deleting
// destructor.
void* ArrayT095a01DeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424b0578;
  game::Call<void (*)(void*, int)>(0x1404595e0)(self, At<int>(self, 0x10));
  FreeHeapOrThread(At<void*>(self, 8), 8);
  At<void*>(self, 8) = nullptr;
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x1415fa230: Crypto::Sha256 Hash(data, length, array): size the array to
// 32 bytes and hash into it through slot 0.
uint64_t Sha256HashToArray(void* self, const void* data, int length, uint8_t* array) {
  int count = At<int>(array, 0x10);
  if (count < 0x20)
    game::Call<void (*)(void*, int)>(0x140655d70)(array, 0x20);
  else if (count > 0x20)
    At<int>(array, 0x10) = 0x20;
  int size = At<int>(array, 0x10);
  return Virtual<uint64_t>(self, 0, data, length, size ? At<void*>(array, 8) : nullptr, size);
}

// 0x141e96310: StoreBundleDefinition scalar deleting destructor (class
// operator delete).
void* StoreBundleDefinitionDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425aa3d0;
  game::Call<void (*)(void*)>(0x14161a240)(self);
  if (flags & 1) {
    if (flags & 4)
      game::Call<void (*)(void*, size_t)>(0x1402ec800)(self, 0x1D0);
    else if (*reinterpret_cast<void**>(0x143e09638) != nullptr)
      game::Call<void (*)(void*, int)>(0x14032f980)(self, 0);
    else
      game::Call<void (*)(void*)>(0x1402fc170)(self);
  }
  return self;
}

// 0x140340bd0: RefArrayPooled Free(block, flags): heap blocks (flags & 1)
// go back to the allocator, pooled ones to the array's pool or the lazily
// created global pool (0x142b06e08).
void RefArrayPooledFree(uint8_t* self, void* block, unsigned flags) {
  if (flags & 1) {
    FreeHeapOrThread(block, 1);
    return;
  }
  void* pool = At<void*>(self, 0x28);
  if (!pool) {
    if (!*reinterpret_cast<bool*>(0x142b06e00)) {
      game::Call<void (*)()>(0x14033b300)();
      *reinterpret_cast<bool*>(0x142b06e00) = true;
    }
    pool = *reinterpret_cast<void**>(0x142b06e08);
  }
  game::Call<void (*)(void*, void*, unsigned)>(0x140340c60)(pool, block, flags);
}

// 0x140d1b664: undname pairNode::getString(buffer, end): left child then,
// if room remains, the right child (slot 2, CFG-checked).
char* PairNodeGetString(uint8_t* self, char* buffer, char* end) {
  auto childString = [](void* child, char* at, char* limit) {
    void* target = (*reinterpret_cast<void***>(child))[2];
    game::Call<void (*)(void*)>(0x140d11434)(target);  // _guard_check_icall
    return reinterpret_cast<char* (*)(void*, char*, char*)>(target)(child, at, limit);
  };
  char* at = childString(At<void*>(self, 8), buffer, end);
  if (at < end) at = childString(At<void*>(self, 0x10), at, end);
  return at;
}

// 0x141679060: TaskManagement::ScheduledTaskNode TimeRemaining(now): 0 for
// a pending/repeating task with no delay or an unset start time, else
// start (+0x30 -> +0x100) + delay - now.
int64_t ScheduledTaskTimeRemaining(void** self, const int64_t* now) {
  using DelayFn = int* (*)(void*, int64_t*);
  int state = At<int>(self, 0x48);
  if (((state - 1) & ~2) == 0) {
    int64_t scratch;
    if (*reinterpret_cast<DelayFn>(reinterpret_cast<void**>(*self)[1])(self, &scratch) == *reinterpret_cast<int*>(0x143dcb1dc)) return 0;
    if (At<int64_t>(At<void*>(self, 0x30), 0x100) == *reinterpret_cast<int64_t*>(0x143dcb1c8)) return 0;
  }
  int64_t scratch;
  int delay = *reinterpret_cast<DelayFn>(reinterpret_cast<void**>(*self)[1])(self, &scratch);
  return At<int64_t>(At<void*>(self, 0x30), 0x100) + delay - *now;
}

// 0x141e5ec00: SpeedTree::CCore scalar deleting destructor: release the
// file buffer (+0x118), destroy the three 0x2A8-byte LOD entries (+0x160).
void* SpeedTreeCoreDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a1598;
  if (At<void*>(self, 0x118)) game::Call<void (*)(void*)>(0x141e57a10)(self + 0x118);
  game::Call<void (*)(void*, size_t, size_t, void*)>(0x140d10830)(self + 0x160, 0x2A8, 3, reinterpret_cast<void*>(0x141803c20));  // eh vector dtor
  At<uint64_t>(self, 8) = 0x1425a0f18;
  if (flags & 1) SizedDelete(self, 0x118C);
  return self;
}

// 0x14167fa60: SoeGems::Event scalar deleting destructor (embedded hash
// list at +0x10 with its bucket member at +0x238).
void* SoeGemsEventDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424c6ee0;
  At<uint64_t>(self, 0x10) = 0x1424c6ea8;
  game::Call<void (*)(void*)>(0x141681a60)(self + 0x10);
  game::Call<void (*)(void*)>(0x14167f780)(self + 0x238);
  At<uint64_t>(self, 0x10) = 0x1424c6e70;
  game::Call<void (*)(void*)>(0x141681a60)(self + 0x10);
  if (flags & 1) SizedDelete(self, 0x440);
  return self;
}

// 0x14160ed20: UramApiPaymentSourceResponse scalar deleting destructor.
void* PaymentSourceResponseDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0x60) = 0x1424ba000;
  game::Call<void (*)(void*)>(0x141613810)(self + 0x60);
  auto* text = reinterpret_cast<soeutil::IString*>(self + 0x48);
  text->vtable = soeutil::IStringVtable();
  soeutil::StringRelease(text);
  game::Call<void (*)(void*)>(0x14160ddd0)(self);
  if (flags & 1) SizedDelete(self, 0x88);
  return self;
}

// 0x141e57ed0: SpeedTree::CFileSystem CompareFileTimes(a, b): 0 same,
// 1 a older, 2 a newer, 3 either stat failed.
int SpeedTreeCompareFileTimes(void*, const char* first, const char* second) {
  struct Stat64i32 {
    uint8_t head[0x18];
    int64_t accessTime;
    int64_t modifyTime;
    int64_t changeTime;
  };
  static_assert(offsetof(Stat64i32, modifyTime) == 0x20 && sizeof(Stat64i32) == 0x30);
  using StatFn = int (*)(const char*, Stat64i32*);
  Stat64i32 a, b;
  int resultA = game::Call<StatFn>(0x140d42078)(first, &a);
  int resultB = game::Call<StatFn>(0x140d42078)(second, &b);
  if (resultA != 0 || resultB != 0) return 3;
  if (a.modifyTime < b.modifyTime) return resultA + 1;
  return a.modifyTime > b.modifyTime ? 2 : resultB;
}

// 0x14161d5b0: StoreBundleGroupDefinition scalar deleting destructor.
void* StoreBundleGroupDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1424bc4f8;
  At<uint64_t>(self, 0x58) = 0x1424bc4c0;
  game::Call<void (*)(void*)>(0x140831ba0)(self + 0x58);
  game::Call<void (*)(void*)>(0x141621640)(self + 0x18);
  At<uint64_t>(self, 0) = 0x1424bc080;
  if (flags & 1) {
    if (flags & 4)
      game::Call<void (*)(void*, size_t)>(0x1402ec800)(self, 0xC0);
    else if (*reinterpret_cast<void**>(0x143e09638) != nullptr)
      game::Call<void (*)(void*, int)>(0x14032f980)(self, 0);
    else
      game::Call<void (*)(void*)>(0x1402fc170)(self);
  }
  return self;
}

// 0x1416278a0: MarketingDataUpdater Finish(.., .., success): mark done,
// record the elapsed milliseconds (+0x36C) and log them.
void MarketingDataUpdaterFinish(uint8_t* self, void*, void*, bool success) {
  auto elapsedSinceStart = [&] {
    int64_t start = At<int64_t>(self, 0x398);
    int64_t now;
    int64_t elapsed = *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now) - start;
    return static_cast<int>(elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : elapsed);
  };
  At<int>(self, 0x388) = 2;
  At<bool>(self, 0x3A0) = success;
  At<int>(self, 0x36C) = elapsedSinceStart();
  int elapsed = elapsedSinceStart();
  game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(At<void*>(self, 0xB0), reinterpret_cast<const char*>(0x1424bd700), At<uint8_t>(self, 0x3A0),
                                                            elapsed);
}

// 0x141e96940: StoreBundleCategoryMapEntryDefinition ToString. Builds (and
// drops) an unused empty StringFixed<256> scratch, as the original does.
const char* CategoryMapEntryToString(uint8_t* self, soeutil::IString* out) {
  soeutil::IString scratch{reinterpret_cast<void**>(0x142049e08), soeutil::EmptyStringData(), 0, 0};
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1425aa5f8), At<int>(self, 8), At<int>(self, 0xC), At<int>(self, 0x10));
  const char* text = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&scratch);
  return text;
}

// 0x141ec3230: DataManagement::FlatFileDataLoader Load(sink, path): open
// and parse, reporting errors (slot 4) and completion (slot 3) to the sink.
bool FlatFileDataLoaderLoad(uint8_t* self, void* sink, const char* path) {
  soeutil::StringAssign(self + 0x70, path);
  At<uint64_t>(self, 0xD0) = At<uint64_t>(self, 0xF8);
  bool ok = false;
  if (game::Call<bool (*)(void*, void*, const char*)>(0x141ec32d0)(self, sink, path))
    ok = game::Call<bool (*)(void*, void*)>(0x141ec35f0)(self, sink);
  else
    Virtual(sink, 4, static_cast<void*>(self), path, reinterpret_cast<const char*>(0x1425ad398));
  Virtual(sink, 3, static_cast<void*>(self), path, ok);
  return ok;
}

// 0x141624f90: GameCommerce::MarketingDataSource IsConsistent: every
// element group in the source chain (+0x20, next +0x128) must report a
// single type id (element slot 2) across its entries.
bool MarketingDataSourceIsConsistent(uint8_t* self) {
  if (At<int>(self, 0x30) <= 0) return false;
  for (uint8_t* group = At<uint8_t*>(self, 0x20); group; group = At<uint8_t*>(group, 0x128)) {
    int seen = -1;
    size_t index = 0;
    for (uint8_t* entry = At<uint8_t*>(At<uint8_t*>(group, 0), 0x118); entry; entry = At<uint8_t*>(entry, 0x48)) {
      void* element = At<void**>(group, 0x10)[index++];
      int type = Virtual<int>(element, 2);
      if (type != seen) {
        if (seen != -1) return false;
      }
      seen = type;
    }
  }
  return true;
}

// 0x141e96880: StoreBundleCategoryDefinition ToString (name from the
// member at +0x10, slot 1, rendered into an empty StringFixed<256>).
const char* StoreBundleCategoryToString(uint8_t* self, soeutil::IString* out) {
  soeutil::IString scratch{reinterpret_cast<void**>(0x142049e08), soeutil::EmptyStringData(), 0, 0};
  const char* name = Virtual<const char*>(self + 0x10, 1, &scratch);
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1425aa638), At<int>(self, 8), At<int>(self, 0xC), name);
  const char* text = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&scratch);
  return text;
}

// 0x141ec2fb0: DataManagement::FlatFileLineData Find(key, found): look up
// the column value by name in the map at +8.
void* FlatFileLineDataFind(uint8_t* self, const char* key, bool* found) {
  soeutil::IString name{soeutil::IStringVtable(), soeutil::EmptyStringData(), 0, 0};
  soeutil::StringAssign(&name, key);
  uint8_t* entry = game::Call<uint8_t* (*)(void*, soeutil::IString*)>(0x141ec2eb0)(self + 8, &name);
  name.vtable = soeutil::IStringVtable();
  soeutil::StringRelease(&name);
  if (entry) {
    if (found) *found = true;
    return At<void*>(entry, 8);
  }
  if (found) *found = false;
  return nullptr;
}

// 0x14166f3b0: TaskManagement::TaskManager TaskCompleted(.., task): count
// down the outstanding-frame tasks, stamp and queue the task, and resume
// its owner if it is registered and not cancelled.
void TaskManagerTaskCompleted(uint8_t* self, void*, uint8_t* task) {
  int outstanding = At<int>(self, 0x60B20);
  if (outstanding > 0 && At<int>(task, 0x138) != At<int>(At<void*>(self, 0x60B10), 0xE158)) {
    At<int>(self, 0x60B20) = outstanding - 1;
    if (outstanding - 1 == 0) game::Call<void (*)(void*)>(0x14166e380)(self);
  }
  At<int>(task, 0x138) = *reinterpret_cast<int*>(0x143dcb0d8);
  game::Call<void (*)(void*, void*)>(0x141676750)(At<void*>(self, 0x60B10), task);
  int id = At<int>(At<void*>(task, 0x120), 8);
  for (uint8_t* node = At<uint8_t*>(self, 0x8C8 + (id & 0x7FFF) * 8); node; node = At<uint8_t*>(node, 0x80)) {
    if (At<int>(node, 0x78) == id) {
      if (At<int>(task, 0xB8) == 0) game::Call<void (*)(void*, void*)>(0x14166e270)(self, task);
      return;
    }
  }
}

// 0x141678d50: TaskManagement::ScheduledTaskNode Release(manager): pull the
// node's heap entry (+0x50) out of the manager's timer heap (+0xA0D0) by
// swapping in the last entry, then return its tasks to the pools.
void ScheduledTaskNodeRelease(uint8_t* self, uint8_t* manager) {
  uint8_t* entry = self ? self + 0x50 : nullptr;
  void* slot = At<int>(entry, 8) < 0 ? nullptr : entry + 0x10;
  if (slot && At<int>(entry, 8) != -1) {
    int64_t index = At<int>(entry, 8);
    uint8_t* heap = manager + 0xA0D0;
    int count = At<int>(heap, 0x10) - 1;
    uint8_t** data = At<uint8_t**>(heap, 8);
    uint8_t* last = data[count];
    At<int>(heap, 0x10) = count;
    if (entry != last) {
      data[index] = last;
      At<int>(last, 8) = static_cast<int>(index);
      game::Call<void (*)(void*, void*, int, int64_t)>(0x141676910)(heap, last, count, index);  // re-sift
    }
    At<int>(entry, 8) = -1;
  }
  SerialTaskNodeReleaseTasks(self, manager);
}

// 0x141672c80: async job completion: finish (0x141669a40), report the
// result to the listener (+0x130, slot 2) with a timestamp, then drop the
// listener's intrusive reference (count block at listener +8).
void AsyncJobComplete(uint8_t* self) {
  game::Call<void (*)(void*)>(0x141669a40)(self);
  uint64_t second = At<uint64_t>(self, 0x110);
  int level = At<int>(self, 0x70);
  uint64_t first = At<uint64_t>(self, 0x108);
  void* listener = At<void*>(self, 0x130);
  void** vtable = *static_cast<void***>(listener);
  uint64_t stamp;
  void* now = game::Call<void* (*)(uint64_t*)>(0x14032fde0)(&stamp);
  reinterpret_cast<void (*)(void*, void*, void*, int, uint64_t*, uint64_t*, void*)>(vtable[2])(At<void*>(self, 0x130), At<void*>(self, 0x120), self + 0x138,
                                                                                           level, &first, &second, now);
  uint8_t* object = At<uint8_t*>(self, 0x130);
  auto* counts = At<volatile long*>(object, 8);
  bool lastStrong = _InterlockedDecrement(&counts[0]) == 0;
  if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts) SizedDelete(const_cast<long*>(counts), 0x10);
  if (lastStrong) Virtual(object, 1);
  At<void*>(self, 0x130) = nullptr;
}

// 0x140351480: GameClientInputManager ShutdownDirectInput: unacquire and
// release every device, release the DirectInput object, free the wrapper.
bool InputManagerShutdownDirectInput(uint8_t* self) {
  uint8_t* direct = At<uint8_t*>(self, 0x8218);
  if (!direct) return true;
  for (uint8_t* node = At<uint8_t*>(direct, 0x10); node; node = At<uint8_t*>(node, 0x140)) {
    if (void* device = At<void*>(node, 0x28)) {
      if (Virtual<long>(device, 8) < 0) return false;  // Unacquire
      Virtual<unsigned long>(At<void*>(node, 0x28), 2);  // Release
      At<void*>(node, 0x28) = nullptr;
    }
  }
  if (void* input = At<void*>(At<void*>(self, 0x8218), 0)) Virtual<unsigned long>(input, 2);
  At<void*>(At<void*>(self, 0x8218), 0) = nullptr;
  if (uint8_t* wrapper = At<uint8_t*>(self, 0x8218)) {
    At<uint64_t>(wrapper, 8) = 0x142052f98;
    game::Call<void (*)(void*)>(0x140351290)(wrapper + 8);
    SizedDelete(wrapper, 0x28);
  }
  At<void*>(self, 0x8218) = nullptr;
  return true;
}

// 0x141807d50: SpeedTree CArray<0x88-byte T> scalar deleting destructor.
void* SpeedTreeArray88DeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a1f98;
  if (At<bool>(self, 0x28)) {
    At<uint64_t>(self, 0x10) = 0;
    for (uint64_t i = 0; i < At<uint64_t>(self, 0x18); ++i) game::Call<void (*)(void*, int)>(0x141808c60)(At<uint8_t*>(self, 8) + i * 0x88, 0);
    At<uint64_t>(self, 0x18) = 0;
    At<void*>(self, 8) = nullptr;
    At<bool>(self, 0x28) = false;
  }
  if (!At<bool>(self, 0x28)) {
    void* data = At<void*>(self, 8);
    game::Call<void (*)(void**)>(0x1417f9f70)(&data);
    At<void*>(self, 8) = nullptr;
    At<uint64_t>(self, 0x18) = 0;
    At<uint64_t>(self, 0x10) = 0;
  }
  if (flags & 1) SizedDelete(self, 0x2C);
  return self;
}

// 0x1418065b0: SpeedTree CArray (0x2C bytes) vector deleting destructor.
void* SpeedTreeArrayVectorDeletingDestructor(uint8_t* self, unsigned flags) {
  if (flags & 2) {
    uint64_t count = At<uint64_t>(self - 8, 0);
    game::Call<void (*)(void*, size_t, size_t, void*)>(0x140d10830)(self, 0x2C, count, reinterpret_cast<void*>(0x141800e60));
    if (flags & 1) game::Call<void (*)(void*, size_t)>(0x140d10924)(self - 8, At<uint64_t>(self - 8, 0) * 0x2C + 8);
    return self - 8;
  }
  At<uint64_t>(self, 0) = 0x1425a1f48;
  if (At<bool>(self, 0x28)) {
    At<uint64_t>(self, 0x10) = 0;
    At<uint64_t>(self, 0x18) = 0;
    At<void*>(self, 8) = nullptr;
    At<bool>(self, 0x28) = false;
  }
  void* data = At<void*>(self, 8);
  game::Call<void (*)(void**)>(0x1417f9870)(&data);
  At<void*>(self, 8) = nullptr;
  At<uint64_t>(self, 0x18) = 0;
  At<uint64_t>(self, 0x10) = 0;
  if (flags & 1) SizedDelete(self, 0x2C);
  return self;
}

// 0x141603930: Crypto::Prng Seed(seed array): needs at least the cipher's
// key size (slot 4); keys the cipher (+8) and the counter (+0x40) from the
// first 16 bytes.
bool PrngSeed(uint8_t* self, uint8_t* seed) {
  int available = At<int>(seed, 0x10);
  if (available < Virtual<int>(self, 4)) return false;
  alignas(16) uint8_t key[0x60];
  game::Call<void (*)(void*, const void*, int)>(0x1415f9a40)(key, At<int>(seed, 0x10) ? At<void*>(seed, 8) : nullptr, 0x10);
  if (!game::Call<bool (*)(void*)>(0x1415f9ea0)(key)) {
    game::Call<void (*)(void*)>(0x1415f9be0)(key);
    return false;
  }
  game::Call<void (*)(void*)>(0x1416038d0)(self);
  game::Call<void (*)(void*, int, void*, int, int)>(0x141435310)(self + 0x40, 0, seed, 0x10, 0x10);
  bool ok = Virtual<bool>(self + 8, 1, static_cast<void*>(key), 0);
  At<bool>(self, 0xB0) = ok;
  game::Call<void (*)(void*)>(0x1415f9be0)(key);
  return ok;
}

// 0x141e5eb30: SpeedTree CMap scalar deleting destructor: free the tree
// from the root (+8, an offset into the block pool at +0x20), push the root
// slot onto the free list (+0x28 / +0x38), then tear down the pool.
void* SpeedTreeMapDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a1588;
  if (At<uint8_t*>(self, 8)) {
    game::Call<void (*)(void*, void*)>(0x141e5f160)(At<uint8_t*>(self, 8) + At<uint64_t>(self, 0x20), self);
    uint8_t* root = At<uint8_t*>(self, 8) ? At<uint8_t*>(self, 8) + At<uint64_t>(self, 0x20) : nullptr;
    At<uint64_t>(root, 0) = 0x1425a0f18;
    At<uint64_t*>(self, 0x28)[At<uint64_t>(self, 0x38)] = At<uint64_t>(self, 8);
    ++At<uint64_t>(self, 0x38);
    At<uint64_t>(self, 8) = 0;
  }
  At<uint64_t>(self, 0x10) = 0;
  At<uint64_t>(self, 0x18) = 0x1425a1578;
  game::Call<void (*)(void*)>(0x1402fc170)(At<void*>(self, 0x20));
  game::Call<void (*)(void*)>(0x1402fc170)(At<void*>(self, 0x28));
  for (size_t offset = 0x20; offset <= 0x38; offset += 8) At<uint64_t>(self, offset) = 0;
  if (flags & 1) SizedDelete(self, 0x50);
  return self;
}

// 0x1418071c0: SpeedTree CArray (custom-allocated) scalar deleting
// destructor: return the block (header at data - 8) to the SpeedTree
// allocator, updating the global byte counter.
void* SpeedTreeAllocatedArrayDeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a1ff8;
  if (At<bool>(self, 0x28)) {
    At<uint64_t>(self, 0x10) = 0;
    At<uint64_t>(self, 0x18) = 0;
    At<void*>(self, 8) = nullptr;
    At<bool>(self, 0x28) = false;
  }
  if (uint8_t* data = At<uint8_t*>(self, 8)) {
    if (uint8_t* header = data - 8) {
      int64_t* allocated = game::Call<int64_t* (*)()>(0x141e58110)();
      *allocated += -8 - At<int64_t>(header, 0);
      if (*game::Call<void** (*)()>(0x141e58100)())
        Virtual(*game::Call<void** (*)()>(0x141e58100)(), 2, static_cast<void*>(header));
      else
        game::Call<void (*)(void*)>(0x140d42428)(header);
    }
  }
  At<void*>(self, 8) = nullptr;
  At<uint64_t>(self, 0x18) = 0;
  At<uint64_t>(self, 0x10) = 0;
  if (flags & 1) SizedDelete(self, 0x2C);
  return self;
}

// 0x14034f0c0: GameClientInputManager PumpMessages: unless disabled
// (+0x8010), drain the thread's message queue; keyboard and left-button
// messages, and anything the UI hook (0x140cbc182) consumed, also go to
// the input handler (+0x8210, slot 1). Win32 calls go through the IAT.
void InputManagerPumpMessages(uint8_t* self) {
  if (At<bool>(self, 0x8010)) return;
  struct Message {
    void* window;
    uint32_t message;
    uint64_t wParam;
    int64_t lParam;
    uint8_t rest[0x10];
  };
  static_assert(offsetof(Message, message) == 8 && offsetof(Message, lParam) == 0x18);
  using PeekFn = int(__stdcall*)(Message*, void*, unsigned, unsigned, unsigned);
  using MessageFn = int64_t(__stdcall*)(Message*);
  Message message;
  while ((*reinterpret_cast<PeekFn*>(0x1440a04e8))(&message, nullptr, 0, 0, 1)) {
    bool consumed = game::Call<int (*)(void*, uint32_t, uint64_t, int64_t)>(0x140cbc182)(nullptr, message.message, message.wParam, message.lParam) != 0;
    uint32_t id = message.message;
    if (id - 0x100 <= 1 || consumed || id - 0x201 <= 1) {
      if (void* handler = At<void*>(self, 0x8210)) Virtual(handler, 1, id, message.wParam, message.lParam, true);
    }
    (*reinterpret_cast<MessageFn*>(0x1440a04d0))(&message);  // TranslateMessage
    (*reinterpret_cast<MessageFn*>(0x1440a04d8))(&message);  // DispatchMessage
  }
}

// 0x14163fdd0: Login::GameServerData Assign(other, flagMask): copy the
// masked flag bits (+0x11C8) and the server's names / ids / addresses.
uint8_t* GameServerDataAssign(uint8_t* self, uint8_t* other, uint8_t mask) {
  auto copyString = [&](size_t offset) {
    soeutil::StringAssignString(reinterpret_cast<soeutil::IString*>(self + offset), reinterpret_cast<soeutil::IString*>(other + offset));
  };
  At<uint8_t>(self, 0x11C8) = (At<uint8_t>(other, 0x11C8) & mask) | (At<uint8_t>(self, 0x11C8) & static_cast<uint8_t>(~mask));
  copyString(0x18);
  At<int>(self, 0x78) = At<int>(other, 0x78);
  copyString(0x80);
  At<int>(self, 0x1A0) = At<int>(other, 0x1A0);
  At<int>(self, 0x1A4) = At<int>(other, 0x1A4);
  copyString(0x1A8);
  At<int>(self, 0x11CC) = At<int>(other, 0x11CC);
  copyString(0x11D0);
  copyString(0x1A8);
  copyString(0x11E8);
  return self;
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
REBUILD_FUNCTION(MarketingDataSourceFlatFileLoader_Reset, 0x141628220, FlatFileLoaderReset);
REBUILD_FUNCTION(CrashReport_Logger_Log, 0x1403187a0, CrashReportLoggerLog);
REBUILD_FUNCTION(CrashReport_BufferLogger_Log, 0x140318740, CrashReportBufferLoggerLog);
REBUILD_FUNCTION(Deque_T65e4fa_DeletingDestructor, 0x141ebfca0, DequeDeletingDestructor);
REBUILD_FUNCTION(TRateTracker_Add, 0x140312040, RateTrackerAdd);
REBUILD_FUNCTION(SpeedTree_CBlockPool_DeletingDestructor, 0x141e5eac0, BlockPoolDeletingDestructor);
REBUILD_FUNCTION(pairNode_Length, 0x140d1cb70, PairNodeLength);
REBUILD_FUNCTION(FlatFileDataLoader_Open, 0x141ec3080, FlatFileDataLoaderOpen);
REBUILD_FUNCTION(Array_Ta2a1d0_DeletingDestructor, 0x1403b5b70, Array64DeletingDestructor);
REBUILD_FUNCTION(HashMap_Tf9454d_DeletingDestructor, 0x1417108d0, HashMapDeletingDestructor);
REBUILD_FUNCTION(Crypto_Sha256_DeletingDestructor, 0x1415fa150, Sha256DeletingDestructor);
REBUILD_FUNCTION(UramApiIsSteamCustomerResponse_DeletingDestructor, 0x14160ec60, IsSteamCustomerResponseDeletingDestructor);
REBUILD_FUNCTION(HashListSet_T961379_DeletingDestructor, 0x14164d4a0, HashListSetDeletingDestructor);
REBUILD_FUNCTION(ClientInGamePurchaseOrder_AddQuantity, 0x14161e360, PurchaseOrderAddQuantity);
REBUILD_FUNCTION(SoeUtil_InputManager_PushEvent, 0x1416678f0, InputManagerPushEvent);
REBUILD_FUNCTION(Array_T095a01_DeletingDestructor, 0x1403b5af0, ArrayT095a01DeletingDestructor);
REBUILD_FUNCTION(Crypto_Sha256_HashToArray, 0x1415fa230, Sha256HashToArray);
REBUILD_FUNCTION(StoreBundleDefinition_DeletingDestructor, 0x141e96310, StoreBundleDefinitionDeletingDestructor);
REBUILD_FUNCTION(RefArrayPooled_Free, 0x140340bd0, RefArrayPooledFree);
REBUILD_FUNCTION(pairNode_GetString, 0x140d1b664, PairNodeGetString);
REBUILD_FUNCTION(ScheduledTaskNode_TimeRemaining, 0x141679060, ScheduledTaskTimeRemaining);
REBUILD_FUNCTION(SpeedTree_CCore_DeletingDestructor, 0x141e5ec00, SpeedTreeCoreDeletingDestructor);
REBUILD_FUNCTION(SoeGems_Event_DeletingDestructor, 0x14167fa60, SoeGemsEventDeletingDestructor);
REBUILD_FUNCTION(UramApiPaymentSourceResponse_DeletingDestructor, 0x14160ed20, PaymentSourceResponseDeletingDestructor);
REBUILD_FUNCTION(SpeedTree_CFileSystem_CompareFileTimes, 0x141e57ed0, SpeedTreeCompareFileTimes);
REBUILD_FUNCTION(StoreBundleGroupDefinition_DeletingDestructor, 0x14161d5b0, StoreBundleGroupDeletingDestructor);
REBUILD_FUNCTION(MarketingDataUpdater_Finish, 0x1416278a0, MarketingDataUpdaterFinish);
REBUILD_FUNCTION(StoreBundleCategoryMapEntryDefinition_ToString, 0x141e96940, CategoryMapEntryToString);
REBUILD_FUNCTION(FlatFileDataLoader_Load, 0x141ec3230, FlatFileDataLoaderLoad);
REBUILD_FUNCTION(MarketingDataSource_IsConsistent, 0x141624f90, MarketingDataSourceIsConsistent);
REBUILD_FUNCTION(StoreBundleCategoryDefinition_ToString, 0x141e96880, StoreBundleCategoryToString);
REBUILD_FUNCTION(FlatFileLineData_Find, 0x141ec2fb0, FlatFileLineDataFind);
REBUILD_FUNCTION(TaskManager_TaskCompleted, 0x14166f3b0, TaskManagerTaskCompleted);
REBUILD_FUNCTION(ScheduledTaskNode_Release, 0x141678d50, ScheduledTaskNodeRelease);
REBUILD_FUNCTION(RefObjectPool_Tc683aa_Complete, 0x141672c80, AsyncJobComplete);
REBUILD_FUNCTION(GameClientInputManager_ShutdownDirectInput, 0x140351480, InputManagerShutdownDirectInput);
REBUILD_FUNCTION(SpeedTree_CArray88_DeletingDestructor, 0x141807d50, SpeedTreeArray88DeletingDestructor);
REBUILD_FUNCTION(SpeedTree_CArray_VectorDeletingDestructor, 0x1418065b0, SpeedTreeArrayVectorDeletingDestructor);
REBUILD_FUNCTION(Crypto_Prng_Seed, 0x141603930, PrngSeed);
REBUILD_FUNCTION(SpeedTree_CMap_DeletingDestructor, 0x141e5eb30, SpeedTreeMapDeletingDestructor);
REBUILD_FUNCTION(SpeedTree_CArrayAllocated_DeletingDestructor, 0x1418071c0, SpeedTreeAllocatedArrayDeletingDestructor);
REBUILD_FUNCTION(GameClientInputManager_PumpMessages, 0x14034f0c0, InputManagerPumpMessages);
REBUILD_FUNCTION(Login_GameServerData_Assign, 0x14163fdd0, GameServerDataAssign);
