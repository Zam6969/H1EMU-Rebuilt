// Scalar deleting destructors (vtable slot 0) across the engine's utility,
// asset, audio-hook, commerce, task and third-party classes, rebuilt from
// two templates:
//   - member-less bases: reset the vtable, then sized delete if (flags & 1);
//   - classes with a destructor: call it, then sized delete if (flags & 1).
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

#include <intrin.h>

namespace rebuild::game_misc {
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

}  // namespace
}  // namespace rebuild::game_misc

using namespace rebuild::game_misc;

REBUILD_FUNCTION(SoeUtil__LogHandler_DeletingDestructor, 0x1403066f0, (SimpleDeletingDestructor<0x142049b70, 0x8>));
REBUILD_FUNCTION(SceaSharedUtil__CrashReporterHandler_DeletingDestructor, 0x1403066c0, (SimpleDeletingDestructor<0x142049b90, 0x8>));
REBUILD_FUNCTION(Logging__LoggingApi_DeletingDestructor, 0x14030ce70, (DeletingDestructor<0x14030c420, 0xf580>));
REBUILD_FUNCTION(SoeGems__IRateTracker_DeletingDestructor, 0x140311230, (SimpleDeletingDestructor<0x14204b290, 0x8>));
REBUILD_FUNCTION(SoeUtil__ProfilerHandler_DeletingDestructor, 0x1403275e0, (SimpleDeletingDestructor<0x14204e4f8, 0x8>));
REBUILD_FUNCTION(SoeUtil__Thread_DeletingDestructor, 0x140335980, (DeletingDestructor<0x1403358a0, 0xb0>));
REBUILD_FUNCTION(GameCore__CoreGameClient_DeletingDestructor, 0x14034e510, (DeletingDestructor<0x14034e300, 0x31488>));
REBUILD_FUNCTION(GameCore__InputThread_DeletingDestructor, 0x14034eca0, (DeletingDestructor<0x1403358a0, 0xc0>));
REBUILD_FUNCTION(AssetDelivery__Loader_DeletingDestructor, 0x141501180, (DeletingDestructor<0x141501110, 0x38>));
REBUILD_FUNCTION(AK__StreamMgr__IAkLowLevelIOHook_DeletingDestructor, 0x1415017a0, (SimpleDeletingDestructor<0x14248bbc8, 0x8>));
REBUILD_FUNCTION(AK__StreamMgr__IAkIOHookBlocking_DeletingDestructor, 0x141501770, (SimpleDeletingDestructor<0x14248bbc8, 0x8>));
REBUILD_FUNCTION(AK__StreamMgr__IAkFileLocationResolver_DeletingDestructor, 0x141501740, (SimpleDeletingDestructor<0x14248bc38, 0x8>));
REBUILD_FUNCTION(Crypto__CryptographicHash_DeletingDestructor, 0x1415fa120, (SimpleDeletingDestructor<0x1424b32a8, 0x8>));
REBUILD_FUNCTION(GameCommerce__IMarketingDataElementInstance_DeletingDestructor, 0x141622d40, (SimpleDeletingDestructor<0x1424bcf70, 0x8>));
REBUILD_FUNCTION(GameCommerce__MarketingDataSourceLoaderHandler_DeletingDestructor, 0x141626f50, (SimpleDeletingDestructor<0x1424bd2c0, 0x8>));
REBUILD_FUNCTION(GameCommerce__BaseInGamePurchaseOrder_DeletingDestructor, 0x141628c70, (DeletingDestructor<0x141628a00, 0x128>));
REBUILD_FUNCTION(SoeUtil__JobHandler_DeletingDestructor, 0x1413fb5b0, (SimpleDeletingDestructor<0x1424c13c0, 0x8>));
REBUILD_FUNCTION(AssetDelivery__LoaderHandler_DeletingDestructor, 0x14164d600, (SimpleDeletingDestructor<0x1424c13d8, 0x8>));
REBUILD_FUNCTION(Resource__Manager__DecompressionJob_DeletingDestructor, 0x14164d5c0, (DeletingDestructor<0x14164ccd0, 0xd8>));
REBUILD_FUNCTION(RefObjectPool_T59d9ef_DeletingDestructor, 0x14164d6b0, (DeletingDestructor<0x14164ccd0, 0xe0>));
REBUILD_FUNCTION(SoeUtil__InputManager_DeletingDestructor, 0x1416678c0, (SimpleDeletingDestructor<0x1424c4568, 0x8010>));
REBUILD_FUNCTION(SoeUtil__JobQueueHandler_DeletingDestructor, 0x1407854d0, (SimpleDeletingDestructor<0x1424c5380, 0x8>));
REBUILD_FUNCTION(TaskManagement__StateListener_DeletingDestructor, 0x14166dc10, (SimpleDeletingDestructor<0x1424c5490, 0x8>));
REBUILD_FUNCTION(TaskManagement__TaskEntry_DeletingDestructor, 0x1416722b0, (DeletingDestructor<0x141672190, 0x140>));
REBUILD_FUNCTION(TaskManagement__SchedulerCallback_DeletingDestructor, 0x141674850, (SimpleDeletingDestructor<0x1424c5958, 0x18>));
REBUILD_FUNCTION(TinyHttp__HttpManager__Request_DeletingDestructor, 0x14167ac00, (DeletingDestructor<0x14167a870, 0x260>));
REBUILD_FUNCTION(std___Facet_base_DeletingDestructor, 0x1416857b0, (SimpleDeletingDestructor<0x142520e38, 0x8>));
REBUILD_FUNCTION(pugi__xml_writer_DeletingDestructor, 0x141948e40, (SimpleDeletingDestructor<0x14252d1e0, 0x8>));
REBUILD_FUNCTION(pugi__xml_tree_walker_DeletingDestructor, 0x141948e10, (SimpleDeletingDestructor<0x14252d210, 0x10>));
REBUILD_FUNCTION(CaptureCommon__IConstBuffer_DeletingDestructor, 0x14194ee60, (SimpleDeletingDestructor<0x14252d690, 0x8>));
REBUILD_FUNCTION(CaptureCommon__IBuffer_DeletingDestructor, 0x14194ee30, (SimpleDeletingDestructor<0x14252d690, 0x8>));
REBUILD_FUNCTION(SpeedTree__CCoordSysBase_DeletingDestructor, 0x141808d80, (SimpleDeletingDestructor<0x1425a0f90, 0x8>));
REBUILD_FUNCTION(DataManagement__DataLoaderInterface_DeletingDestructor, 0x141710970, (SimpleDeletingDestructor<0x1425ad280, 0x8>));
REBUILD_FUNCTION(DataManagement__DataAccessInterface_DeletingDestructor, 0x141710940, (SimpleDeletingDestructor<0x1425ad2b8, 0x8>));

