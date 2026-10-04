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

// Deleting destructors that call the class destructor first (generated list).
REBUILD_FUNCTION(HashListMap_T2b0fdb_DeletingDestructor_306100, 0x140306100, (DeletingDestructor<0x140305960, 0x191d8>));
REBUILD_FUNCTION(HashList_T8d7639_DeletingDestructor_306000, 0x140306000, (DeletingDestructor<0x140305960, 0x191d8>));
REBUILD_FUNCTION(Array_T4f6fbe_DeletingDestructor_30c9a0, 0x14030c9a0, (DeletingDestructor<0x14030bf90, 0x2020>));
REBUILD_FUNCTION(HashListMap_Tbf3d04_DeletingDestructor_3111c0, 0x1403111c0, (DeletingDestructor<0x140310de0, 0x10048>));
REBUILD_FUNCTION(HashList_Tdf032c_DeletingDestructor_311110, 0x140311110, (DeletingDestructor<0x140310de0, 0x10048>));
REBUILD_FUNCTION(SceaSharedUtil__InternalCrashReporterWrapper_DeletingDestructor_315a50, 0x140315a50, (DeletingDestructor<0x1403154f0, 0x93a0>));
REBUILD_FUNCTION(HashListMap_Tcd9a00_DeletingDestructor_321b90, 0x140321b90, (DeletingDestructor<0x140321500, 0x478>));
REBUILD_FUNCTION(HashList_T4b22a1_DeletingDestructor_321b50, 0x140321b50, (DeletingDestructor<0x140321500, 0x478>));
REBUILD_FUNCTION(List_Tb642cd_DeletingDestructor_321c40, 0x140321c40, (DeletingDestructor<0x1403215d0, 0x20>));
REBUILD_FUNCTION(List_T32d417_DeletingDestructor_321c80, 0x140321c80, (DeletingDestructor<0x140321650, 0x448>));
REBUILD_FUNCTION(Map_T46fccb_DeletingDestructor_321cc0, 0x140321cc0, (DeletingDestructor<0x1403216e0, 0x80>));
REBUILD_FUNCTION(List_T5c1afa_DeletingDestructor_327470, 0x140327470, (DeletingDestructor<0x140326e90, 0x15110>));
REBUILD_FUNCTION(HashListMap_T91beff_DeletingDestructor_327430, 0x140327430, (DeletingDestructor<0x140326e10, 0x1c050>));
REBUILD_FUNCTION(HashList_T22661b_DeletingDestructor_327310, 0x140327310, (DeletingDestructor<0x140326e10, 0x1c050>));
REBUILD_FUNCTION(SoeGems__PerformanceProfiler_DeletingDestructor_3275a0, 0x1403275a0, (DeletingDestructor<0x140327240, 0x31210>));
REBUILD_FUNCTION(Set_Te5129b_DeletingDestructor_327520, 0x140327520, (DeletingDestructor<0x140327140, 0x18>));
REBUILD_FUNCTION(Set_T84aa0b_DeletingDestructor_327560, 0x140327560, (DeletingDestructor<0x1403271b0, 0x4e60>));
REBUILD_FUNCTION(RefArray_T8f99c4_DeletingDestructor_33d6c0, 0x14033d6c0, (DeletingDestructor<0x14033cd00, 0x28>));
REBUILD_FUNCTION(RefArrayPooled_Tb62483_DeletingDestructor_33d700, 0x14033d700, (DeletingDestructor<0x14033cdd0, 0x30>));
REBUILD_FUNCTION(Map_T10b0e4_DeletingDestructor_33d620, 0x14033d620, (DeletingDestructor<0x14033cb80, 0x80>));
REBUILD_FUNCTION(RefObjectPool_Tac9a7c_DeletingDestructor_33d830, 0x14033d830, (DeletingDestructor<0x14033cdd0, 0x38>));
REBUILD_FUNCTION(GameCore__GameClientInputManager_DeletingDestructor_34ec60, 0x14034ec60, (DeletingDestructor<0x14034eae0, 0x8220>));
REBUILD_FUNCTION(Array_T0b4f4c_DeletingDestructor_5f9c40, 0x1415f9c40, (DeletingDestructor<0x1415f9ae0, 0x60>));
REBUILD_FUNCTION(Array_Tf2b739_DeletingDestructor_5fd030, 0x1415fd030, (DeletingDestructor<0x1415fcdd0, 0x30>));
REBUILD_FUNCTION(Array_T0734f0_DeletingDestructor_5fd070, 0x1415fd070, (DeletingDestructor<0x1415fce70, 0x28>));
REBUILD_FUNCTION(Array_T9a26a8_DeletingDestructor_d08e00, 0x140d08e00, (DeletingDestructor<0x140d08d60, 0x40>));
REBUILD_FUNCTION(Array_T756398_DeletingDestructor_603260, 0x141603260, (DeletingDestructor<0x141603180, 0x30>));
REBUILD_FUNCTION(IStringSecure_T948cec_DeletingDestructor_814100, 0x140814100, (DeletingDestructor<0x140810b50, 0x40>));
REBUILD_FUNCTION(IStringSecure_T250beb_DeletingDestructor_8140c0, 0x1408140c0, (DeletingDestructor<0x140810ad0, 0x30>));
REBUILD_FUNCTION(GameCommerce__UramApiRequestBase_DeletingDestructor_60ef70, 0x14160ef70, (DeletingDestructor<0x14160dcc0, 0x80>));
REBUILD_FUNCTION(GameCommerce__UramApiResponseBase_DeletingDestructor_60efb0, 0x14160efb0, (DeletingDestructor<0x14160ddd0, 0x48>));
REBUILD_FUNCTION(GameCommerce__UramApiPaymentSourceRequest_DeletingDestructor_60ece0, 0x14160ece0, (DeletingDestructor<0x14160d3f0, 0xb0>));
REBUILD_FUNCTION(GameCommerce__UramApiAddCreditCardResponse_DeletingDestructor_60e900, 0x14160e900, (DeletingDestructor<0x14160ddd0, 0x50>));
REBUILD_FUNCTION(GameCommerce__UramApiUpdateCreditCardResponse_DeletingDestructor_60f080, 0x14160f080, (DeletingDestructor<0x14160ddd0, 0x48>));
REBUILD_FUNCTION(GameCommerce__UramApiDeleteCreditCardResponse_DeletingDestructor_60ea60, 0x14160ea60, (DeletingDestructor<0x14160ddd0, 0x48>));
REBUILD_FUNCTION(GameCommerce__UramApiPreviewWalletFundRequest_DeletingDestructor_60edf0, 0x14160edf0, (DeletingDestructor<0x14160d660, 0xb8>));
REBUILD_FUNCTION(GameCommerce__UramApiFundWalletPreviewResponse_DeletingDestructor_60eb60, 0x14160eb60, (DeletingDestructor<0x14160cf60, 0x138>));
REBUILD_FUNCTION(GameCommerce__UramApiFundWalletRequest_DeletingDestructor_60eba0, 0x14160eba0, (DeletingDestructor<0x14160d080, 0x120>));
REBUILD_FUNCTION(GameCommerce__UramApiFundWalletResponse_DeletingDestructor_60ebe0, 0x14160ebe0, (DeletingDestructor<0x14160d1c0, 0x138>));
REBUILD_FUNCTION(GameCommerce__UramApiPreviewMembershipPurchaseRequest_DeletingDestructor_60edb0, 0x14160edb0, (DeletingDestructor<0x14160d4f0, 0x130>));
REBUILD_FUNCTION(GameCommerce__UramApiPurchaseMembershipPreviewResponse_DeletingDestructor_60ee30, 0x14160ee30, (DeletingDestructor<0x14160d6f0, 0x138>));
REBUILD_FUNCTION(GameCommerce__UramApiPurchaseMembershipRequest_DeletingDestructor_60ee70, 0x14160ee70, (DeletingDestructor<0x14160d880, 0x118>));
REBUILD_FUNCTION(GameCommerce__UramApiPurchaseMembershipResponse_DeletingDestructor_60eeb0, 0x14160eeb0, (DeletingDestructor<0x14160d9c0, 0x100>));
REBUILD_FUNCTION(GameCommerce__UramApiRedeemCodeRequest_DeletingDestructor_60eef0, 0x14160eef0, (DeletingDestructor<0x14160db10, 0xb8>));
REBUILD_FUNCTION(GameCommerce__UramApiRedeemCodeResponse_DeletingDestructor_60ef30, 0x14160ef30, (DeletingDestructor<0x14160dba0, 0x180>));
REBUILD_FUNCTION(GameCommerce__UramApiCreditCardTypesRequest_DeletingDestructor_60e940, 0x14160e940, (DeletingDestructor<0x14160dcc0, 0x80>));
REBUILD_FUNCTION(GameCommerce__UramApiFinalizeSteamTransactionResponse_DeletingDestructor_60eb20, 0x14160eb20, (DeletingDestructor<0x14160ddd0, 0x48>));
REBUILD_FUNCTION(GameCommerce__UramApiIsSteamCustomerRequest_DeletingDestructor_60ec20, 0x14160ec20, (DeletingDestructor<0x14160d2e0, 0xc8>));
REBUILD_FUNCTION(GameCommerce__UramApi_DeletingDestructor_60e830, 0x14160e830, (DeletingDestructor<0x14160ca90, 0x318>));
REBUILD_FUNCTION(IStringSecure_Tdb206b_DeletingDestructor_3be330, 0x1403be330, (DeletingDestructor<0x1403a6400, 0x128>));
REBUILD_FUNCTION(HashListMap_Tc95282_DeletingDestructor_615ab0, 0x141615ab0, (DeletingDestructor<0x1416152d0, 0x1270>));
REBUILD_FUNCTION(GameCommerce__CasApi_DeletingDestructor_615ca0, 0x141615ca0, (DeletingDestructor<0x141615550, 0x1590>));
REBUILD_FUNCTION(IStringSecure_Ta82633_DeletingDestructor_615b60, 0x141615b60, (DeletingDestructor<0x1416153a0, 0x428>));
REBUILD_FUNCTION(GameCommerce__BaseInGamePurchaseOrderDetail_DeletingDestructor_61e1a0, 0x14161e1a0, (DeletingDestructor<0x141628bc0, 0x60>));
REBUILD_FUNCTION(HashListMap_Td6fa56_DeletingDestructor_61e0f0, 0x14161e0f0, (DeletingDestructor<0x14161df50, 0xf0>));
REBUILD_FUNCTION(List_T829908_DeletingDestructor_61f980, 0x14161f980, (DeletingDestructor<0x14161f7d0, 0x20>));
REBUILD_FUNCTION(HashListMap_Tff06a3_DeletingDestructor_620360, 0x141620360, (DeletingDestructor<0x141620080, 0x350>));
REBUILD_FUNCTION(GameCommerce__ClientAccount_DeletingDestructor_620410, 0x141620410, (DeletingDestructor<0x1416201f0, 0x468>));
REBUILD_FUNCTION(Array_Tb2ede1_DeletingDestructor_622b60, 0x141622b60, (DeletingDestructor<0x141622560, 0x120>));
REBUILD_FUNCTION(Array_T7babba_DeletingDestructor_622ba0, 0x141622ba0, (DeletingDestructor<0x141622600, 0x60>));
REBUILD_FUNCTION(Array_T02e6f1_DeletingDestructor_622ab0, 0x141622ab0, (DeletingDestructor<0x141622460, 0x60>));
REBUILD_FUNCTION(Array_T705254_DeletingDestructor_72f2c0, 0x14072f2c0, (DeletingDestructor<0x14072e550, 0x220>));
REBUILD_FUNCTION(GameCommerce__MarketingDataSource_DeletingDestructor_622d70, 0x141622d70, (DeletingDestructor<0x141622950, 0xbe1b0>));
REBUILD_FUNCTION(GameCommerce__MarketingDataUpdater_DeletingDestructor_626f80, 0x141626f80, (DeletingDestructor<0x141626e70, 0x3b0>));
REBUILD_FUNCTION(GameCommerce__MarketingDataSourceFlatFileLoader_DeletingDestructor_6281e0, 0x1416281e0, (DeletingDestructor<0x1416280e0, 0x258>));
REBUILD_FUNCTION(Array_Ta7356d_DeletingDestructor_629ec0, 0x141629ec0, (DeletingDestructor<0x141629a10, 0xa0>));
REBUILD_FUNCTION(HashListSet_T18ccb9_DeletingDestructor_64d460, 0x14164d460, (DeletingDestructor<0x14164c890, 0x30>));
REBUILD_FUNCTION(HashListMap_Te35252_DeletingDestructor_64d3b0, 0x14164d3b0, (DeletingDestructor<0x14164c7d0, 0x30>));
REBUILD_FUNCTION(RefObjectPool_Tfded9b_DeletingDestructor_64d670, 0x14164d670, (DeletingDestructor<0x14033cd00, 0x30>));
REBUILD_FUNCTION(Resource__Manager_DeletingDestructor_64d630, 0x14164d630, (DeletingDestructor<0x14164ce50, 0xb0570>));
REBUILD_FUNCTION(Array_T4514a5_DeletingDestructor_64d1b0, 0x14164d1b0, (DeletingDestructor<0x14164c490, 0x4020>));
REBUILD_FUNCTION(Array_Tc4d671_DeletingDestructor_64d170, 0x14164d170, (DeletingDestructor<0x14164c3f0, 0x420>));
REBUILD_FUNCTION(Array_T912c7d_DeletingDestructor_64d260, 0x14164d260, (DeletingDestructor<0x14164c590, 0x420>));
REBUILD_FUNCTION(Array_Tb081a1_DeletingDestructor_65a490, 0x14165a490, (DeletingDestructor<0x14165a6e0, 0x220>));
REBUILD_FUNCTION(Map_T05c67a_DeletingDestructor_65f300, 0x14165f300, (DeletingDestructor<0x14165ecf0, 0x18>));
REBUILD_FUNCTION(Map_T999a32_DeletingDestructor_65f340, 0x14165f340, (DeletingDestructor<0x14165ed60, 0x5840>));
REBUILD_FUNCTION(List_Ta1e857_DeletingDestructor_65f270, 0x14165f270, (DeletingDestructor<0x14165ec40, 0x20>));
REBUILD_FUNCTION(HashListMap_T329b28_DeletingDestructor_65f1b0, 0x14165f1b0, (DeletingDestructor<0x14165eb70, 0x30>));
REBUILD_FUNCTION(RefObjectPool_Tc683aa_DeletingDestructor_66dbd0, 0x14166dbd0, (DeletingDestructor<0x141672190, 0x148>));
REBUILD_FUNCTION(TaskManagement__TaskManager_DeletingDestructor_66dcb0, 0x14166dcb0, (DeletingDestructor<0x14166d6f0, 0x60b28>));
REBUILD_FUNCTION(Array_T297d7e_DeletingDestructor_674450, 0x141674450, (DeletingDestructor<0x141673cf0, 0x40>));
REBUILD_FUNCTION(TaskManagement__SerialTaskNode_DeletingDestructor_674880, 0x141674880, (DeletingDestructor<0x141678a10, 0x68>));
REBUILD_FUNCTION(TaskManagement__SerialTaskGroup_DeletingDestructor_678aa0, 0x141678aa0, (DeletingDestructor<0x141678980, 0x98>));
REBUILD_FUNCTION(HashListMap_T7bdc6e_DeletingDestructor_67aa10, 0x14167aa10, (DeletingDestructor<0x14167a200, 0x9c10>));
REBUILD_FUNCTION(HashList_T363f84_DeletingDestructor_67a9d0, 0x14167a9d0, (DeletingDestructor<0x14167a200, 0x9c10>));
REBUILD_FUNCTION(DataManagement__DynamicDataEvent_DeletingDestructor_682640, 0x141682640, (DeletingDestructor<0x14167f820, 0x458>));
REBUILD_FUNCTION(CStaticArray_T607dfd_DeletingDestructor_8084c0, 0x1418084c0, (DeletingDestructor<0x141802070, 0x34>));
REBUILD_FUNCTION(CStaticArray_T02f84f_DeletingDestructor_e66540, 0x141e66540, (DeletingDestructor<0x141e66400, 0x34>));
REBUILD_FUNCTION(CStaticArray_T9c8ac5_DeletingDestructor_e6f0f0, 0x141e6f0f0, (DeletingDestructor<0x141e6efa0, 0x34>));
REBUILD_FUNCTION(HashListMap_Te041eb_DeletingDestructor_eb6050, 0x141eb6050, (DeletingDestructor<0x141eb58e0, 0x1a050>));
REBUILD_FUNCTION(HashListSet_T9c78e1_DeletingDestructor_eb6100, 0x141eb6100, (DeletingDestructor<0x141eb59b0, 0xc050>));
REBUILD_FUNCTION(Array_Tb71391_DeletingDestructor_ebfbf0, 0x141ebfbf0, (DeletingDestructor<0x141ebf990, 0x40>));
REBUILD_FUNCTION(Array_T6695d6_DeletingDestructor_77eb90, 0x14077eb90, (DeletingDestructor<0x14077e0f0, 0x38>));
REBUILD_FUNCTION(DataManagement__FlatFileLineData_DeletingDestructor_ec2e60, 0x141ec2e60, (DeletingDestructor<0x141710630, 0xc0>));
REBUILD_FUNCTION(DataManagement__FlatFileDataLoader_DeletingDestructor_ec2e20, 0x141ec2e20, (DeletingDestructor<0x141710540, 0x128>));

