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
  soeutil::StringFixed<256> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x142049e08));
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
  soeutil::StringFixed<256> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x142049e08));
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

// 0x141e5e9a0: SpeedTree CArray<0x410-byte polymorphic T> scalar deleting
// destructor (elements destroyed through slot 0; storage freed through the
// array's vector deleting destructor when it has a count header).
void* SpeedTreeArray410DeletingDestructor(uint8_t* self, unsigned flags) {
  At<uint64_t>(self, 0) = 0x1425a16b0;
  if (At<bool>(self, 0x28)) {
    At<uint64_t>(self, 0x10) = 0;
    for (uint64_t i = 0; i < At<uint64_t>(self, 0x18); ++i) Virtual(At<uint8_t*>(self, 8) + i * 0x410, 0, 0);
    At<uint64_t>(self, 0x18) = 0;
    At<void*>(self, 8) = nullptr;
    At<bool>(self, 0x28) = false;
  }
  if (!At<bool>(self, 0x28)) {
    if (uint8_t* data = At<uint8_t*>(self, 8)) {
      if (At<uint64_t>(data - 8, 0) != 0)
        Virtual(data, 0, 3);
      else
        game::Call<void (*)(void*)>(0x1402fc170)(data - 8);
    }
    At<void*>(self, 8) = nullptr;
    At<uint64_t>(self, 0x18) = 0;
    At<uint64_t>(self, 0x10) = 0;
  }
  if (flags & 1) SizedDelete(self, 0x2C);
  return self;
}

// 0x141e96c70: StoreBillboardPanelDefinition ToString.
const char* StoreBillboardPanelToString(uint8_t* self, soeutil::IString* out) {
  soeutil::StringFixed<512> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x14204aea0));
  const char* image = Virtual<const char*>(self + 0x48, 1, static_cast<void*>(&scratch));
  const char* name = Virtual<const char*>(self + 0x10, 1, static_cast<void*>(&scratch));
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1425aa730), At<int>(self, 8), At<int>(self, 0xC), name, image, At<int>(self, 0x60));
  const char* text = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x14204ae80);
  soeutil::StringRelease(&scratch);
  return text;
}

// 0x141621bb0: StorePortalCategoryDefinition ToString.
const char* StorePortalCategoryToString(uint8_t* self, soeutil::IString* out) {
  soeutil::StringFixed<256> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x142049e08));
  const char* name = Virtual<const char*>(self + 0x18, 1, static_cast<void*>(&scratch));
  const char* image = Virtual<const char*>(self + 0x50, 1, static_cast<void*>(&scratch));
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bcc80), At<int>(self, 8), At<int>(self, 0xC), At<int>(self, 0x10),
                                    At<int>(self, 0x14), image, name);
  const char* text = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&scratch);
  return text;
}

// 0x141e57fb0: SpeedTree::CFileSystem::LoadFile(path, kind): read the whole
// file, from the pooled cache (kind 0) or a fresh allocation, logging the
// "loading" line; returns the buffer or null.
void* SpeedTreeLoadFile(void*, const char* path, int kind) {
  if (!path) return nullptr;
  void* file = game::Call<void* (*)(const char*)>(0x141e5f740)(path);  // fopen wrapper
  if (!file) return nullptr;
  alignas(8) uint8_t message[0x10];
  game::Call<void (*)(void*, const char*, const char*)>(0x1418158f0)(message, reinterpret_cast<const char*>(0x1425a0f30), path);
  alignas(8) uint8_t sizeOut[0x100];
  if (kind == 0) {
    int slot = 0;
    void* buffer = game::Call<void* (*)(void*, void*, int*, int)>(0x141e62940)(file, sizeOut, &slot, 0);
    if (!buffer) return nullptr;
    if (game::Call<void* (*)(const char*, void**, void*)>(0x141e5f8e0)(path, &file, buffer)) return buffer;
    game::Call<void (*)(int)>(0x141e62cb0)(slot);
    return nullptr;
  }
  void* buffer = game::Call<void* (*)(void*, void*, int)>(0x1417fa2a0)(file, sizeOut, 1);
  if (!buffer) return nullptr;
  if (game::Call<void* (*)(const char*, void**, void*)>(0x141e5f8e0)(path, &file, buffer)) return buffer;
  game::Call<void (*)(void**)>(0x141e57a10)(&buffer);
  return nullptr;
}

namespace {
// Resource type table (0x142ab6a48, 128 buckets): the alignment for a type
// id, 0x10 when unknown.
uint32_t ResourceTypeAlignment(uint32_t type) {
  for (uint32_t* node = reinterpret_cast<uint32_t**>(0x142ab6a48)[type & 0x7F]; node; node = *reinterpret_cast<uint32_t**>(node + 0x3A)) {
    if (*node == type) return node != reinterpret_cast<uint32_t*>(~uint64_t{7}) ? node[0x33] : 0x10;
  }
  return 0x10;
}
}  // namespace