namespace rebuild::game_misc {
namespace {

template <uint64_t Target, ptrdiff_t Adjust>
void* DeletingDestructorThunk(uint8_t* self, unsigned flags) {
  return game::Call<void* (*)(uint8_t*, unsigned)>(Target)(self + Adjust, flags);
}

// SoeUtil::RefCounted-style deleting destructors: drop the weak reference on
// the shared count block at +8 ({strong, weak}, freed with the last weak).
template <size_t Size>
void* RefCountedDeletingDestructor(uint8_t* self, unsigned flags) {
  *reinterpret_cast<uint64_t*>(self) = 0x14204f640;
  if (auto* counts = *reinterpret_cast<volatile long**>(self + 8)) {
    if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts) SizedDelete(const_cast<long*>(counts), 0x10);
    *reinterpret_cast<void**>(self + 8) = nullptr;
  }
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

}  // namespace

// 0x14034e6c0 / 0x14034e550 (CoreGameClient): notify the object at +0x31418
// (vfunc 0x10 / 0x18, arguments passed through), then a BaseApp step.
void CoreGameClientNotifyA(uint8_t* self, uint64_t a, uint64_t b, uint64_t c) {
  void* target = *reinterpret_cast<void**>(self + 0x31418);
  (*reinterpret_cast<void (***)(void*, uint64_t, uint64_t, uint64_t)>(target))[0x10 / 8](target, a, b, c);
  game::Call<void (*)(uint8_t*)>(0x140309bd0)(self);  // BaseApp: clear +0x312D1
}
void CoreGameClientNotifyB(uint8_t* self, uint64_t a, uint64_t b, uint64_t c) {
  void* target = *reinterpret_cast<void**>(self + 0x31418);
  (*reinterpret_cast<void (***)(void*, uint64_t, uint64_t, uint64_t)>(target))[0x18 / 8](target, a, b, c);
  game::Call<void (*)(uint8_t*)>(0x14034e5a0)(self);
}

// 0x1415012e0 (AssetDelivery::Loader): set the name string at +8.
void AssetLoaderSetName(uint8_t* self, const char* name) {
  auto* text = reinterpret_cast<soeutil::IString*>(self + 8);
  soeutil::StringRelease(text);
  text->data = soeutil::EmptyStringData();
  text->length = 0;
  text->capacity = 0;
  if (name) soeutil::StringAssign(text, name);
}

}  // namespace rebuild::game_misc

