// Scalar deleting destructors (vtable slot 0) across the engine's utility,
// asset, audio-hook, commerce, task and third-party classes, rebuilt from
// two templates:
//   - member-less bases: reset the vtable, then sized delete if (flags & 1);
//   - classes with a destructor: call it, then sized delete if (flags & 1).
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"

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