// 0x14164fd60: Resource::Manager::DecompressionJob::Run: allocate the
// output through the manager's allocator callback (+0xA8) and inflate the
// packed data into it; state +0xD0 = 1 (no memory) / 2 (bad data).
void DecompressionJobRun(uint8_t* self) {
  uint8_t* packed = At<uint8_t*>(At<uint8_t*>(self, 0x90), 8);
  At<uint32_t>(self, 0xA0) = At<uint32_t>(packed, 0x10);
  uint32_t type = At<uint32_t>(self, 0x88);
  uint32_t alignment = ResourceTypeAlignment(type);
  using AllocateFn = void* (*)(void*, uint32_t*, int64_t, uint32_t);
  using FreeFn = void (*)(void*, void*, int64_t);
  void* output = At<AllocateFn>(self, 0xA8)(At<void*>(self, 0xC8), &type, static_cast<int>(alignment), At<uint32_t>(packed, 0x10));
  At<void*>(self, 0x98) = output;
  if (!output) {
    At<int>(self, 0xD0) = 1;
    return;
  }
  if (game::Call<int (*)(void*, uint32_t, void*, uint32_t)>(0x14167efa0)(packed + 0x20, At<uint32_t>(packed, 0x14), output, At<uint32_t>(packed, 0x10)) < 0) {
    alignment = ResourceTypeAlignment(At<uint32_t>(self, 0x88));
    At<FreeFn>(self, 0xB0)(At<void*>(self, 0xC8), At<void*>(self, 0x98), static_cast<int>(alignment));
    At<void*>(self, 0x98) = nullptr;
    At<int>(self, 0xD0) = 2;
  }
}

// 0x14033e310: RefArrayPooled storage policy: same growth rule as
// SoeUtil::Array, but sizes are rounded to a power-of-two granularity and
// served from the array's pool (+0x28) or the global pool (0x142b06e08);
// without any pool, round to even and use the heap.
void* RefArrayPooledReallocate(uint8_t* self, int count, int* newSize, bool exact) {
  int capacity = At<int>(self, 0x14);
  if (!exact) {
    if (capacity < count) {
      *newSize = static_cast<int>(static_cast<uint32_t>(count) * 5u) / 4;
    } else if (capacity <= static_cast<int>(static_cast<uint32_t>(count) * 4u) / 3) {
      *newSize = capacity;
      return At<void*>(self, 8);
    } else {
      *newSize = static_cast<int>(static_cast<uint32_t>(count) * 6u) / 5;
    }
  } else {
    if (capacity == count) {
      *newSize = capacity;
      return At<void*>(self, 8);
    }
    if (count == 0) {
      *newSize = 0;
      return nullptr;
    }
    *newSize = count;
  }
  void* pool = At<void*>(self, 0x28);
  if (!pool) {
    if (!*reinterpret_cast<bool*>(0x142b06e00)) {
      game::Call<void (*)()>(0x14033b300)();
      *reinterpret_cast<bool*>(0x142b06e00) = true;
    }
    pool = *reinterpret_cast<void**>(0x142b06e08);
    if (!pool) {
      int size = *newSize + ((*newSize - 1) & 1);
      *newSize = size;
      if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
        return game::Call<void* (*)(int64_t, void*)>(0x1402fc150)(size, reinterpret_cast<void*>(0x143c46658));
      return game::Call<void* (*)(size_t, size_t)>(0x14032f910)(static_cast<uint32_t>(size), 1);
    }
  }
  int size = *newSize;
  uint32_t mask = static_cast<uint32_t>(size / 8 - 1);
  mask |= mask >> 1;
  mask |= mask >> 2;
  mask |= mask >> 4;
  mask |= mask >> 8;
  uint32_t step = (mask | mask >> 16) + 1;
  step += step == 0;
  int padded = size - 1 + static_cast<int>(step);
  int rounded = padded - padded % static_cast<int>(step);
  int finalSize = (rounded & 1) + rounded;
  *newSize = finalSize;
  return game::Call<void* (*)(void*, int, int, uint32_t)>(0x14033e7e0)(pool, finalSize, rounded, step);
}

// 0x141e96390: StoreBundleDefinition ToString: the MarketingBundle part
// (0x14161b4f0), then the store fields (an empty StringFixed<16> scratch is
// built and dropped, as in the original).
const char* StoreBundleToString(uint8_t* self, soeutil::IString* out) {
  game::Call<const char* (*)(void*, soeutil::IString*)>(0x14161b4f0)(self, out);
  soeutil::StringFixed<16> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x14204baa8));
  int onSale = At<bool>(self, 0x1CB) || At<int>(self, 0x1BC) != 0 ? 1 : 0;
  game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(
      out, reinterpret_cast<const char*>(0x1425aa400), At<int>(self, 0x1A0), At<int>(self, 0x1A4), At<int>(self, 0x1B8), onSale, At<uint8_t>(self, 0x1CA),
      At<uint8_t>(self, 0x1C9), At<uint8_t>(self, 0x199), At<uint8_t>(self, 0x1C8), At<int>(self, 0x1A8), At<int>(self, 0x1AC), At<int>(self, 0x1B0),
      At<int>(self, 0x1B4), At<int>(self, 0x1BC), At<int>(self, 0x1C0));
  const char* text = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x14204ba88);
  soeutil::StringRelease(&scratch);
  return text;
}