REBUILD_FUNCTION(LoggingApi_DeletingDestructor_Thunk80, 0x14030c90c, (DeletingDestructorThunk<0x14030ce70, -0x80>));
REBUILD_FUNCTION(LoggingApi_DeletingDestructor_Thunk88, 0x14030c918, (DeletingDestructorThunk<0x14030ce70, -0x88>));
REBUILD_FUNCTION(LoggingApi_DeletingDestructor_ThunkD28, 0x14030c924, (DeletingDestructorThunk<0x14030ce70, -0xd28>));
REBUILD_FUNCTION(SoeUtil_RefCounted_DeletingDestructor, 0x1402f2a20, RefCountedDeletingDestructor<0x10>);
REBUILD_FUNCTION(SoeUtil_RefCountedImplicit_DeletingDestructor, 0x140335910, RefCountedDeletingDestructor<0x10>);
REBUILD_FUNCTION(SoeUtil_ThreadBase_DeletingDestructor, 0x1403359c0, RefCountedDeletingDestructor<0x28>);
REBUILD_FUNCTION(CoreGameClient_NotifyA, 0x14034e6c0, CoreGameClientNotifyA);
REBUILD_FUNCTION(CoreGameClient_NotifyB, 0x14034e550, CoreGameClientNotifyB);
REBUILD_FUNCTION(AssetDeliveryLoader_SetName, 0x1415012e0, AssetLoaderSetName);

#include <windows.h>