namespace rebuild::game_misc {
// Set the class vtable, run the base/member destructor, sized delete.
template <uint64_t Vtable, uint64_t Destructor, size_t Size>
void* VtableDeletingDestructor(void* self, unsigned flags) {
  *static_cast<uint64_t*>(self) = Vtable;
  game::Call<void (*)(void*)>(Destructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// Set the class vtable, free the pointer field at Offset, drop to the base
// vtable, sized delete (std locale facets and similar).
template <uint64_t Vtable, size_t Offset, uint64_t Free, uint64_t BaseVtable, size_t Size>
void* FieldDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*)>(Free)(*reinterpret_cast<void**>(bytes + Offset));
  *reinterpret_cast<uint64_t*>(bytes) = BaseVtable;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

// Generated lists.
REBUILD_FUNCTION(HashListMap_T6800c7_DeletingDestructor_140306040, 0x140306040, (VtableDeletingDestructor<0x142049bc8, 0x140309670, 0x4028>));
REBUILD_FUNCTION(List_T5e5512_DeletingDestructor_14030cc00, 0x14030cc00, (VtableDeletingDestructor<0x14204ae30, 0x14030f960, 0x20>));
REBUILD_FUNCTION(HashMap_Tc1bfb8_DeletingDestructor_14030ca30, 0x14030ca30, (VtableDeletingDestructor<0x14204aec0, 0x14030f8e0, 0x1018>));
REBUILD_FUNCTION(Hash_Tc1c96b_DeletingDestructor_14030c9e0, 0x14030c9e0, (VtableDeletingDestructor<0x14204aec0, 0x14030f8e0, 0x1018>));
REBUILD_FUNCTION(List_T009099_DeletingDestructor_1403158e0, 0x1403158e0, (VtableDeletingDestructor<0x14204bac8, 0x140316ed0, 0x20>));
REBUILD_FUNCTION(List_T4aea37_DeletingDestructor_1403319f0, 0x1403319f0, (VtableDeletingDestructor<0x14204f510, 0x140334af0, 0x20>));
REBUILD_FUNCTION(List_T847fe1_DeletingDestructor_140331b20, 0x140331b20, (VtableDeletingDestructor<0x14204f560, 0x140334b80, 0x20>));
REBUILD_FUNCTION(HashListMap_T9d73be_DeletingDestructor_14034a2e0, 0x14034a2e0, (VtableDeletingDestructor<0x142052430, 0x14034aaa0, 0x2028>));
REBUILD_FUNCTION(HashList_T1bcb1f_DeletingDestructor_14034a290, 0x14034a290, (VtableDeletingDestructor<0x142052430, 0x14034aaa0, 0x2028>));
REBUILD_FUNCTION(List_T02c89f_DeletingDestructor_14034ec10, 0x14034ec10, (VtableDeletingDestructor<0x142052f98, 0x140351290, 0x20>));
REBUILD_FUNCTION(List_Tdc1615_DeletingDestructor_1403beb90, 0x1403beb90, (VtableDeletingDestructor<0x1424b0380, 0x140453600, 0x20>));
REBUILD_FUNCTION(List_T8fae42_DeletingDestructor_14160e790, 0x14160e790, (VtableDeletingDestructor<0x1424b9f20, 0x141613a90, 0x20>));
REBUILD_FUNCTION(List_T893ed4_DeletingDestructor_14160e6f0, 0x14160e6f0, (VtableDeletingDestructor<0x1424b9f48, 0x141613980, 0x20>));
REBUILD_FUNCTION(List_T375663_DeletingDestructor_14160e740, 0x14160e740, (VtableDeletingDestructor<0x1424b9f70, 0x141613a00, 0x20>));
REBUILD_FUNCTION(List_Tece4be_DeletingDestructor_14160e650, 0x14160e650, (VtableDeletingDestructor<0x1424ba000, 0x141613810, 0x20>));
REBUILD_FUNCTION(List_T0abc9f_DeletingDestructor_14160e7e0, 0x14160e7e0, (VtableDeletingDestructor<0x1424ba230, 0x141613b40, 0x20>));
REBUILD_FUNCTION(List_T1ea3e7_DeletingDestructor_14160e6a0, 0x14160e6a0, (VtableDeletingDestructor<0x1424ba340, 0x141613890, 0x20>));
REBUILD_FUNCTION(List_T21c4ac_DeletingDestructor_14160e600, 0x14160e600, (VtableDeletingDestructor<0x1424ba4a8, 0x141613790, 0x20>));
REBUILD_FUNCTION(List_T65a21e_DeletingDestructor_141615c10, 0x141615c10, (VtableDeletingDestructor<0x1424bb940, 0x141618eb0, 0x20>));
REBUILD_FUNCTION(List_T172ea8_DeletingDestructor_14161a540, 0x14161a540, (VtableDeletingDestructor<0x1424bc110, 0x140832150, 0x20>));
REBUILD_FUNCTION(List_Tf73bdd_DeletingDestructor_14161a600, 0x14161a600, (VtableDeletingDestructor<0x1424bc160, 0x1408321d0, 0x20>));
REBUILD_FUNCTION(HashListMap_T6529a4_DeletingDestructor_14161d500, 0x14161d500, (VtableDeletingDestructor<0x1424bc4c0, 0x140831ba0, 0x68>));
REBUILD_FUNCTION(HashListMap_T05c9dc_DeletingDestructor_140628600, 0x140628600, (VtableDeletingDestructor<0x1424bebd8, 0x140636020, 0x68>));
REBUILD_FUNCTION(List_T4c9be1_DeletingDestructor_1406288a0, 0x1406288a0, (VtableDeletingDestructor<0x1424bec10, 0x140636390, 0x20>));
REBUILD_FUNCTION(List_T57d626_DeletingDestructor_1406287e0, 0x1406287e0, (VtableDeletingDestructor<0x1424bec60, 0x140636300, 0x20>));
REBUILD_FUNCTION(CLZInWindow_DeletingDestructor_141642010, 0x141642010, (VtableDeletingDestructor<0x1424c08e8, 0x14164a590, 0x40>));
REBUILD_FUNCTION(IMatchFinder_DeletingDestructor_141642360, 0x141642360, (VtableDeletingDestructor<0x1424c08e8, 0x14164a590, 0x48>));
REBUILD_FUNCTION(HashListMap_T6ec29d_DeletingDestructor_14164d310, 0x14164d310, (VtableDeletingDestructor<0x1424c14c8, 0x1416558d0, 0x428>));
REBUILD_FUNCTION(HashListMap_T7d2c72_DeletingDestructor_14164d360, 0x14164d360, (VtableDeletingDestructor<0x1424c1500, 0x141655970, 0x428>));
REBUILD_FUNCTION(HashMap_Tcb7f7a_DeletingDestructor_14164d520, 0x14164d520, (VtableDeletingDestructor<0x1424c1538, 0x141655aa0, 0x418>));
REBUILD_FUNCTION(HashMap_T9e1737_DeletingDestructor_14164d570, 0x14164d570, (VtableDeletingDestructor<0x1424c1608, 0x141655b20, 0x8018>));
REBUILD_FUNCTION(HashMap_Tc45ed6_DeletingDestructor_14165a520, 0x14165a520, (VtableDeletingDestructor<0x1424c1d98, 0x14165a8d0, 0x418>));
REBUILD_FUNCTION(HashMap_T3f5c54_DeletingDestructor_14165a1f0, 0x14165a1f0, (VtableDeletingDestructor<0x1424c1db8, 0x14165a7f0, 0x418>));
REBUILD_FUNCTION(Map_Tc52974_DeletingDestructor_14165f2b0, 0x14165f2b0, (VtableDeletingDestructor<0x1424c4220, 0x1416659e0, 0x18>));
REBUILD_FUNCTION(HashMap_T3478d2_DeletingDestructor_14166dae0, 0x14166dae0, (VtableDeletingDestructor<0x1424c5420, 0x1416708f0, 0x10018>));
REBUILD_FUNCTION(HashMap_T105bd5_DeletingDestructor_14166da90, 0x14166da90, (VtableDeletingDestructor<0x1424c5440, 0x141670830, 0x10018>));
REBUILD_FUNCTION(HashListMap_Tf44332_DeletingDestructor_141672260, 0x141672260, (VtableDeletingDestructor<0x1424c5578, 0x141672e30, 0x68>));
REBUILD_FUNCTION(HashMap_Tbea0a3_DeletingDestructor_141674780, 0x141674780, (VtableDeletingDestructor<0x1424c5ba8, 0x1416774e0, 0x2018>));
REBUILD_FUNCTION(HashMap_Tf4ddbf_DeletingDestructor_141674730, 0x141674730, (VtableDeletingDestructor<0x1424c5bc8, 0x141677460, 0x2018>));
REBUILD_FUNCTION(HashListMap_T9b0c64_DeletingDestructor_14167f950, 0x14167f950, (VtableDeletingDestructor<0x1424c6e70, 0x141681a60, 0x228>));
REBUILD_FUNCTION(HashListMap_T86526f_DeletingDestructor_14167fa10, 0x14167fa10, (VtableDeletingDestructor<0x1424c6f10, 0x141681b00, 0x348>));
REBUILD_FUNCTION(Map_Ta14c65_DeletingDestructor_141e93dd0, 0x141e93dd0, (VtableDeletingDestructor<0x1425a7b78, 0x141e94e80, 0x18>));
REBUILD_FUNCTION(List_T405eb1_DeletingDestructor_141eb6140, 0x141eb6140, (VtableDeletingDestructor<0x1425ab878, 0x141eb9e70, 0x20>));
REBUILD_FUNCTION(time_put_T5f78c5_DeletingDestructor_141914220, 0x141914220, (FieldDeletingDestructor<0x142525cb8, 0x10, 0x140d42428, 0x142520e38, 0x18>));
REBUILD_FUNCTION(collate_T7306a0_DeletingDestructor_141913bf8, 0x141913bf8, (FieldDeletingDestructor<0x142525e00, 0x18, 0x140d42428, 0x142520e38, 0x20>));
REBUILD_FUNCTION(collate_T4a899a_DeletingDestructor_141913bac, 0x141913bac, (FieldDeletingDestructor<0x142526180, 0x18, 0x140d42428, 0x142520e38, 0x20>));
REBUILD_FUNCTION(time_put_T8348f0_DeletingDestructor_1419141d4, 0x1419141d4, (FieldDeletingDestructor<0x1425263e0, 0x10, 0x140d42428, 0x142520e38, 0x18>));
REBUILD_FUNCTION(collate_Ta29307_DeletingDestructor_14192ccb0, 0x14192ccb0, (FieldDeletingDestructor<0x142526bc0, 0x18, 0x140d42428, 0x142520e38, 0x20>));
REBUILD_FUNCTION(time_put_Td2fe53_DeletingDestructor_14192ceb0, 0x14192ceb0, (FieldDeletingDestructor<0x142526e20, 0x10, 0x140d42428, 0x142520e38, 0x18>));
REBUILD_FUNCTION(CaptureCommon__Buffer_DeletingDestructor_14194ede0, 0x14194ede0, (FieldDeletingDestructor<0x14252d738, 0x18, 0x1402fc170, 0x14252d690, 0x30>));
