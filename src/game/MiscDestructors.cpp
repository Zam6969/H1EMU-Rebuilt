// Scalar deleting destructors (vtable slot 0) across the engine's utility,
// asset, audio-hook, commerce, task and third-party classes, rebuilt from
// two templates:
//   - member-less bases: reset the vtable, then sized delete if (flags & 1);
//   - classes with a destructor: call it, then sized delete if (flags & 1).
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

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

namespace rebuild::game_misc {
// Intrusive list / hash-list containers: set the vtable, unlink every node
// from the head (+0x10) through the class's remove (Remove(self, node)),
// sized delete.
template <uint64_t Vtable, uint64_t Remove, size_t Size>
void* ListDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  while (void* head = *reinterpret_cast<void**>(bytes + 0x10)) game::Call<void (*)(void*, void*)>(Remove)(self, head);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// SoeUtil::StringFixed<N> and friends: release through the interface vtable,
// leave the string empty with the plain IString vtable, sized delete.
template <uint64_t InterfaceVtable, uint64_t PlainVtable, size_t Size>
void* FixedStringDeletingDestructor(void* self, unsigned flags) {
  auto* text = static_cast<soeutil::IString*>(self);
  text->vtable = reinterpret_cast<void**>(InterfaceVtable);
  soeutil::StringRelease(text);
  text->data = soeutil::EmptyStringData();
  *reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(self) + 0x10) = 0;
  text->vtable = reinterpret_cast<void**>(PlainVtable);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(HashListMap_T3a396d_DeletingDestructor_140306140, 0x140306140, (ListDeletingDestructor<0x142049c38, 0x140309580, 0x128>));
REBUILD_FUNCTION(HashListMap_Te63d1d_DeletingDestructor_140311150, 0x140311150, (ListDeletingDestructor<0x14204b350, 0x140312790, 0x10028>));
REBUILD_FUNCTION(HashListMap_Tb50654_DeletingDestructor_140321bd0, 0x140321bd0, (ListDeletingDestructor<0x14204d300, 0x140324bf0, 0x3b0>));
REBUILD_FUNCTION(HashListMap_T74c50b_DeletingDestructor_1403273c0, 0x1403273c0, (ListDeletingDestructor<0x14204e5a8, 0x14032b8e0, 0x4028>));
REBUILD_FUNCTION(HashListMap_T89d71d_DeletingDestructor_14160e520, 0x14160e520, (ListDeletingDestructor<0x1424ba470, 0x1416132c0, 0xa8>));
REBUILD_FUNCTION(HashListMap_Tfbe090_DeletingDestructor_141615af0, 0x141615af0, (ListDeletingDestructor<0x1424bb990, 0x141618d20, 0xa8>));
REBUILD_FUNCTION(HashListMap_Tdf3b05_DeletingDestructor_14161e130, 0x14161e130, (ListDeletingDestructor<0x1424bc718, 0x14161f080, 0x48>));
REBUILD_FUNCTION(HashListMap_Tbfc69a_DeletingDestructor_1416203a0, 0x1416203a0, (ListDeletingDestructor<0x1424bc9f0, 0x140830380, 0x68>));
REBUILD_FUNCTION(HashListMap_T5181ba_DeletingDestructor_141622be0, 0x141622be0, (ListDeletingDestructor<0x1424bcff8, 0x1416258d0, 0x128>));
REBUILD_FUNCTION(HashListMap_T79c627_DeletingDestructor_141629fe0, 0x141629fe0, (ListDeletingDestructor<0x1424bdd68, 0x14162b6b0, 0xa8>));
REBUILD_FUNCTION(HashList_T315445_DeletingDestructor_141629f00, 0x141629f00, (ListDeletingDestructor<0x1424bdd68, 0x14162b6b0, 0xa8>));
REBUILD_FUNCTION(HashListMap_Tbdda20_DeletingDestructor_14162a050, 0x14162a050, (ListDeletingDestructor<0x1424bde28, 0x14162b7a0, 0x128>));
REBUILD_FUNCTION(HashList_T77bc25_DeletingDestructor_141629f70, 0x141629f70, (ListDeletingDestructor<0x1424bde28, 0x14162b7a0, 0x128>));
REBUILD_FUNCTION(HashListMap_T195568_DeletingDestructor_14162f460, 0x14162f460, (ListDeletingDestructor<0x1424be860, 0x140b0b280, 0x68>));
REBUILD_FUNCTION(HashListMap_T969c1e_DeletingDestructor_14162f3f0, 0x14162f3f0, (ListDeletingDestructor<0x1424be898, 0x140bbea70, 0x428>));
REBUILD_FUNCTION(HashListMap_Tf0d8a0_DeletingDestructor_140628410, 0x140628410, (ListDeletingDestructor<0x1424beb28, 0x140635900, 0x2028>));
REBUILD_FUNCTION(HashList_T90e688_DeletingDestructor_140bb9650, 0x140bb9650, (ListDeletingDestructor<0x1424beb28, 0x140635900, 0x2028>));
REBUILD_FUNCTION(HashListMap_Ta1c9ee_DeletingDestructor_140628480, 0x140628480, (ListDeletingDestructor<0x1424becb0, 0x1406359c0, 0x2028>));
REBUILD_FUNCTION(HashList_T931442_DeletingDestructor_141631a90, 0x141631a90, (ListDeletingDestructor<0x1424becb0, 0x1406359c0, 0x2028>));
REBUILD_FUNCTION(HashListMap_T9dd27b_DeletingDestructor_14072f3e0, 0x14072f3e0, (ListDeletingDestructor<0x1424bfb80, 0x140732b50, 0x128>));
REBUILD_FUNCTION(HashList_Tf968c1_DeletingDestructor_14072f300, 0x14072f300, (ListDeletingDestructor<0x1424bfb80, 0x140732b50, 0x128>));
REBUILD_FUNCTION(HashListSet_T4f50f9_DeletingDestructor_14164d3f0, 0x14164d3f0, (ListDeletingDestructor<0x1424c1470, 0x1416555a0, 0x4028>));
REBUILD_FUNCTION(HashListMap_Ta2c88d_DeletingDestructor_14164d2a0, 0x14164d2a0, (ListDeletingDestructor<0x1424c1558, 0x141655310, 0x428>));
REBUILD_FUNCTION(HashListMap_Tbf997b_DeletingDestructor_14166da20, 0x14166da20, (ListDeletingDestructor<0x1424c53b0, 0x141670730, 0x428>));
REBUILD_FUNCTION(HashListMap_T936ef1_DeletingDestructor_141674650, 0x141674650, (ListDeletingDestructor<0x1424c5a68, 0x141677160, 0x2028>));
REBUILD_FUNCTION(HashListMap_Tef78bd_DeletingDestructor_1416745e0, 0x1416745e0, (ListDeletingDestructor<0x1424c5aa0, 0x1416770a0, 0x2028>));
REBUILD_FUNCTION(HashListMap_T3eff8c_DeletingDestructor_1416746c0, 0x1416746c0, (ListDeletingDestructor<0x1424c5ad8, 0x141677220, 0x2028>));
REBUILD_FUNCTION(HashListMap_T92c0b6_DeletingDestructor_141674570, 0x141674570, (ListDeletingDestructor<0x1424c5b10, 0x141676fe0, 0x2028>));
REBUILD_FUNCTION(HashList_Tba550f_DeletingDestructor_141674500, 0x141674500, (ListDeletingDestructor<0x1424c5b10, 0x141676fe0, 0x2028>));
REBUILD_FUNCTION(HashListMap_T0ffc4d_DeletingDestructor_14167aa50, 0x14167aa50, (ListDeletingDestructor<0x1424c63c8, 0x14167d980, 0x1028>));
REBUILD_FUNCTION(HashListMap_T9fc61e_DeletingDestructor_141eb5f70, 0x141eb5f70, (ListDeletingDestructor<0x1425ab808, 0x141eb9b70, 0x8028>));
REBUILD_FUNCTION(HashList_Td40ee4_DeletingDestructor_141eb5f00, 0x141eb5f00, (ListDeletingDestructor<0x1425ab808, 0x141eb9b70, 0x8028>));
REBUILD_FUNCTION(HashListMap_T267dfe_DeletingDestructor_141eb5fe0, 0x141eb5fe0, (ListDeletingDestructor<0x1425ab8c8, 0x141eb9c40, 0x10028>));
REBUILD_FUNCTION(HashListSet_Td00d30_DeletingDestructor_141eb6090, 0x141eb6090, (ListDeletingDestructor<0x1425ab938, 0x141eb9ce0, 0x4028>));

namespace rebuild::game_misc {
// SoeUtil::Array<T>: set the vtable, zero the count (+0x10), free the data
// (+8) through the thread allocator (0x14032f980) or operator delete[]
// (0x1402fc170), sized delete.
template <uint64_t Vtable, size_t Size>
void* ArrayDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<int*>(bytes + 0x10) = 0;
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  void* data = *reinterpret_cast<void**>(bytes + 8);
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    game::Call<void (*)(void*)>(0x1402fc170)(data);
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, 1);
  *reinterpret_cast<void**>(bytes + 8) = nullptr;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// Derived container with an embedded member: derived vtable + clear,
// member destructor at Offset, base vtable + clear, sized delete.
template <uint64_t Vtable, uint64_t Clear, size_t Offset, uint64_t MemberDestructor, uint64_t BaseVtable, uint64_t BaseClear, size_t Size>
void* TwoPartDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*)>(Clear)(self);
  game::Call<void (*)(void*)>(MemberDestructor)(bytes + Offset);
  *reinterpret_cast<uint64_t*>(bytes) = BaseVtable;
  game::Call<void (*)(void*)>(BaseClear)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(HashListMap_T12ad28_DeletingDestructor_140306090, 0x140306090, (TwoPartDeletingDestructor<0x142049c00, 0x140309670, 0x4028, 0x140305c50, 0x142049bc8, 0x140309670, 0x15050>));