// 0x1416186c0: GameCommerce::CasApi (HttpListener subobject at +0x28)
// OnHttpResponse(.., url, requestId, data, httpCode): match the pending
// request (16 buckets at +0x2B0), log it with its round-trip time and queue
// the response (+0x1D8) holding a reference to the data; unknown ids are
// logged as ignored.
void CasApiOnHttpResponse(uint8_t* self, void*, const char* url, uint32_t requestId, uint8_t* data, uint32_t httpCode) {
  uint8_t* lock = self + 0x190;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  using LogFn = void (*)(void*, void*, const char*, ...);
  for (uint8_t* request = At<uint8_t*>(self, 0x2B0 + (requestId & 0xF) * 8);; request = At<uint8_t*>(request, 0x460)) {
    if (!request) {
      game::Call<LogFn>(0x14165c700)(self - 0x28, At<void*>(self, 0x1510), reinterpret_cast<const char*>(0x1424baa60), data ? At<int>(data, 0x10) : 0, url,
                                     requestId, httpCode);
      break;
    }
    if (At<uint32_t>(request, 0x458) != requestId) continue;
    int length = data ? At<int>(data, 0x10) : 0;
    void* serviceUrl = At<void*>(request, 0x30);
    int64_t started = At<int64_t>(request, 0x20);
    int64_t now;
    int64_t elapsed = *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now) - started;
    int elapsedMs = elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
    game::Call<LogFn>(0x14165c9a0)(self - 0x28, At<void*>(self, 0x1510), reinterpret_cast<const char*>(0x1424bbc10), length, url, requestId, httpCode,
                                   elapsedMs, serviceUrl);
    if (data) {
      if (char* terminator = game::Call<char* (*)(void*)>(0x140aa52a0)(data)) *terminator = 0;
    }
    uint32_t* response = game::Call<uint32_t* (*)(void*)>(0x141618390)(self + 0x1D8);
    *response = requestId;
    soeutil::StringAssign(response + 2, url);
    if (data) {
      *reinterpret_cast<uint8_t**>(response + 8) = data;
      auto* counts = At<volatile long*>(data, 0x20);
      _InterlockedIncrement(&counts[1]);
      _InterlockedIncrement(&counts[0]);
    }
    response[10] = httpCode;
    break;
  }
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

// 0x141612900: GameCommerce::UramApi (HttpListener subobject at +0x28)
// OnHttpResponse: as CasApi, with 16 buckets at +0xE0, the server
// response time (+0x568) logged alongside the total (+0x560).
void UramApiOnHttpResponse(uint8_t* self, void*, const char* url, uint32_t requestId, uint8_t* data, uint32_t httpCode) {
  uint8_t* lock = self + 0x178;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  using LogFn = void (*)(void*, void*, const char*, ...);
  auto sinceMs = [](int64_t start) {
    int64_t now;
    int64_t elapsed = *game::Call<int64_t* (*)(int64_t*)>(0x14032fd30)(&now) - start;
    return elapsed > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(elapsed);
  };
  for (uint8_t* request = At<uint8_t*>(self, 0xE0 + (requestId & 0xF) * 8);; request = At<uint8_t*>(request, 0x5A8)) {
    if (!request) {
      game::Call<LogFn>(0x14165c700)(self - 0x28, At<void*>(self, 0x2C8), reinterpret_cast<const char*>(0x1424baa60), data ? At<int>(data, 0x10) : 0, url,
                                     requestId, httpCode);
      break;
    }
    if (At<uint32_t>(request, 0x5A0) != requestId) continue;
    int length = data ? At<int>(data, 0x10) : 0;
    int totalMs = sinceMs(At<int64_t>(request, 0x560));
    int serverMs = sinceMs(At<int64_t>(request, 0x568));
    game::Call<LogFn>(0x14165c9a0)(self - 0x28, At<void*>(self, 0x2C8), reinterpret_cast<const char*>(0x1424ba9e0), length, url, requestId, httpCode, serverMs,
                                   totalMs);
    if (data) {
      if (char* terminator = game::Call<char* (*)(void*)>(0x140aa52a0)(data)) *terminator = 0;
    }
    uint32_t* response = game::Call<uint32_t* (*)(void*)>(0x141611c30)(self + 0x1C0);
    *response = requestId;
    soeutil::StringAssign(response + 2, url);
    if (data) {
      *reinterpret_cast<uint8_t**>(response + 8) = data;
      auto* counts = At<volatile long*>(data, 0x20);
      _InterlockedIncrement(&counts[1]);
      _InterlockedIncrement(&counts[0]);
    }
    response[10] = httpCode;
    break;
  }
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
}

namespace {
// Inlined SoeUtil::String truncate used by the list ToStrings: drop the
// trailing ", ", unsharing (copy-on-write) the buffer first if needed.
template <int N>
void DropTrailingSeparator(soeutil::StringFixed<N>& ids) {
  if (ids.length > 2) {
    int newLength = ids.length - 2;
    int needed = ids.length - 1;
    char* buffer = ids.data;
    int capacity = ids.capacity;
    if (ids.capacity < needed || (ids.capacity > 0 && reinterpret_cast<int*>(ids.data)[-1] > 1)) {
      if (needed < ids.length + 1) needed = ids.length + 1;
      int granted;
      char onHeap;
      auto* block = reinterpret_cast<int*>(
          reinterpret_cast<void* (*)(void*, int, int*, char*)>(ids.vtable[soeutil::kStringSlotAllocate])(&ids, needed + 4, &granted, &onHeap));
      if (block) _InterlockedExchange(reinterpret_cast<volatile long*>(block), onHeap != 0);
      buffer = reinterpret_cast<char*>(block + 1);
      capacity = granted - 4;
      game::Call<void* (*)(void*, const void*, size_t)>(0x140d11e20)(buffer, ids.data, static_cast<size_t>(ids.length + 1));
      if (ids.capacity > 0 && _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ids.data - 4), -1) < 2)
        reinterpret_cast<void (*)(void*)>(ids.vtable[soeutil::kStringSlotFree])(&ids);
    }
    ids.capacity = capacity;
    ids.data = buffer;
    ids.data[newLength] = 0;
    ids.length = newLength;
  }
}
}  // namespace

