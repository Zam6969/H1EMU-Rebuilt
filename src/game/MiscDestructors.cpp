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