REBUILD_FUNCTION(List_T939563_DeletingDestructor_14030cc50, 0x14030cc50, (TwoPartDeletingDestructor<0x14204ae58, 0x14030f960, 0x20, 0x14030c250, 0x14204ae30, 0x14030f960, 0xcc48>));
REBUILD_FUNCTION(HashList_Tf760ad_DeletingDestructor_140327350, 0x140327350, (TwoPartDeletingDestructor<0x142049c00, 0x140309670, 0x4028, 0x140305c50, 0x142049bc8, 0x140309670, 0x15050>));
REBUILD_FUNCTION(List_Tc7ea38_DeletingDestructor_140331a40, 0x140331a40, (TwoPartDeletingDestructor<0x14204f538, 0x140334af0, 0x20, 0x1403315c0, 0x14204f510, 0x140334af0, 0xdc8>));
REBUILD_FUNCTION(List_T113ed6_DeletingDestructor_140331ab0, 0x140331ab0, (TwoPartDeletingDestructor<0x14204f588, 0x140334b80, 0x20, 0x140331660, 0x14204f560, 0x140334b80, 0xe80>));
REBUILD_FUNCTION(List_T458a4b_DeletingDestructor_14160e590, 0x14160e590, (TwoPartDeletingDestructor<0x1424ba4d0, 0x141613790, 0x20, 0x1403218d0, 0x1424ba4a8, 0x141613790, 0x88>));
REBUILD_FUNCTION(List_Tbb2000_DeletingDestructor_141615ba0, 0x141615ba0, (TwoPartDeletingDestructor<0x1424bb968, 0x141618eb0, 0x20, 0x1403218d0, 0x1424bb940, 0x141618eb0, 0x88>));
REBUILD_FUNCTION(List_T27bc43_DeletingDestructor_14161a4d0, 0x14161a4d0, (TwoPartDeletingDestructor<0x1424bc138, 0x140832150, 0x20, 0x14161a100, 0x1424bc110, 0x140832150, 0xa0>));
REBUILD_FUNCTION(List_T0fde3f_DeletingDestructor_14161a590, 0x14161a590, (TwoPartDeletingDestructor<0x1424bc188, 0x1408321d0, 0x20, 0x14161a060, 0x1424bc160, 0x1408321d0, 0x68>));
REBUILD_FUNCTION(List_T159bf8_DeletingDestructor_1406288f0, 0x1406288f0, (TwoPartDeletingDestructor<0x1424bec38, 0x140636390, 0x20, 0x140624f50, 0x1424bec10, 0x140636390, 0x40>));
REBUILD_FUNCTION(List_T0ed63f_DeletingDestructor_140628830, 0x140628830, (TwoPartDeletingDestructor<0x1424bec88, 0x140636300, 0x20, 0x140624e10, 0x1424bec60, 0x140636300, 0x40>));
REBUILD_FUNCTION(HashListMap_Tc841fc_DeletingDestructor_14167f9a0, 0x14167f9a0, (TwoPartDeletingDestructor<0x1424c6ea8, 0x141681a60, 0x228, 0x14167f780, 0x1424c6e70, 0x141681a60, 0x430>));
REBUILD_FUNCTION(HashList_T2114ac_DeletingDestructor_1416825d0, 0x1416825d0, (TwoPartDeletingDestructor<0x1424c6ea8, 0x141681a60, 0x228, 0x14167f780, 0x1424c6e70, 0x141681a60, 0x430>));
REBUILD_FUNCTION(List_T66a935_DeletingDestructor_141eb6190, 0x141eb6190, (TwoPartDeletingDestructor<0x1425ab8a0, 0x141eb9e70, 0x20, 0x1403a7930, 0x1425ab878, 0x141eb9e70, 0x6048>));
REBUILD_FUNCTION(Array_T8ff76d_DeletingDestructor_14030c930, 0x14030c930, (ArrayDeletingDestructor<0x14204adc8, 0x18>));
REBUILD_FUNCTION(Array_Tbdc477_DeletingDestructor_141622af0, 0x141622af0, (ArrayDeletingDestructor<0x1424bcfa8, 0x18>));
REBUILD_FUNCTION(Array_Tdc6556_DeletingDestructor_1403b67b0, 0x1403b67b0, (ArrayDeletingDestructor<0x1424bd030, 0x18>));
REBUILD_FUNCTION(Array_T588403_DeletingDestructor_140ac3710, 0x140ac3710, (ArrayDeletingDestructor<0x1424bd0b8, 0x18>));
REBUILD_FUNCTION(Array_T67e985_DeletingDestructor_141629e50, 0x141629e50, (ArrayDeletingDestructor<0x1424bddd8, 0x18>));
REBUILD_FUNCTION(Array_Tf14bc3_DeletingDestructor_14164d090, 0x14164d090, (ArrayDeletingDestructor<0x1424c1628, 0x18>));
REBUILD_FUNCTION(Array_Tfe86e4_DeletingDestructor_14164d100, 0x14164d100, (ArrayDeletingDestructor<0x1424c1820, 0x18>));
REBUILD_FUNCTION(Array_T659cd7_DeletingDestructor_14164d1f0, 0x14164d1f0, (ArrayDeletingDestructor<0x1424c1b48, 0x18>));
REBUILD_FUNCTION(Array_T82f82e_DeletingDestructor_14165a340, 0x14165a340, (ArrayDeletingDestructor<0x1424c1d48, 0x18>));
REBUILD_FUNCTION(Array_T14d15e_DeletingDestructor_141668cc0, 0x141668cc0, (ArrayDeletingDestructor<0x1424c4a48, 0x18>));
REBUILD_FUNCTION(Array_T043ee0_DeletingDestructor_14166d9b0, 0x14166d9b0, (ArrayDeletingDestructor<0x1424c5338, 0x18>));
REBUILD_FUNCTION(Array_Tae3129_DeletingDestructor_141674490, 0x141674490, (ArrayDeletingDestructor<0x1424c59b8, 0x18>));
REBUILD_FUNCTION(Array_Tb50686_DeletingDestructor_1416743e0, 0x1416743e0, (ArrayDeletingDestructor<0x1424c5b80, 0x18>));
REBUILD_FUNCTION(Array_T968ad7_DeletingDestructor_14194ed70, 0x14194ed70, (ArrayDeletingDestructor<0x14252d798, 0x18>));
REBUILD_FUNCTION(Array_Tfe35cd_DeletingDestructor_141ebfc30, 0x141ebfc30, (ArrayDeletingDestructor<0x1425acd28, 0x18>));