// 0x14161fb60: StoreBundleCategoryGroupDefinition ToString: "groupId=%d,
// categoryIds=(a, b, ...)" built in a StringFixed<1024>; the trailing ", "
// is cut with the inlined copy-on-write truncate.
const char* StoreCategoryGroupToString(uint8_t* self, soeutil::IString* out) {
  soeutil::StringFixed<1024> ids;
  soeutil::InitFixed(ids, reinterpret_cast<void**>(0x14204b2f0));
  if (uint32_t* node = At<uint32_t*>(self, 0x18)) {
    do {
      game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(&ids, reinterpret_cast<const char*>(0x1424bc8d0), *node);  // "%d, "
      node = *reinterpret_cast<uint32_t**>(node + 2);
    } while (node);
    DropTrailingSeparator(ids);
  }
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bc8d8), At<int>(self, 8), ids.data);
  const char* text = out->data;
  ids.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&ids);
  return text;
}

// 0x1416035a0: Crypto::Prng Generate(out, count): CTR-mode output. Drain
// the leftover keystream block (+0x78, position +0xA0), encrypt the 128-bit
// counter (+0x48) for each whole block, then one more block for the tail.
uint8_t* PrngGenerate(uint8_t* self, uint8_t* out, int count) {
  if (!At<bool>(self, 0xB0)) return nullptr;
  uint8_t* cursor = out;
  for (int position = At<int>(self, 0xA0); position > 0 && count > 0; --count) {
    int index = At<int>(self, 0xA0)++;
    *cursor++ = At<uint8_t*>(self, 0x78)[index];
    position = At<int>(self, 0xA0) % 16;
    At<int>(self, 0xA0) = position;
  }
  auto counterKey = [&] { return At<int>(self, 0x50) ? At<void*>(self, 0x48) : nullptr; };
  while (count > 15) {
    struct Block16 {
      void** vtable;
      uint8_t* data;
      uint64_t sizes;
      uint8_t inlineBuffer[16];
    } block{reinterpret_cast<void**>(0x1424b3b30), nullptr, 0, {}};
    if (Virtual<int>(self + 8, 4, counterKey(), 0x10, static_cast<void*>(&block)) != 1) {
      game::Call<void (*)(void*)>(0x1415fcdd0)(&block);
      return nullptr;
    }
    for (int64_t i = 15; i >= 0; --i)
      if (++At<uint8_t*>(self, 0x48)[i] != 0) break;
    ++At<int64_t>(self, 0xA8);
    for (int i = 0; i < 16; ++i) cursor[i] = block.data[i];
    cursor += 16;
    count -= 16;
    block.vtable = reinterpret_cast<void**>(0x1424b3b30);
    block.sizes &= 0xFFFFFFFF00000000ull;
    if (block.data != block.inlineBuffer) FreeHeapOrThread(block.data, 1);
    block.data = nullptr;
    block.vtable = reinterpret_cast<void**>(0x14204adc8);
    block.sizes &= 0xFFFFFFFF00000000ull;
    FreeHeapOrThread(nullptr, 1);
  }
  if (count < 1) return out;
  if (Virtual<int>(self + 8, 4, counterKey(), 0x10, static_cast<void*>(self + 0x70)) != 1) return nullptr;
  game::Call<void* (*)(void*, const void*, size_t)>(0x140d11e20)(cursor, At<uint8_t*>(self, 0x78) + (0x10 - count), static_cast<size_t>(count));
  At<int>(self, 0xA0) = 0x10 - count;
  game::Call<void (*)(void*)>(0x1416033b0)(self);
  return out;
}

namespace {
// SoeGems::PerformanceProfiler per-thread data (TLS slot at +8): depth
// counter +0, enabled +0x10, current node +0x18, lock +0x20, node map
// +0x68 (2048 buckets at +0x90), list links +0x150B8 / +0x150C0.
uint8_t* ProfilerThreadData(uint8_t* self) {
  using TlsGetFn = void*(__stdcall*)(unsigned long);
  using TlsSetFn = int(__stdcall*)(unsigned long, void*);
  auto* data = static_cast<uint8_t*>((*reinterpret_cast<TlsGetFn*>(0x14409fe38))(At<unsigned long>(self, 8)));
  if (!data) {
    uint8_t* lock = self + 0x31170;
    game::Call<void (*)(void*)>(0x14032f270)(lock);
    uint64_t threadId = game::Call<uint64_t (*)()>(0x14032e7b0)();
    uint8_t* created = Virtual<uint8_t*>(self + 0x10, 2);
    data = nullptr;
    if (created) {
      game::Call<void (*)(void*, uint64_t, uint8_t)>(0x140326c20)(created, threadId, At<uint8_t>(self, 0x31200));
      data = created;
    }
    At<void*>(data, 0x150C0) = At<void*>(self, 0x20);
    At<uint64_t>(data, 0x150B8) = 0;
    if (!At<void*>(self, 0x20))
      At<void*>(self, 0x18) = data;
    else
      At<void*>(At<void*>(self, 0x20), 0x150B8) = data;
    At<void*>(self, 0x20) = data;
    ++At<int>(self, 0x28);
    (*reinterpret_cast<TlsSetFn*>(0x14409fe40))(At<unsigned long>(self, 8), data);
    if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  } else if (At<int>(data, 0) == 0) {
    At<uint8_t>(data, 0x10) = At<uint8_t>(self, 0x31200);
  }
  return data;
}

// Find the sample node for (hash, id, depth) in the thread's map.
uint8_t* ProfilerFindNode(uint8_t* data, uint32_t hash, uint32_t id, uint32_t depth) {
  for (uint8_t* node = At<uint8_t*>(data, 0x90 + (hash & 0x7FF) * 8); node; node = At<uint8_t*>(node, 0x80))
    if (At<uint32_t>(node, 0x78) == hash && At<uint32_t>(node, 0x44) == id && At<uint32_t>(node, 0x30) == depth) return node;
  return nullptr;
}

// Register the sample name (+0x15120 map, guarded by +0x311B8) on first
// use.
void ProfilerRegisterName(uint8_t* self, uint32_t* id, void** name) {
  game::Call<void (*)(void*)>(0x14032f270)(self + 0x311B8);
  bool known = false;
  for (uint8_t* entry = At<uint8_t*>(self, 0x15148 + (*id & 0x7FF) * 8); entry; entry = At<uint8_t*>(entry, 0x58))
    if (At<uint32_t>(entry, 0x50) == *id) {
      known = true;
      break;
    }
  if (!known) game::Call<void (*)(void*, uint32_t*, void**)>(0x1403265f0)(self + 0x15120, id, name);
  game::Call<void (*)(void*)>(0x14032f360)(self + 0x311B8);
}
}  // namespace