namespace rebuild::game_misc {
// 0x140351370 (GameCore::InputThread slot 2): pump this thread's Windows
// messages until asked to stop, sleeping +0xB0 ms between rounds.
void InputThreadRun(uint8_t* self) {
  MSG message;
  while (!game::Call<bool (*)(void*)>(0x140335aa0)(self)) {
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageA(&message);
    }
    game::Call<void (*)(int)>(0x14032ec60)(*reinterpret_cast<int*>(self + 0xB0));
  }
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(GameCore_InputThread_Run, 0x140351370, InputThreadRun);

namespace rebuild::game_misc {
// Thread subclasses: reset the vtable, run ~Thread (0x1403358a0), sized delete.
template <uint64_t Vtable, size_t Size>
void* ThreadDeletingDestructor(void* self, unsigned flags) {
  *static_cast<uint64_t*>(self) = Vtable;
  game::Call<void (*)(void*)>(0x1403358a0)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}

// 0x1403be2c0: IStringNoShare deleting destructor (plain IString release).
void* IStringNoShareDeletingDestructor(soeutil::IString* self, unsigned flags) {
  self->vtable = soeutil::IStringVtable();
  soeutil::StringRelease(self);
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}

// 0x14166a5a0 (SoeUtil::Internal::JobQueueWorkerThread slot 2): register
// with the queue (+0xC0), then run jobs until asked to stop: hold a shared
// reference, stamp the worker id (+0xC8 -> job +0x70), Execute (vfunc 0x20),
// Finish (vfunc 0x10), report completion, release.
void JobQueueWorkerRun(uint8_t* self) {
  void* queue = *reinterpret_cast<void**>(self + 0xC0);
  game::Call<void (*)(void*, uint8_t*)>(0x141669cd0)(queue, self);
  while (!game::Call<bool (*)(void*)>(0x140335aa0)(self)) {
    auto* job = game::Call<uint8_t* (*)(void*)>(0x14166aa90)(*reinterpret_cast<void**>(self + 0xC0));
    if (!job) continue;
    auto* counts = *reinterpret_cast<volatile long**>(job + 8);
    _InterlockedIncrement(&counts[1]);
    _InterlockedIncrement(&counts[0]);
    *reinterpret_cast<int*>(job + 0x70) = *reinterpret_cast<int*>(self + 0xC8);
    (*reinterpret_cast<void (***)(uint8_t*)>(job))[0x20 / 8](job);
    (*reinterpret_cast<void (***)(uint8_t*)>(job))[0x10 / 8](job);
    game::Call<void (*)(void*, uint8_t*)>(0x141669a70)(*reinterpret_cast<void**>(self + 0xC0), job);
    counts = *reinterpret_cast<volatile long**>(job + 8);
    bool lastStrong = _InterlockedDecrement(&counts[0]) == 0;
    if (_InterlockedExchangeAdd(&counts[1], -1) == 1 && counts) SizedDelete(const_cast<long*>(counts), 0x10);
    if (lastStrong) (*reinterpret_cast<void (***)(uint8_t*)>(job))[1](job);
  }
  game::Call<void (*)(void*, uint8_t*)>(0x141669d50)(*reinterpret_cast<void**>(self + 0xC0), self);
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(JobQueueWorkerThread_DeletingDestructor, 0x141668d30, (ThreadDeletingDestructor<0x1424c4a28, 0xd0>));
REBUILD_FUNCTION(TinyHttp_HttpManagerThread_DeletingDestructor, 0x14167ab40, (ThreadDeletingDestructor<0x1424c62e0, 0xc0>));
REBUILD_FUNCTION(IStringNoShare_DeletingDestructor, 0x1403be2c0, IStringNoShareDeletingDestructor);
REBUILD_FUNCTION(JobQueueWorkerThread_Run, 0x14166a5a0, JobQueueWorkerRun);

namespace rebuild::game_misc {
// 0x141629200 (GameCommerce::BaseInGamePurchaseOrder): IsValid - the three
// strings (+0x18 / +0x58 / +0xA0 lengths) are set, the quantity (vfunc 0x30)
// is positive and every line item (vfunc 0x40 first / 0x48 next) validates
// (item vfunc 8).
bool PurchaseOrderIsValid(uint8_t* order) {
  if (*reinterpret_cast<int*>(order + 0x18) <= 0 || *reinterpret_cast<int*>(order + 0x58) <= 0 || *reinterpret_cast<int*>(order + 0xA0) <= 0)
    return false;
  using CountFn = int (*)(uint8_t*);
  using FirstFn = uint8_t* (*)(uint8_t*);
  using NextFn = uint8_t* (*)(uint8_t*, uint8_t*);
  bool valid = (*reinterpret_cast<CountFn**>(order))[0x30 / 8](order) > 0;
  if (!valid) return false;
  for (uint8_t* item = (*reinterpret_cast<FirstFn**>(order))[0x40 / 8](order); item && valid;
       item = (*reinterpret_cast<NextFn**>(order))[0x48 / 8](order, item))
    valid &= (*reinterpret_cast<bool (***)(uint8_t*)>(item))[1](item);
  return valid;
}

// 0x1415011c0 (AssetDelivery::Loader): handler at +0x10.
void* AssetLoaderGetHandler(uint8_t* self) { return *reinterpret_cast<void**>(self + 0x10); }
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(GameCommerce_PurchaseOrder_IsValid, 0x141629200, PurchaseOrderIsValid);
REBUILD_FUNCTION_TOO_SMALL(AssetDeliveryLoader_GetHandler, 0x1415011c0, AssetLoaderGetHandler);

namespace rebuild::game_misc {
// 0x141628cb0 (GameCommerce::BaseInGamePurchaseOrder): Clear - vfunc 0x28
// (clear line items), empty the five strings, reset the currency (+0x88)
// to the default at 0x143c77fa4.
void PurchaseOrderClear(uint8_t* order) {
  (*reinterpret_cast<void (***)(uint8_t*)>(order))[0x28 / 8](order);
  auto clear = [&](size_t offset) {
    auto* text = reinterpret_cast<soeutil::IString*>(order + offset);
    soeutil::StringRelease(text);
    text->data = soeutil::EmptyStringData();
    text->length = 0;
    text->capacity = 0;
  };
  clear(0x08);
  clear(0x48);
  *reinterpret_cast<int*>(order + 0x88) = *reinterpret_cast<int*>(0x143c77fa4);
  clear(0x90);
  clear(0xB8);
  clear(0x110);
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(GameCommerce_PurchaseOrder_Clear, 0x141628cb0, PurchaseOrderClear);