namespace rebuild::game_misc {
// MSVC std::basic_string<Ch> (0x20 bytes): small buffer/pointer +0, size
// +0x10, capacity +0x18.
template <typename Ch>
struct StdString {
  union {
    Ch buffer[16 / sizeof(Ch)];
    Ch* pointer;
  };
  size_t size;
  size_t capacity;
};
static_assert(offsetof(StdString<char>, size) == 0x10 && offsetof(StdString<wchar_t>, capacity) == 0x18 && sizeof(StdString<char>) == 0x20);

// std::exception family (what/doFree at +8): reset the vtable, free the
// message (__std_exception_destroy 0x140d149f0), sized delete.
template <uint64_t Vtable, size_t Size>
void* ExceptionDeletingDestructor(void* self, unsigned flags) {
  *static_cast<uint64_t*>(self) = Vtable;
  game::Call<void (*)(void*)>(0x140d149f0)(static_cast<uint8_t*>(self) + 8);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// numpunct / _Mpunct string getters (do_grouping, do_falsename,
// do_curr_symbol...): construct an empty string in the return slot and
// assign the stored C string at Offset.
template <size_t Offset, typename Ch, uint64_t Assign>
StdString<Ch>* PunctGetString(void* self, StdString<Ch>* out) {
  const Ch* text = *reinterpret_cast<const Ch**>(static_cast<uint8_t*>(self) + Offset);
  out->capacity = 16 / sizeof(Ch) - 1;
  out->size = 0;
  out->buffer[0] = 0;
  size_t length = 0;
  while (text[length]) ++length;
  game::Call<void (*)(StdString<Ch>*, const Ch*, size_t)>(Assign)(out, text, length);
  return out;
}
// _Mpunct / moneypunct: free the four cached strings (+0x10, +0x20, +0x28,
// +0x30) with free (0x140d42428), restore the locale::facet vtable, sized
// delete.
template <uint64_t Vtable, uint64_t FacetVtable, size_t Size>
void* MpunctDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  for (size_t offset : {0x10, 0x20, 0x28, 0x30}) game::Call<void (*)(void*)>(0x140d42428)(*reinterpret_cast<void**>(bytes + offset));
  *reinterpret_cast<uint64_t*>(bytes) = FacetVtable;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(std__exception_DeletingDestructor_1402ba7d0, 0x1402ba7d0, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__bad_alloc_DeletingDestructor_140d0f70c, 0x140d0f70c, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__logic_error_DeletingDestructor_140d0f81c, 0x140d0f81c, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__invalid_argument_DeletingDestructor_140d0f794, 0x140d0f794, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__length_error_DeletingDestructor_140d0f7d8, 0x140d0f7d8, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__out_of_range_DeletingDestructor_140d0f860, 0x140d0f860, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__runtime_error_DeletingDestructor_140d0f92c, 0x140d0f92c, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__overflow_error_DeletingDestructor_140d0f8a4, 0x140d0f8a4, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__bad_function_call_DeletingDestructor_140d0f750, 0x140d0f750, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__regex_error_DeletingDestructor_140d0f8e8, 0x140d0f8e8, (ExceptionDeletingDestructor<0x1421f3c70, 0x20>));
REBUILD_FUNCTION(std__bad_exception_DeletingDestructor_140d12600, 0x140d12600, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(std__bad_cast_DeletingDestructor_141909e2c, 0x141909e2c, (ExceptionDeletingDestructor<0x1421f3c70, 0x18>));
REBUILD_FUNCTION(numpunct_T322acf_GetString_1419262f8, 0x1419262f8, (PunctGetString<0x10, char, 0x140783e70>));
REBUILD_FUNCTION(numpunct_T322acf_GetString_141921ac4, 0x141921ac4, (PunctGetString<0x20, wchar_t, 0x14192157c>));
REBUILD_FUNCTION(numpunct_T322acf_GetString_141929294, 0x141929294, (PunctGetString<0x28, wchar_t, 0x14192157c>));
REBUILD_FUNCTION(_Mpunct_Teaba18_GetString_141926260, 0x141926260, (PunctGetString<0x10, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_Teaba18_GetString_1419219f4, 0x1419219f4, (PunctGetString<0x20, wchar_t, 0x14192157c>));
REBUILD_FUNCTION(_Mpunct_Teaba18_GetString_141926cd4, 0x141926cd4, (PunctGetString<0x28, wchar_t, 0x14192157c>));
REBUILD_FUNCTION(_Mpunct_Teaba18_GetString_1419269f4, 0x1419269f4, (PunctGetString<0x30, wchar_t, 0x14192157c>));
REBUILD_FUNCTION(numpunct_T274ceb_GetString_1419262ac, 0x1419262ac, (PunctGetString<0x10, char, 0x140783e70>));
REBUILD_FUNCTION(numpunct_T274ceb_GetString_141921a74, 0x141921a74, (PunctGetString<0x20, wchar_t, 0x1419211d4>));
REBUILD_FUNCTION(numpunct_T274ceb_GetString_141929244, 0x141929244, (PunctGetString<0x28, wchar_t, 0x1419211d4>));
REBUILD_FUNCTION(_Mpunct_T2fcdf3_GetString_141926214, 0x141926214, (PunctGetString<0x10, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_T2fcdf3_GetString_1419219a4, 0x1419219a4, (PunctGetString<0x20, wchar_t, 0x1419211d4>));
REBUILD_FUNCTION(_Mpunct_T2fcdf3_GetString_141926c84, 0x141926c84, (PunctGetString<0x28, wchar_t, 0x1419211d4>));
REBUILD_FUNCTION(_Mpunct_T2fcdf3_GetString_1419269a4, 0x1419269a4, (PunctGetString<0x30, wchar_t, 0x1419211d4>));
REBUILD_FUNCTION(_Mpunct_Tc7d76e_GetString_14193107c, 0x14193107c, (PunctGetString<0x10, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_Tc7d76e_GetString_14192fd20, 0x14192fd20, (PunctGetString<0x20, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_Tc7d76e_GetString_141931168, 0x141931168, (PunctGetString<0x28, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_Tc7d76e_GetString_14193110c, 0x14193110c, (PunctGetString<0x30, char, 0x140783e70>));
REBUILD_FUNCTION(_Mpunct_Teaba18_DeletingDestructor_141913aec, 0x141913aec, (MpunctDeletingDestructor<0x142525ed0, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_Tf202db_DeletingDestructor_141913f3c, 0x141913f3c, (MpunctDeletingDestructor<0x142525ed0, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_Tf263fe_DeletingDestructor_141913ed4, 0x141913ed4, (MpunctDeletingDestructor<0x142525ed0, 0x142520e38, 0x78>));
REBUILD_FUNCTION(_Mpunct_T2fcdf3_DeletingDestructor_141913a84, 0x141913a84, (MpunctDeletingDestructor<0x142526250, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_T072ab3_DeletingDestructor_141913e6c, 0x141913e6c, (MpunctDeletingDestructor<0x142526250, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_T1e5ca9_DeletingDestructor_141913e04, 0x141913e04, (MpunctDeletingDestructor<0x142526250, 0x142520e38, 0x78>));
REBUILD_FUNCTION(_Mpunct_Tc7d76e_DeletingDestructor_14192cc48, 0x14192cc48, (MpunctDeletingDestructor<0x142526c90, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_Te54cb2_DeletingDestructor_14192cde8, 0x14192cde8, (MpunctDeletingDestructor<0x142526c90, 0x142520e38, 0x78>));
REBUILD_FUNCTION(moneypunct_T29e7aa_DeletingDestructor_14192cd80, 0x14192cd80, (MpunctDeletingDestructor<0x142526c90, 0x142520e38, 0x78>));

namespace rebuild::game_misc {
// SoeUtil::Array<T> storage policy (vtable slot): pick the new capacity for
// `count` elements and allocate it, or return the current buffer when it is
// kept. exact: capacity == count. Otherwise grow to count*5/4, keep while
// capacity <= count*4/3, else shrink to count*6/5. Allocates through the
// thread allocator (0x14032f910, aligned) when one is installed, else the
// tagged heap (0x1402fc150, tag 0x143c46658). 32-bit wrapping arithmetic as
// in the original.
template <int Element, uint64_t Tag, size_t Align = Element>
void* ArrayReallocate(uint8_t* self, int count, int* newCapacity, bool exact) {
  int capacity = *reinterpret_cast<int*>(self + 0x14);
  int chosen;
  if (exact) {
    if (capacity == count) {
      *newCapacity = capacity;
      return *reinterpret_cast<void**>(self + 8);
    }
    if (count == 0) {
      *newCapacity = 0;
      return nullptr;
    }
    chosen = count;
  } else if (count > capacity) {
    chosen = static_cast<int>(static_cast<uint32_t>(count) * 5u) / 4;
  } else if (static_cast<int>(static_cast<uint32_t>(count) * 4u) / 3 >= capacity) {
    *newCapacity = capacity;
    return *reinterpret_cast<void**>(self + 8);
  } else {
    chosen = static_cast<int>(static_cast<uint32_t>(count) * 6u) / 5;
  }
  *newCapacity = chosen;
  uint32_t bytes = static_cast<uint32_t>(chosen) * Element;
  if (*reinterpret_cast<void**>(0x143e09638) != nullptr)
    return game::Call<void* (*)(size_t, size_t)>(0x14032f910)(bytes, Align);
  return game::Call<void* (*)(int64_t, void*)>(0x1402fc150)(static_cast<int32_t>(bytes), reinterpret_cast<void*>(Tag));
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(Array_Tbdc477_Reallocate_141623040, 0x141623040, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_Tdc6556_Reallocate_1403cc120, 0x1403cc120, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T588403_Reallocate_140ac3a60, 0x140ac3a60, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T67e985_Reallocate_14162a250, 0x14162a250, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_Tf14bc3_Reallocate_14164dfc0, 0x14164dfc0, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_Tfe86e4_Reallocate_14164e090, 0x14164e090, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T659cd7_Reallocate_14164e1a0, 0x14164e1a0, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T82f82e_Reallocate_14165a240, 0x14165a240, (ArrayReallocate<4, 0x143c46658>));
REBUILD_FUNCTION(Array_T14d15e_Reallocate_141668eb0, 0x141668eb0, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T043ee0_Reallocate_14166dcf0, 0x14166dcf0, (ArrayReallocate<4, 0x143c46658>));
REBUILD_FUNCTION(Array_Tae3129_Reallocate_1416749b0, 0x1416749b0, (ArrayReallocate<4, 0x143c46658>));
REBUILD_FUNCTION(Array_Tb50686_Reallocate_1416748c0, 0x1416748c0, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_Tfe35cd_Reallocate_141ebfd60, 0x141ebfd60, (ArrayReallocate<8, 0x143c46658>));

namespace rebuild::game_misc {
// Classes with a class-level operator delete (GameCommerce definitions):
// reset to the base vtable; on (flags & 1) free through the sized pool
// (0x1402ec800) when (flags & 4) is set, else the thread allocator
// (0x14032f980) if installed, else operator delete (0x1402fc170).
template <uint64_t Vtable, size_t Size>
void* PoolDeletingDestructor(void* self, unsigned flags) {
  *static_cast<uint64_t*>(self) = Vtable;
  if (flags & 1) {
    if (flags & 4)
      game::Call<void (*)(void*, size_t)>(0x1402ec800)(self, Size);
    else if (*reinterpret_cast<void**>(0x143e09638) != nullptr)
      game::Call<void (*)(void*, int)>(0x14032f980)(self, 0);
    else
      game::Call<void (*)(void*)>(0x1402fc170)(self);
  }
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(StringSecure_Tc47e52_DeletingDestructor_1408146e0, 0x1408146e0, (VtableDeletingDestructor<0x1424b9ed0, 0x140810b50, 0x40>));
REBUILD_FUNCTION(StringSecure_T8190b9_DeletingDestructor_1408146a0, 0x1408146a0, (VtableDeletingDestructor<0x1424b9f10, 0x140810ad0, 0x30>));
REBUILD_FUNCTION(StringSecure_Tedee12_DeletingDestructor_1403c05a0, 0x1403c05a0, (VtableDeletingDestructor<0x1424bb930, 0x1403a6400, 0x128>));
REBUILD_FUNCTION(StringSecure_T9ee84a_DeletingDestructor_141615c60, 0x141615c60, (VtableDeletingDestructor<0x1424bbca0, 0x1416153a0, 0x428>));
REBUILD_FUNCTION(GameCore__GameClientConfig_DeletingDestructor_141683370, 0x141683370, (VtableDeletingDestructor<0x1424c72c0, 0x14030a210, 0x35d0>));
REBUILD_FUNCTION(GameCommerce__BaseDefinition_DeletingDestructor_1407f27e0, 0x1407f27e0, (PoolDeletingDestructor<0x1424bc080, 0x8>));
REBUILD_FUNCTION(GameCommerce__MarketingBundleDefinition__Tag_DeletingDestructor_14161a730, 0x14161a730, (PoolDeletingDestructor<0x1424bc080, 0x10>));
REBUILD_FUNCTION(GameCommerce__StoreBundleGroupDefinition__Entry_DeletingDestructor_14161d550, 0x14161d550, (PoolDeletingDestructor<0x1424bc080, 0x10>));
REBUILD_FUNCTION(GameCommerce__StoreBundleCategoryGroupDefinition__Entry_DeletingDestructor_14161f9c0, 0x14161f9c0, (PoolDeletingDestructor<0x1424bc080, 0x10>));
REBUILD_FUNCTION(GameCommerce__StoreShortcutDefinition_DeletingDestructor_141621880, 0x141621880, (PoolDeletingDestructor<0x1424bc080, 0x18>));
REBUILD_FUNCTION(GameCommerce__StoreBundleCategoryMapEntryDefinition_DeletingDestructor_141e96820, 0x141e96820, (PoolDeletingDestructor<0x1424bc080, 0x18>));

namespace rebuild::game_misc {
// SoeUtil::Array<T> with inline storage (+0x18, aligned to T): when the data
// still points at the inline buffer, move the elements to a heap block of
// exactly `count` and set capacity = count. The inline buffer is left as is.
template <int Element>
void ArrayLeaveInlineStorage(uint8_t* self) {
  using T = std::conditional_t<Element == 8, uint64_t, uint32_t>;
  auto inlineBuffer = (reinterpret_cast<uintptr_t>(self) + 0x18 + Element - 1) & ~static_cast<uintptr_t>(Element - 1);
  T*& data = *reinterpret_cast<T**>(self + 8);
  int& count = *reinterpret_cast<int*>(self + 0x10);
  if (reinterpret_cast<uintptr_t>(data) != inlineBuffer) return;
  uint32_t bytes = static_cast<uint32_t>(count) << (Element == 8 ? 3 : 2);
  T* moved;
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    moved = game::Call<T* (*)(int64_t, void*)>(0x1402fc150)(static_cast<int32_t>(bytes), reinterpret_cast<void*>(0x143c46658));
  else
    moved = game::Call<T* (*)(size_t, size_t)>(0x14032f910)(bytes, Element);
  for (int i = 0; i < count; ++i)
    if (moved + i) moved[i] = data[i];
  *reinterpret_cast<int*>(self + 0x14) = count;
  data = moved;
}
// 7-Zip LZMA match finders (CMatchFinderBinTree / CMatchFinderHC): free the
// hash table (+0x58), free the window (0x14164a590), drop to the
// CLZInWindow vtable and free again, sized delete (0x68).
template <uint64_t Vtable, uint64_t WindowVtable>
void* MatchFinderDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*)>(0x140d42428)(*reinterpret_cast<void**>(bytes + 0x58));
  *reinterpret_cast<void**>(bytes + 0x58) = nullptr;
  game::Call<void (*)(void*)>(0x14164a590)(self);
  *reinterpret_cast<uint64_t*>(bytes) = WindowVtable;
  game::Call<void (*)(void*)>(0x14164a590)(self);
  if (flags & 1) SizedDelete(self, 0x68);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(Array_Tb2ede1_LeaveInlineStorage_1416250c0, 0x1416250c0, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(Array_Ta7356d_LeaveInlineStorage_14162b620, 0x14162b620, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(Array_T4514a5_LeaveInlineStorage_141653010, 0x141653010, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(Array_Tc4d671_LeaveInlineStorage_141652f80, 0x141652f80, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(Array_T912c7d_LeaveInlineStorage_1416530b0, 0x1416530b0, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(Array_Tb081a1_LeaveInlineStorage_14165a400, 0x14165a400, (ArrayLeaveInlineStorage<4>));
REBUILD_FUNCTION(Array_T297d7e_LeaveInlineStorage_141676390, 0x141676390, (ArrayLeaveInlineStorage<4>));
REBUILD_FUNCTION(Array_Tb71391_LeaveInlineStorage_141ec09f0, 0x141ec09f0, (ArrayLeaveInlineStorage<8>));
REBUILD_FUNCTION(NBT2__CMatchFinderBinTree_DeletingDestructor_141642060, 0x141642060, (MatchFinderDeletingDestructor<0x1424c09a8, 0x1424c08e8>));
REBUILD_FUNCTION(NBT3__CMatchFinderBinTree_DeletingDestructor_1416420e0, 0x1416420e0, (MatchFinderDeletingDestructor<0x1424c0a00, 0x1424c08e8>));
REBUILD_FUNCTION(NBT4__CMatchFinderBinTree_DeletingDestructor_141642160, 0x141642160, (MatchFinderDeletingDestructor<0x1424c0a58, 0x1424c08e8>));
REBUILD_FUNCTION(NBT4B__CMatchFinderBinTree_DeletingDestructor_1416421e0, 0x1416421e0, (MatchFinderDeletingDestructor<0x1424c0ab0, 0x1424c08e8>));
REBUILD_FUNCTION(NHC3__CMatchFinderHC_DeletingDestructor_141642260, 0x141642260, (MatchFinderDeletingDestructor<0x1424c0b08, 0x1424c08e8>));
REBUILD_FUNCTION(NHC4__CMatchFinderHC_DeletingDestructor_1416422e0, 0x1416422e0, (MatchFinderDeletingDestructor<0x1424c0b60, 0x1424c08e8>));

namespace rebuild::game_misc {
// Lists with the head at +8: unlink nodes through Remove(self, node) until
// empty, sized delete.
template <uint64_t Vtable, uint64_t Remove, size_t Size>
void* HeadListDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  if (*reinterpret_cast<void**>(bytes + 8)) {
    do {
      if (void* head = *reinterpret_cast<void**>(bytes + 8)) game::Call<void (*)(void*, void*)>(Remove)(self, head);
    } while (*reinterpret_cast<void**>(bytes + 8));
  }
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// SoeUtil::Map (0x18 bytes): free the tree from the root (+8), clear root
// and count (+0x10), sized delete.
template <uint64_t Vtable, uint64_t FreeTree>
void* MapDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*, void*)>(FreeTree)(self, *reinterpret_cast<void**>(bytes + 8));
  *reinterpret_cast<void**>(bytes + 8) = nullptr;
  *reinterpret_cast<int*>(bytes + 0x10) = 0;
  if (flags & 1) SizedDelete(self, 0x18);
  return self;
}
// RefObjectPool<T>: the ref-count interface (at +0x18 in the pooled object)
// hit zero. Destroy the object in place (vtable slot 0, no delete), then
// return it to the pool (pointer at PoolOffset) under the pool's lock.
template <size_t PoolOffset, size_t LockOffset, uint64_t Return, size_t Adjust = 0x18>
void RefObjectPoolRelease(uint8_t* refInterface) {
  uint8_t* object = refInterface - Adjust;
  uint8_t* pool = *reinterpret_cast<uint8_t**>(refInterface + PoolOffset);
  (*reinterpret_cast<void (***)(void*, unsigned)>(object))[0](object, 0);
  uint8_t* lock = pool + LockOffset;
  game::Call<void (*)(void*)>(0x14032f270)(lock);
  game::Call<void (*)(void*, void*)>(Return)(pool, object);
  if (lock) game::Call<void (*)(void*)>(0x14032f360)(lock);
}
// GameCommerce definitions with a destructor and class operator delete
// (see PoolDeletingDestructor).
template <uint64_t Destructor, size_t Size>
void* PoolDestructorDeletingDestructor(void* self, unsigned flags) {
  game::Call<void (*)(void*)>(Destructor)(self);
  if (flags & 1) {
    if (flags & 4)
      game::Call<void (*)(void*, size_t)>(0x1402ec800)(self, Size);
    else if (*reinterpret_cast<void**>(0x143e09638) != nullptr)
      game::Call<void (*)(void*, int)>(0x14032f980)(self, 0);
    else
      game::Call<void (*)(void*)>(0x1402fc170)(self);
  }
  return self;
}
// Class vtable, member destructor at Offset, base vtable, sized delete.
template <uint64_t Vtable, size_t Offset, uint64_t MemberDestructor, uint64_t BaseVtable, size_t Size>
void* MemberBaseDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*)>(MemberDestructor)(bytes + Offset);
  *reinterpret_cast<uint64_t*>(bytes) = BaseVtable;
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(List_T1ad770_DeletingDestructor_140313250, 0x140313250, (HeadListDeletingDestructor<0x14204b538, 0x140314750, 0x20>));
REBUILD_FUNCTION(List_T6c5e26_DeletingDestructor_1403274b0, 0x1403274b0, (HeadListDeletingDestructor<0x14204e558, 0x14032b9c0, 0x20>));
REBUILD_FUNCTION(List_Tbd54a8_DeletingDestructor_140adc0f0, 0x140adc0f0, (HeadListDeletingDestructor<0x1424bfd80, 0x140adcbf0, 0x20>));
REBUILD_FUNCTION(Map_Te94e77_DeletingDestructor_140321d00, 0x140321d00, (MapDeletingDestructor<0x14204d3f8, 0x140324d60>));
REBUILD_FUNCTION(Map_T072562_DeletingDestructor_14033d660, 0x14033d660, (MapDeletingDestructor<0x1420517f0, 0x140343700>));
REBUILD_FUNCTION(Map_Ta00205_DeletingDestructor_141e93d70, 0x141e93d70, (MapDeletingDestructor<0x1425a7ba0, 0x141e94d90>));
REBUILD_FUNCTION(RefObjectPool_Tac9a7c_ReleaseObject_140342de0, 0x140342de0, (RefObjectPoolRelease<0x18, 0x20, 0x140340ad0>));
REBUILD_FUNCTION(RefObjectPool_Tfded9b_ReleaseObject_141654630, 0x141654630, (RefObjectPoolRelease<0x10, 0xc028, 0x140bd4640>));
REBUILD_FUNCTION(RefObjectPool_T914a1c_ReleaseObject_14165b3e0, 0x14165b3e0, (RefObjectPoolRelease<0x28, 0x20, 0x1403f28f0>));
REBUILD_FUNCTION(GameCommerce__MarketingBundleDefinition__Entry_DeletingDestructor_14161a650, 0x14161a650, (PoolDestructorDeletingDestructor<0x14161a1a0, 0x48>));
REBUILD_FUNCTION(GameCommerce__MarketingBundleDefinition_DeletingDestructor_14161a6c0, 0x14161a6c0, (PoolDestructorDeletingDestructor<0x14161a240, 0x1a0>));
REBUILD_FUNCTION(GameCommerce__ImageDataDefinition_DeletingDestructor_141621720, 0x141621720, (PoolDestructorDeletingDestructor<0x141621640, 0x38>));
REBUILD_FUNCTION(MarketingDataElementInstance_Tf11b63_DeletingDestructor_141622cf0, 0x141622cf0, (MemberBaseDeletingDestructor<0x1424bd080, 8, 0x141622600, 0x1424bcf70, 0x68>));
REBUILD_FUNCTION(MarketingDataElementInstance_Ta77bbe_DeletingDestructor_141622c50, 0x141622c50, (MemberBaseDeletingDestructor<0x1424bd108, 8, 0x141622460, 0x1424bcf70, 0x68>));
REBUILD_FUNCTION(MarketingDataElementInstance_Teef970_DeletingDestructor_141622ca0, 0x141622ca0, (MemberBaseDeletingDestructor<0x1424bd190, 8, 0x14072e550, 0x1424bcf70, 0x228>));

namespace rebuild::game_misc {
void FreeArrayStorage(void* data) {
  if (*reinterpret_cast<void**>(0x143e09638) == nullptr)
    game::Call<void (*)(void*)>(0x1402fc170)(data);
  else
    game::Call<void (*)(void*, int)>(0x14032f980)(data, 1);
}

// SoeUtil::TRateTracker: refresh (0x140311910), then scale `value` by the
// tracked total at Offset over the window length (+0x414).
template <size_t Offset>
int64_t RateTrackerScale(uint8_t* self, int value) {
  game::Call<void (*)(void*)>(0x140311910)(self);
  return static_cast<int64_t>(*reinterpret_cast<int*>(self + Offset)) * value / *reinterpret_cast<int*>(self + 0x414);
}
// IStringFixed / WideStringFixed<N> with their own empty-data sentinel.
template <uint64_t InterfaceVtable, uint64_t EmptyData, uint64_t PlainVtable, size_t Size>
void* FixedStringDeletingDestructorEx(void* self, unsigned flags) {
  auto* text = static_cast<soeutil::IString*>(self);
  text->vtable = reinterpret_cast<void**>(InterfaceVtable);
  soeutil::StringRelease(text);
  text->data = reinterpret_cast<char*>(EmptyData);
  *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(self) + 0x10) = 0;
  text->vtable = reinterpret_cast<void**>(PlainVtable);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// GameClientInputManager: run Inner under the optional mutex (+0x81F0).
template <uint64_t Inner>
uint64_t InputManagerLocked(uint8_t* self) {
  if (void* lock = *reinterpret_cast<void**>(self + 0x81F0)) game::Call<void (*)(void*)>(0x14032f270)(lock);
  uint64_t result = game::Call<uint64_t (*)(void*)>(Inner)(self);
  if (void* lock = *reinterpret_cast<void**>(self + 0x81F0)) game::Call<void (*)(void*)>(0x14032f360)(lock);
  return result;
}
// SoeUtil::ArraySecure<T> (inline storage at +0x18): clear the count, free
// heap storage, run the base destructor, sized delete.
template <uint64_t Vtable, uint64_t BaseDestructor, size_t Size>
void* ArraySecureDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<int*>(bytes + 0x10) = 0;
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  void* data = *reinterpret_cast<void**>(bytes + 8);
  if (data != bytes + 0x18) FreeArrayStorage(data);
  *reinterpret_cast<void**>(bytes + 8) = nullptr;
  game::Call<void (*)(void*)>(BaseDestructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// ArraySecure storage release: wipe the live elements, then free the block
// unless it is the inline buffer.
template <uint64_t Wipe>
void ArraySecureFreeStorage(uint8_t* self, void* data) {
  int count = *reinterpret_cast<int*>(self + 0x10);
  if (count > 0) game::Call<void (*)(void*, int, int)>(Wipe)(self, 0, count);
  if (data != self + 0x18) FreeArrayStorage(data);
}
// GameCommerce::UramApi*Request: release the IString at StringOffset, run
// the optional member destructor at +0x80, then the UramApiRequest base
// destructor (0x14160dcc0), sized delete.
template <uint64_t StringVtable, size_t StringOffset, uint64_t MemberDestructor, size_t Size>
void* UramRequestDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  auto* text = reinterpret_cast<soeutil::IString*>(bytes + StringOffset);
  text->vtable = reinterpret_cast<void**>(StringVtable);
  soeutil::StringRelease(text);
  if constexpr (MemberDestructor != 0) game::Call<void (*)(void*)>(MemberDestructor)(bytes + 0x80);
  game::Call<void (*)(void*)>(0x14160dcc0)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(TRateTracker_Td27331_Scale_140311f00, 0x140311f00, (RateTrackerScale<0x40c>));
REBUILD_FUNCTION(TRateTracker_Td27331_Scale_140311e50, 0x140311e50, (RateTrackerScale<0x410>));
REBUILD_FUNCTION(IStringFixed_T0e8e62_DeletingDestructor_14032dae0, 0x14032dae0, (FixedStringDeletingDestructorEx<0x14204f080, 0x142ae85c8, 0x14204efd0, 0x1020>));
REBUILD_FUNCTION(IStringFixed_T36b58f_DeletingDestructor_14032f0a0, 0x14032f0a0, (FixedStringDeletingDestructorEx<0x14204f138, 0x142ae85c8, 0x14204efd0, 0x220>));
REBUILD_FUNCTION(WideStringFixed_T6bbbe2_DeletingDestructor_14032db60, 0x14032db60, (FixedStringDeletingDestructorEx<0x14204f080, 0x142ae85c8, 0x14204efd0, 0x1020>));
REBUILD_FUNCTION(WideStringFixed_T53800f_DeletingDestructor_14032f120, 0x14032f120, (FixedStringDeletingDestructorEx<0x14204f138, 0x142ae85c8, 0x14204efd0, 0x220>));
REBUILD_FUNCTION(GameCore__GameClientInputManager_Locked_140351880, 0x140351880, (InputManagerLocked<0x1416679a0>));
REBUILD_FUNCTION(GameCore__GameClientInputManager_Locked_1403518d0, 0x1403518d0, (InputManagerLocked<0x1416679d0>));
REBUILD_FUNCTION(ArraySecure_T8028d8_DeletingDestructor_1415f9c80, 0x1415f9c80, (ArraySecureDeletingDestructor<0x1424b3258, 0x1415f9ae0, 0x60>));
REBUILD_FUNCTION(ArraySecure_T7afc2a_DeletingDestructor_1415fd0b0, 0x1415fd0b0, (ArraySecureDeletingDestructor<0x1424b3ba8, 0x140d08d60, 0x40>));
REBUILD_FUNCTION(ArraySecure_T8028d8_FreeStorage_1415f9de0, 0x1415f9de0, (ArraySecureFreeStorage<0x140479e80>));
REBUILD_FUNCTION(ArraySecure_T7afc2a_FreeStorage_1415fd7b0, 0x1415fd7b0, (ArraySecureFreeStorage<0x1415fdd00>));
REBUILD_FUNCTION(GameCommerce__UramApiAddCreditCardRequest_DeletingDestructor_14160e870, 0x14160e870, (UramRequestDeletingDestructor<0x142049b50, 0x1f0, 0x140812b80, 0x208>));
REBUILD_FUNCTION(GameCommerce__UramApiUpdateCreditCardRequest_DeletingDestructor_14160eff0, 0x14160eff0, (UramRequestDeletingDestructor<0x142049b50, 0x1f0, 0x140812b80, 0x210>));
REBUILD_FUNCTION(GameCommerce__UramApiDeleteCreditCardRequest_DeletingDestructor_14160e9e0, 0x14160e9e0, (UramRequestDeletingDestructor<0x142049b50, 0x80, 0, 0xa0>));
REBUILD_FUNCTION(GameCommerce__UramApiFinalizeSteamTransactionRequest_DeletingDestructor_14160eaa0, 0x14160eaa0, (UramRequestDeletingDestructor<0x142049b50, 0x80, 0, 0xa0>));

namespace rebuild::game_misc {
// GameCommerce detail ToString: format three ints and the name pointer
// (+0x20) into `out`, return its text.
template <uint64_t Format, size_t A, size_t B, size_t C>
const char* FormatThreeInts(uint8_t* self, soeutil::IString* out) {
  auto field = [&](size_t offset) { return *reinterpret_cast<int*>(self + offset); };
  game::Call<void (*)(soeutil::IString*, const char*, ...)>(0x1402bd7f0)(out, reinterpret_cast<const char*>(Format), field(A), field(B), field(C),
                                                                         *reinterpret_cast<void**>(self + 0x20));
  return out->data;
}
// GameCommerce definitions: class vtable, one or two member destructors,
// base vtable, class operator delete (see PoolDeletingDestructor).
template <uint64_t Vtable, size_t Offset1, uint64_t Destructor1, size_t Offset2, uint64_t Destructor2, uint64_t BaseVtable, size_t Size>
void* MemberPoolDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  game::Call<void (*)(void*)>(Destructor1)(bytes + Offset1);
  if constexpr (Destructor2 != 0) game::Call<void (*)(void*)>(Destructor2)(bytes + Offset2);
  return PoolDeletingDestructor<BaseVtable, Size>(self, flags);
}
// Class vtable, release the IString member at Offset, sized delete.
template <uint64_t Vtable, size_t Offset, uint64_t StringVtable, size_t Size>
void* StringMemberDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  auto* text = reinterpret_cast<soeutil::IString*>(bytes + Offset);
  text->vtable = reinterpret_cast<void**>(StringVtable);
  soeutil::StringRelease(text);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// MarketingDataElementInstance pointer array (data +0x10, count +0x18):
// remove element `index` if below the virtual count (slot 2).
void PointerArrayRemoveAt(uint8_t* self, int index) {
  int count = (*reinterpret_cast<int (***)(void*)>(self))[2](self);
  if (index >= count) return;
  auto* data = *reinterpret_cast<void***>(self + 0x10);
  int& stored = *reinterpret_cast<int*>(self + 0x18);
  game::Call<void* (*)(void*, const void*, size_t)>(0x140d11e20)(data + index, data + index + 1,
                                                                 static_cast<size_t>(static_cast<int64_t>(stored - (index + 1))) * 8);
  --stored;
}
// Pooled ref-array: set both vtables (+0, +0x18), run the shared
// destructor, sized delete.
template <uint64_t Vtable, uint64_t RefVtable, uint64_t Destructor, size_t Size>
void* DualVtableDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  *reinterpret_cast<uint64_t*>(bytes + 0x18) = RefVtable;
  game::Call<void (*)(void*)>(Destructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// Hash list: unlink all nodes (head +0x10), destroy the bucket member
// (+0x30) and the base, sized delete.
template <uint64_t Vtable, uint64_t Remove, uint64_t MemberDestructor, uint64_t BaseDestructor, size_t Size>
void* ListMemberDeletingDestructor(void* self, unsigned flags) {
  auto* bytes = static_cast<uint8_t*>(self);
  *reinterpret_cast<uint64_t*>(bytes) = Vtable;
  if (*reinterpret_cast<void**>(bytes + 0x10)) {
    do {
      if (void* head = *reinterpret_cast<void**>(bytes + 0x10)) game::Call<void (*)(void*, void*)>(Remove)(self, head);
    } while (*reinterpret_cast<void**>(bytes + 0x10));
  }
  game::Call<void (*)(void*)>(MemberDestructor)(bytes + 0x30);
  game::Call<void (*)(void*)>(BaseDestructor)(self);
  if (flags & 1) SizedDelete(self, Size);
  return self;
}
// std::collate<Ch>::do_compare: _Strcoll/_Wcscoll with the facet's
// collation info (+0x10), clamped to -1 / 0 / 1.
template <uint64_t Compare>
int CollateCompare(uint8_t* self, const void* first1, const void* last1, const void* first2, const void* last2) {
  int result = game::Call<int (*)(const void*, const void*, const void*, const void*, void*)>(Compare)(first1, last1, first2, last2, self + 0x10);
  return result < 0 ? -1 : result != 0;
}
}  // namespace rebuild::game_misc

REBUILD_FUNCTION(GameCommerce__MarketingBundleDefinition__Entry_ToString_14161b4b0, 0x14161b4b0, (FormatThreeInts<0x1424bc1e0, 0xc, 0x10, 0x8>));
REBUILD_FUNCTION(GameCommerce__BaseInGamePurchaseOrderDetail_ToString_1416291c0, 0x1416291c0, (FormatThreeInts<0x1424bd9b0, 0x8, 0xc, 0x10>));
REBUILD_FUNCTION(GameCommerce__StoreBundleCategoryGroupDefinition_DeletingDestructor_14161fa20, 0x14161fa20, (MemberPoolDeletingDestructor<0x1424bc8a8, 0x10, 0x14161f7d0, 0, 0, 0x1424bc080, 0x30>));
REBUILD_FUNCTION(GameCommerce__StoreBundleCategoryDefinition_DeletingDestructor_141e96790, 0x141e96790, (MemberPoolDeletingDestructor<0x1425aa5b0, 0x10, 0x141621640, 0, 0, 0x1424bc080, 0x48>));
REBUILD_FUNCTION(GameCommerce__CommerceProcessor_DeletingDestructor_1416213f0, 0x1416213f0, (StringMemberDeletingDestructor<0x1424bcac8, 0x10, 0x142049b50, 0x30>));
REBUILD_FUNCTION(SoeGems__LoggingHeader_DeletingDestructor_14165c680, 0x14165c680, (StringMemberDeletingDestructor<0x1424c37d8, 0x8, 0x142049b50, 0x28>));
REBUILD_FUNCTION(GameCommerce__StorePortalCategoryDefinition_DeletingDestructor_141621b20, 0x141621b20, (MemberPoolDeletingDestructor<0x1424bcc58, 0x50, 0x141621850, 0x18, 0x141621640, 0x1424bc080, 0x68>));
REBUILD_FUNCTION(GameCommerce__StoreBillboardPanelDefinition_DeletingDestructor_141e96be0, 0x141e96be0, (MemberPoolDeletingDestructor<0x1425aa710, 0x48, 0x141621850, 0x10, 0x141621640, 0x1424bc080, 0x68>));
REBUILD_FUNCTION(MarketingDataElementInstance_Tf11b63_RemoveAt_141625fe0, 0x141625fe0, PointerArrayRemoveAt);
REBUILD_FUNCTION(MarketingDataElementInstance_Ta77bbe_RemoveAt_141625f50, 0x141625f50, PointerArrayRemoveAt);
REBUILD_FUNCTION(Array_Ta2a1d0_Reallocate_1403cb260, 0x1403cb260, (ArrayReallocate<64, 0x143c46658, 8>));
REBUILD_FUNCTION(Array_Tb9d987_Reallocate_14163d310, 0x14163d310, (ArrayReallocate<128, 0x143c46658, 8>));
REBUILD_FUNCTION(RefObjectPool_T59d9ef_ReleaseObject_1416546a0, 0x1416546a0, (RefObjectPoolRelease<0xd8, 0x38028, 0x141650c30, 0>));
REBUILD_FUNCTION(RefObjectPool_Tc683aa_ReleaseObject_14166f5d0, 0x14166f5d0, (RefObjectPoolRelease<0x140, 0x20, 0x1407be900, 0>));
REBUILD_FUNCTION(RefArrayPooledListEmbedded_Tfe71b0_DeletingDestructor_14165acd0, 0x14165acd0, (DualVtableDeletingDestructor<0x1424c1fe0, 0x1424c2008, 0x14033cdd0, 0x40>));
REBUILD_FUNCTION(RefObjectPool_T914a1c_DeletingDestructor_14165ad20, 0x14165ad20, (DualVtableDeletingDestructor<0x1424c1fe0, 0x1424c2008, 0x14033cdd0, 0x48>));
REBUILD_FUNCTION(HashListMap_T26c5eb_DeletingDestructor_14165f1f0, 0x14165f1f0, (ListMemberDeletingDestructor<0x1424c41b0, 0x1416657e0, 0x14165ee80, 0x14165eb70, 0x7858>));
REBUILD_FUNCTION(HashList_Tb80d2f_DeletingDestructor_14165f130, 0x14165f130, (ListMemberDeletingDestructor<0x1424c41b0, 0x1416657e0, 0x14165ee80, 0x14165eb70, 0x7858>));
REBUILD_FUNCTION(collate_T7306a0_Compare_141921960, 0x141921960, (CollateCompare<0x141932c1c>));
REBUILD_FUNCTION(collate_Ta29307_Compare_14192fcdc, 0x14192fcdc, (CollateCompare<0x141932918>));
REBUILD_FUNCTION(Array_T968ad7_Reallocate_14194ee90, 0x14194ee90, (ArrayReallocate<8, 0x143c46658>));
REBUILD_FUNCTION(Array_T095a01_Reallocate_1403cb1a0, 0x1403cb1a0, (ArrayReallocate<24, 0x143c46658, 8>));