// 0x14032ac10: SoeGems::PerformanceProfiler::Begin(name, id, noTiming):
// push a sample under the thread's current one (hashed with the parent and
// depth) and start its timer unless noTiming.
void ProfilerBegin(uint8_t* self, void* name, uint32_t id, bool noTiming) {
  uint8_t* data = ProfilerThreadData(self);
  ++At<int>(data, 0);
  if (!At<uint8_t>(data, 0x10)) return;
  uint32_t depth = At<uint32_t>(data, 0);
  uint32_t hash = id;
  if (At<void*>(data, 0x18)) hash = At<uint32_t>(At<void*>(data, 0x18), 0x40) ^ depth ^ id;
  uint8_t* node = ProfilerFindNode(data, hash, id, depth);
  if (!node) {
    uint8_t* lock = data + 0x20;
    game::Call<void (*)(void*)>(0x14032f270)(lock);
    uint64_t key = hash;
    node = game::Call<uint8_t* (*)(void*, uint64_t*, void*)>(0x140329ed0)(data + 0x68, &key, nullptr);
    At<uint32_t>(node, 0x48) = noTiming ? 1 : 0;
    At<void*>(node, 0x38) = At<void*>(data, 0x18);
    At<uint32_t>(node, 0x40) = hash;
    At<uint32_t>(node, 0x44) = id;
    At<uint32_t>(node, 0x30) = At<uint32_t>(data, 0);
    ProfilerRegisterName(self, &id, &name);
    if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  }
  if (At<uint32_t>(node, 0x48) == 0) {
    uint64_t stamp;
    At<uint64_t>(node, 0x50) = *game::Call<uint64_t* (*)(uint64_t*)>(0x14032fde0)(&stamp);
  }
  At<void*>(data, 0x18) = node;
}

// 0x14032aea0: SoeGems::PerformanceProfiler::AddValue(name, id, value):
// accumulate a value sample one level below the current one (count, sum,
// max, min, last).
void ProfilerAddValue(uint8_t* self, void* name, uint32_t id, int64_t value) {
  uint8_t* data = ProfilerThreadData(self);
  if (!At<uint8_t>(data, 0x10)) return;
  uint32_t depth = At<uint32_t>(data, 0) + 1;
  uint32_t hash = id;
  if (At<void*>(data, 0x18)) hash = At<uint32_t>(At<void*>(data, 0x18), 0x40) ^ depth ^ id;
  uint8_t* node = ProfilerFindNode(data, hash, id, depth);
  if (!node) {
    uint8_t* lock = data + 0x20;
    game::Call<void (*)(void*)>(0x14032f270)(lock);
    uint8_t* parent = At<uint8_t*>(data, 0x18);
    uint8_t* before = parent;
    while (before && (before == parent || At<int>(parent, 0x30) < At<int>(before, 0x30))) before = At<uint8_t*>(before, 0x68);
    uint64_t key = hash;
    node = game::Call<uint8_t* (*)(void*, uint64_t*, void*)>(0x140329ed0)(data + 0x68, &key, before);
    At<uint32_t>(node, 0x48) = 1;
    At<void*>(node, 0x38) = At<void*>(data, 0x18);
    At<uint32_t>(node, 0x40) = hash;
    At<uint32_t>(node, 0x44) = id;
    At<uint32_t>(node, 0x30) = depth;
    ProfilerRegisterName(self, &id, &name);
    if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
  }
  ++At<int64_t>(node, 0);
  At<int64_t>(node, 0x20) = value;
  At<int64_t>(node, 8) += value;
  if (At<int64_t>(node, 0x10) < value) At<int64_t>(node, 0x10) = value;
  if (value < At<int64_t>(node, 0x18)) At<int64_t>(node, 0x18) = value;
}

// 0x14161d720: StoreBundleGroupDefinition ToString: entries rendered as
// "(entry), " into a StringFixed<1024> (each through a StringFixed<256>
// scratch), trailing ", " dropped, then the group fields.
const char* StoreBundleGroupToString(uint8_t* self, soeutil::IString* out) {
  soeutil::StringFixed<1024> entries;
  soeutil::InitFixed(entries, reinterpret_cast<void**>(0x14204b2f0));
  soeutil::StringFixed<256> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x142049e08));
  if (void** entry = At<void**>(self, 0x68)) {
    do {
      const char* text = Virtual<const char*>(entry, 1, static_cast<void*>(&scratch));
      game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(&entries, reinterpret_cast<const char*>(0x1421d29dc), text);  // "(%s), "
      entry = static_cast<void**>(entry[2]);
    } while (entry);
    DropTrailingSeparator(entries);
  }
  const char* list = entries.data;
  int entryCount = At<int>(self, 0x78);
  uint8_t exclusive = At<uint8_t>(self, 0x50);
  const char* image = Virtual<const char*>(self + 0x18, 1, static_cast<void*>(&scratch));
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bc540), At<int>(self, 8), At<int>(self, 0xC), At<int>(self, 0x10), image,
                                    At<int>(self, 0x14), exclusive, entryCount, list);
  const char* result = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&scratch);
  scratch.data = soeutil::EmptyStringData();
  scratch.length = 0;
  scratch.capacity = 0;
  scratch.vtable = soeutil::IStringVtable();
  entries.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&entries);
  return result;
}

// 0x14034f1a0: GameClientInputManager Initialize(window): base init, raw
// mouse + keyboard registration, optional input thread (+0x8010), then
// DirectInput8 game controllers: absolute axes, 8 axes ranged 0..0xFFFF,
// background non-exclusive cooperative level. The original checks a stale
// result after each axis-range SetProperty; kept as is.
bool InputManagerInitialize(uint8_t* self, void* window) {
  if (!game::Call<bool (*)(void*)>(0x141667990)(self)) return false;
  struct RawDevice {
    uint16_t usagePage;
    uint16_t usage;
    uint32_t flags;
    void* target;
  } devices[2] = {{1, 2, 0, nullptr}, {1, 6, 0x100, window}};
  static_assert(sizeof(RawDevice) == 0x10);
  using RegisterFn = int(__stdcall*)(RawDevice*, unsigned, unsigned);
  if (!(*reinterpret_cast<RegisterFn*>(0x1440a04a8))(devices, 2, 0x10)) {
    game::Call<void (*)(const char*, const char*, ...)>(0x1402baba0)(reinterpret_cast<const char*>(0x142053008), reinterpret_cast<const char*>(0x142052fd8));
    return false;
  }
  if (At<bool>(self, 0x8010)) {
    void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0x48);
    At<void*>(self, 0x81F0) = memory ? game::Call<void* (*)(void*, const char*, int)>(0x14032ef00)(memory, reinterpret_cast<const char*>(0x142053018), 0) : nullptr;
    memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0xC0);
    void* thread = memory ? game::Call<void* (*)(void*, int, void*)>(0x14034ea30)(memory, 0, self) : nullptr;
    At<void*>(self, 0x81F8) = thread;
    game::Call<void (*)(void*)>(0x140335b70)(thread);
  }
  using ModuleFn = void*(__stdcall*)(const char*);
  void* module = (*reinterpret_cast<ModuleFn*>(0x14409fea8))(nullptr);
  void** direct = At<void**>(self, 0x8218);
  if (game::Call<long (*)(void*, unsigned, const void*, void*, void*)>(0x140d9987e)(module, 0x800, reinterpret_cast<const void*>(0x14248ab60), direct,
                                                                                    nullptr) < 0)
    return false;
  void* input = *At<void**>(self, 0x8218);
  if (Virtual<long>(input, 4, 4, reinterpret_cast<void*>(0x14034ed40), static_cast<void*>(At<void*>(self, 0x8218)), 1) < 0) return false;  // EnumDevices
  for (uint8_t* device = At<uint8_t*>(At<void*>(self, 0x8218), 0x10); device; device = At<uint8_t*>(device, 0x140)) {
    if (At<int>(device, 0x20) != 0) continue;
    void* directInput = *At<void**>(self, 0x8218);
    long result = Virtual<long>(directInput, 3, static_cast<void*>(device), static_cast<void*>(device + 0x28), static_cast<void*>(nullptr));  // CreateDevice
    if (result < 0) return false;
    void* joystick = At<void*>(device, 0x28);
    result = Virtual<long>(joystick, 11, reinterpret_cast<const void*>(0x1422034c0));  // SetDataFormat(c_dfDIJoystick)
    if (result < 0) return false;
    struct PropertyDword {
      uint32_t size, headerSize, object, how, data;
    } axisMode{0x14, 0x10, 0, 0, 0};
    result = Virtual<long>(joystick, 6, reinterpret_cast<void*>(2), static_cast<void*>(&axisMode));  // DIPROP_AXISMODE = absolute
    if (result < 0) return false;
    for (uint32_t axis = 0; axis < 8; ++axis) {
      struct PropertyRange {
        uint32_t size, headerSize, object, how;
        int32_t minimum, maximum;
      } range{0x18, 0x10, axis * 4, 1, 0, 0xFFFF};
      Virtual<long>(At<void*>(device, 0x28), 6, reinterpret_cast<void*>(4), static_cast<void*>(&range));  // DIPROP_RANGE
      if (result < 0) return false;
    }
    if (Virtual<long>(At<void*>(device, 0x28), 13, window, 6) < 0) return false;  // SetCooperativeLevel(BACKGROUND | NONEXCLUSIVE)
  }
  return true;
}

// 0x141623580: GameCommerce::MarketingDataSource AddGroup(definition, ..):
// find or create the group for the definition id (+0x100), then append one
// MarketingDataElementInstance per column (list at +0x118, next +0x48):
// type 1 int64, 2 double, 3 StringFixed<32>, anything else null. Instances
// come from the source's pools and reserve 8 values each.
uint64_t MarketingDataSourceAddGroup(uint8_t* self, uint8_t* definition, void*, void* extra) {
  uint64_t key = At<uint32_t>(definition, 0x100);
  uint8_t* source = definition;
  uint8_t* group = game::Call<uint8_t* (*)(void*, uint64_t*, uint8_t**, void*)>(0x141621f80)(self + 0x10, &key, &source, extra);
  auto makeInstance = [](uint8_t* instance, uint64_t vtable, uint64_t arrayVtable) {
    At<uint64_t>(instance, 0) = vtable;
    At<uint64_t>(instance, 0x10) = 0;
    At<uint64_t>(instance, 0x18) = 0;
    At<uint64_t>(instance, 8) = arrayVtable;
  };
  for (int* column = At<int*>(definition, 0x118); column; column = *reinterpret_cast<int**>(column + 0x12)) {
    uint8_t* instance = nullptr;
    if (*column == 1) {
      if (uint8_t* created = game::Call<uint8_t* (*)(void*)>(0x141623220)(self + 0x138)) {
        makeInstance(created, 0x1424bd080, 0x1424bd058);
        At<int>(created, 0x18) = 0;
        Virtual(created, 1, 8);
        instance = created;
      }
    } else if (*column == 2) {
      if (uint8_t* created = game::Call<uint8_t* (*)(void*)>(0x141623220)(self + 0x1A160)) {
        makeInstance(created, 0x1424bd108, 0x1424bd0e0);
        At<int>(created, 0x18) = 0;
        Virtual(created, 1, 8);
        instance = created;
      }
    } else if (*column == 3) {
      if (uint8_t* created = game::Call<uint8_t* (*)(void*)>(0x141623180)(self + 0x34188)) {
        makeInstance(created, 0x1424bd190, 0x1424bd168);
        game::Call<void (*)(void*, int)>(0x140459660)(created + 8, 0);
        Virtual(created, 1, 8);
        instance = created;
      }
    }
    uint8_t* list = group + 8;  // SoeUtil::Array<Instance*>: data +0x10, count +0x18, capacity +0x1C
    int needed = At<int>(group, 0x18) + 1;
    if (At<int>(group, 0x1C) < needed) {
      int64_t capacity = 0;
      auto** grown = Virtual<uint8_t**>(list, 1, needed, &capacity, false);
      auto** old = At<uint8_t**>(group, 0x10);
      if (grown != old) {
        if (old) {
          for (int i = 0; i < At<int>(group, 0x18); ++i)
            if (grown + i) grown[i] = old[i];
          Virtual(list, 2, static_cast<void*>(old), At<int>(group, 0x1C));
        }
        At<uint8_t**>(group, 0x10) = grown;
        At<int>(group, 0x1C) = static_cast<int>(capacity);
      }
    }
    uint8_t** slot = At<uint8_t**>(group, 0x10) + At<int>(group, 0x18);
    ++At<int>(group, 0x18);
    if (slot) *slot = instance;
  }
  return 0;
}

// 0x141628ed0: GameCommerce::BaseInGamePurchaseOrder ToString: details
// (iterated with slots 8 / 9, each rendered through slot 2 into a
// StringFixed<1024> scratch) joined into a StringFixed<4096>, then the
// order fields and detail count (slot 6).
const char* PurchaseOrderToString(void** self, soeutil::IString* out) {
  soeutil::StringFixed<4096> details;
  soeutil::InitFixed(details, reinterpret_cast<void**>(0x14204b038));
  soeutil::StringFixed<1024> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x14204b2f0));
  for (void* detail = Virtual<void*>(self, 8); detail; detail = Virtual<void*>(self, 9, detail)) {
    const char* text = Virtual<const char*>(detail, 2, static_cast<void*>(&scratch));
    game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402ed6c0)(&details, reinterpret_cast<const char*>(0x1424bd9e8), text);
  }
  DropTrailingSeparator(details);
  const char* list = details.data;
  auto field = [&](size_t index) { return reinterpret_cast<const char*>(self[index]); };
  int detailCount = Virtual<int>(self, 6);
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bd9f0), field(2), field(10), static_cast<int>(reinterpret_cast<intptr_t>(self[0x11])),
                                    field(0x13), field(0x1E), field(0x18), detailCount, list);
  const char* result = out->data;
  scratch.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&scratch);
  scratch.data = soeutil::EmptyStringData();
  scratch.length = 0;
  scratch.capacity = 0;
  scratch.vtable = soeutil::IStringVtable();
  details.vtable = reinterpret_cast<void**>(0x14204b018);
  soeutil::StringRelease(&details);
  return result;
}

namespace {
// Inlined ~StringFixed<N>: release through the interface vtable, then the
// IString base leaves it empty.
template <int N>
void DestroyFixed(soeutil::StringFixed<N>& text, uint64_t interfaceVtable) {
  text.vtable = reinterpret_cast<void**>(interfaceVtable);
  soeutil::StringRelease(&text);
  text.data = soeutil::EmptyStringData();
  text.length = 0;
  text.capacity = 0;
  text.vtable = soeutil::IStringVtable();
}
}  // namespace

// 0x14161b4f0: GameCommerce::MarketingBundleDefinition ToString: entries
// (+0x70, next +0x48) and tags (+0x110, next +0x10) rendered as "(x), "
// lists, launch / expire times (+8 / +0x10) formatted with 0x14166bb30.
const char* MarketingBundleToString(uint8_t* self, soeutil::IString* out) {
  soeutil::StringFixed<1024> entries;
  soeutil::InitFixed(entries, reinterpret_cast<void**>(0x14204b2f0));
  soeutil::StringFixed<1024> tags;
  soeutil::InitFixed(tags, reinterpret_cast<void**>(0x14204b2f0));
  soeutil::StringFixed<256> scratch;
  soeutil::InitFixed(scratch, reinterpret_cast<void**>(0x142049e08));
  using AppendFn = void (*)(soeutil::IString*, const char*, ...);
  if (void** entry = At<void**>(self, 0x70)) {
    do {
      game::Call<AppendFn>(0x1402ed6c0)(&entries, reinterpret_cast<const char*>(0x1421d29dc), Virtual<const char*>(entry, 1, static_cast<void*>(&scratch)));
      entry = static_cast<void**>(entry[9]);
    } while (entry);
    DropTrailingSeparator(entries);
  }
  for (void** tag = At<void**>(self, 0x110); tag; tag = static_cast<void**>(tag[2]))
    game::Call<AppendFn>(0x1402ed6c0)(&tags, reinterpret_cast<const char*>(0x1421d29dc), Virtual<const char*>(tag, 1, static_cast<void*>(&scratch)));
  DropTrailingSeparator(tags);
  const char* entryList = entries.data;
  const char* tagList = tags.data;
  soeutil::StringFixed<32> launchText;
  soeutil::InitFixed(launchText, reinterpret_cast<void**>(0x14204a378));
  soeutil::StringFixed<32> expireText;
  soeutil::InitFixed(expireText, reinterpret_cast<void**>(0x14204a378));
  int tagCount = At<int>(self, 0x120);
  int entryCount = At<int>(self, 0x80);
  uint64_t expire = At<uint64_t>(self, 0x10);
  uint64_t launch = At<uint64_t>(self, 8);
  uint64_t tintGroup = At<uint64_t>(self, 0x58);
  using TimeFn = const char* (*)(uint64_t*, void*, int);
  const char* expireString = game::Call<TimeFn>(0x14166bb30)(&expire, &expireText, 1);
  const char* launchString = game::Call<TimeFn>(0x14166bb30)(&launch, &launchText, 1);
  uint8_t tintable = At<uint8_t>(self, 0x199);
  const char* image = Virtual<const char*>(self + 0x18, 1, static_cast<void*>(&scratch));
  game::Call<FormatFn>(0x1402bd7f0)(out, reinterpret_cast<const char*>(0x1424bc240), At<int>(self, 0x170), At<int>(self, 0x174), At<int>(self, 0x178),
                                    At<int>(self, 0x17C), image, tintable, tintGroup, At<int>(self, 0x184), At<int>(self, 0x180), At<int>(self, 0x18C),
                                    At<int>(self, 0x188), At<int>(self, 0x190), launchString, expireString, At<int>(self, 0x194), entryCount, entryList,
                                    tagCount, tagList);
  const char* result = out->data;
  DestroyFixed(expireText, 0x14204a358);
  DestroyFixed(launchText, 0x14204a358);
  DestroyFixed(scratch, 0x142049de8);
  DestroyFixed(tags, 0x14204b2d0);
  entries.vtable = reinterpret_cast<void**>(0x14204b2d0);
  soeutil::StringRelease(&entries);
  return result;
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
REBUILD_FUNCTION(SpeedTree_CArray410_DeletingDestructor, 0x141e5e9a0, SpeedTreeArray410DeletingDestructor);
REBUILD_FUNCTION(StoreBillboardPanelDefinition_ToString, 0x141e96c70, StoreBillboardPanelToString);
REBUILD_FUNCTION(StorePortalCategoryDefinition_ToString, 0x141621bb0, StorePortalCategoryToString);
REBUILD_FUNCTION(SpeedTree_CFileSystem_LoadFile, 0x141e57fb0, SpeedTreeLoadFile);
REBUILD_FUNCTION(Resource_DecompressionJob_Run, 0x14164fd60, DecompressionJobRun);
REBUILD_FUNCTION(RefArrayPooled_Reallocate, 0x14033e310, RefArrayPooledReallocate);
REBUILD_FUNCTION(StoreBundleDefinition_ToString, 0x141e96390, StoreBundleToString);
REBUILD_FUNCTION(CasApi_OnHttpResponse, 0x1416186c0, CasApiOnHttpResponse);
REBUILD_FUNCTION(UramApi_OnHttpResponse, 0x141612900, UramApiOnHttpResponse);
REBUILD_FUNCTION(StoreBundleCategoryGroupDefinition_ToString, 0x14161fb60, StoreCategoryGroupToString);
REBUILD_FUNCTION(Crypto_Prng_Generate, 0x1416035a0, PrngGenerate);
REBUILD_FUNCTION(PerformanceProfiler_Begin, 0x14032ac10, ProfilerBegin);
REBUILD_FUNCTION(PerformanceProfiler_AddValue, 0x14032aea0, ProfilerAddValue);
REBUILD_FUNCTION(StoreBundleGroupDefinition_ToString, 0x14161d720, StoreBundleGroupToString);
REBUILD_FUNCTION(GameClientInputManager_Initialize, 0x14034f1a0, InputManagerInitialize);
REBUILD_FUNCTION(MarketingDataSource_AddGroup, 0x141623580, MarketingDataSourceAddGroup);
REBUILD_FUNCTION(BaseInGamePurchaseOrder_ToString, 0x141628ed0, PurchaseOrderToString);
REBUILD_FUNCTION(MarketingBundleDefinition_ToString, 0x14161b4f0, MarketingBundleToString);
