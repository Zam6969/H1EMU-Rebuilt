// ClientServerCore::BaseUdpManager: the shared UdpManager behind one or more
// BaseApi connections. Reference counted by ServiceStart/ServiceStop; owns
// the UdpManager::Params (+0x88) and reloads them when the ini file changes.
#include <cstddef>
#include <cstdint>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/Memory.h"
#include "udp/UdpRefCount.h"

namespace rebuild::csc {
namespace {

constexpr size_t kServiceRefs = 0x80;    // int
constexpr size_t kThreaded = 0x84;       // bool: UdpManager runs its own thread
constexpr size_t kCompression = 0x85;    // bool
constexpr size_t kParams = 0x88;         // UdpManager::Params (handler pointer first)
constexpr size_t kParamsPort = 0x94;
constexpr size_t kParamsPortRange = 0x98;
constexpr size_t kName = 0x209;          // char[]
constexpr size_t kUdpManager = 0x240;    // UdpManager*
constexpr size_t kLog = 0x250;
constexpr size_t kIniSection = 0x298;    // const char*
constexpr size_t kIniReloadEnabled = 0x2A0;  // int
constexpr size_t kLastIniCheck = 0x2D0;  // seconds
constexpr size_t kIniCrc64 = 0x2D8;

constexpr int kIniCheckIntervalSeconds = 30;
constexpr int kErrorSocketAllocate = 1;
constexpr int kErrorSocketBind = 2;

// UdpManager fields used here.
constexpr size_t kManagerEventsQueued = 0x190;  // bool: events are queued for DeliverEvents

template <typename... Args>
void LogInfo(uint8_t* manager, uintptr_t format, Args... args) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402bab70)(game::Field<void*>(manager, kLog),
                                                             reinterpret_cast<const char*>(format), args...);
}

template <typename... Args>
void LogError(uint8_t* manager, uintptr_t format, Args... args) {
  game::Call<void (*)(void*, const char*, ...)>(0x1402baba0)(game::Field<void*>(manager, kLog),
                                                             reinterpret_cast<const char*>(format), args...);
}

int64_t TimeSeconds() {
  int64_t now;
  return *game::Call<int64_t* (*)(int64_t*)>(0x14032fe90)(&now);
}

// The game's settings ini, if loaded.
void* IniFile() {
  auto base = *reinterpret_cast<uint8_t**>(0x142b068a0);
  return base ? base + 0xC8 : nullptr;
}

uint64_t IniCrc64(void* ini) { return game::Call<uint64_t (*)(void*)>(0x140332e60)(ini); }

}  // namespace

// 0x1415f5700: UdpManager::SetHandler (under two of the manager's guards).
void UdpManagerSetHandler(uint8_t* udpManager, void* handler) {
  auto* outer = reinterpret_cast<udp::UdpPlatformGuardObject*>(udpManager + 0x330);
  auto* inner = reinterpret_cast<udp::UdpPlatformGuardObject*>(udpManager + 0x338);
  outer->Enter();
  inner->Enter();
  game::Field<void*>(udpManager, 0x18) = handler;
  inner->Leave();
  outer->Leave();
}

// 0x1415f54b0: ServiceStart. Loads ini settings into the params, and on the
// first reference creates the UdpManager.
void BaseUdpManagerServiceStart(uint8_t* manager) {
  game::Field<int64_t>(manager, kLastIniCheck) = TimeSeconds();
  if (void* ini = IniFile()) {
    uint64_t crc = IniCrc64(ini);
    game::Field<uint64_t>(manager, kIniCrc64) = crc;
    LogInfo(manager, 0x1424b0e10, game::Field<const char*>(manager, kIniSection), crc);  // "Extracting any inifile settings ..."
    game::Call<void (*)(uint8_t*, void*, const char*)>(0x1415f7050)(manager + kParams, ini,
                                                                     game::Field<const char*>(manager, kIniSection));
  } else {
    const char* section = game::Field<const char*>(manager, kIniSection);
    const char* expected = reinterpret_cast<const char*>(0x1424b0e00);  // "BaseApi"
    while (*section && *section == *expected) {
      ++section;
      ++expected;
    }
    if (*section != *expected) LogError(manager, 0x1424b0e80);  // "... hasn't provided access to the inifile ..."
  }
  if (++game::Field<int>(manager, kServiceRefs) != 1) return;
  game::Field<void*>(manager, kParams) = manager;  // params.handler
  void* memory = soeutil::Allocate(0x660);
  void* udpManager = memory ? game::Call<void* (*)(void*, uint8_t*)>(0x14033bdd0)(memory, manager + kParams) : nullptr;
  game::Field<void*>(manager, kUdpManager) = udpManager;
  LogInfo(manager, 0x1424b0f30, manager + kName, static_cast<int>(game::Field<bool>(manager, kThreaded)),
          static_cast<int>(game::Field<bool>(manager, kCompression)));  // "BaseUdpManager service started ..."
  int error = game::Call<int (*)(void*)>(0x140340d80)(udpManager);  // GetErrorCondition
  uintptr_t format = 0;
  if (error == kErrorSocketAllocate) format = 0x1424b0f70;  // "could not allocate socket"
  else if (error == kErrorSocketBind) format = 0x1424b0fc0;  // "could not bind socket"
  if (format) {
    LogError(manager, format, manager + kName, game::Field<int>(manager, kParamsPort),
             game::Field<int>(manager, kParamsPortRange));
  }
  if (game::Field<bool>(manager, kThreaded)) {
    game::Call<void (*)(void*)>(0x140344cf0)(game::Field<void*>(manager, kUdpManager));  // ThreadStart
  }
}

// 0x1415f5670: ServiceStop. The last reference tears the UdpManager down.
void BaseUdpManagerServiceStop(uint8_t* manager) {
  if (--game::Field<int>(manager, kServiceRefs) != 0) return;
  auto* udpManager = game::Field<uint8_t*>(manager, kUdpManager);
  if (!udpManager) return;
  if (game::Field<bool>(manager, kThreaded)) game::Call<void (*)(void*)>(0x140344d80)(udpManager);  // ThreadStop
  game::Call<void (*)(void*)>(0x14033fb60)(game::Field<void*>(manager, kUdpManager));  // DisconnectAll
  UdpManagerSetHandler(game::Field<uint8_t*>(manager, kUdpManager), nullptr);
  auto* release = game::Field<uint8_t*>(manager, kUdpManager);
  reinterpret_cast<void (*)(void*)>((*reinterpret_cast<void***>(release))[1])(release);
  game::Field<void*>(manager, kUdpManager) = nullptr;
  LogInfo(manager, 0x1424b1008, manager + kName);  // "BaseUdpManager service stopped (%s)"
}

// 0x1415f4470: GiveTime(maxPollingMs). 0 = poll everything once with the
// event queue locked. Every 30 s, reloads UdpParams if the ini changed.
void BaseUdpManagerGiveTime(uint8_t* manager, int maxPollingMs) {
  auto* udpManager = game::Field<uint8_t*>(manager, kUdpManager);
  if (!udpManager) return;
  bool lockedEvents = false;
  if (maxPollingMs == 0 && !game::Field<bool>(udpManager, kManagerEventsQueued)) {
    game::Call<void (*)(void*, bool)>(0x140344910)(udpManager, true);  // SetEventQueuing
    lockedEvents = true;
  }
  if (!game::Field<bool>(manager, kThreaded)) {
    int pollMs = maxPollingMs == 0 ? -1 : maxPollingMs;
    game::Call<bool (*)(void*, int, bool)>(0x1403413c0)(game::Field<void*>(manager, kUdpManager), pollMs, true);
    if (!game::Field<void*>(manager, kUdpManager)) return;
  }
  udpManager = game::Field<uint8_t*>(manager, kUdpManager);
  if (game::Field<bool>(udpManager, kManagerEventsQueued)) {
    game::Call<void (*)(void*, int)>(0x14033f730)(udpManager, maxPollingMs);  // DeliverEvents
    udpManager = game::Field<uint8_t*>(manager, kUdpManager);
    if (!udpManager) return;
  }
  if (lockedEvents) game::Call<void (*)(void*, bool)>(0x140344910)(udpManager, false);
  if (game::Field<int>(manager, kIniReloadEnabled) <= 0) return;
  int64_t last = game::Field<int64_t>(manager, kLastIniCheck);
  if (static_cast<int>(TimeSeconds()) - static_cast<int>(last) <= kIniCheckIntervalSeconds) return;
  game::Field<int64_t>(manager, kLastIniCheck) = TimeSeconds();
  void* ini = IniFile();
  if (!ini) return;
  uint64_t oldCrc = game::Field<uint64_t>(manager, kIniCrc64);
  if (IniCrc64(ini) == oldCrc) return;
  uint64_t newCrc = IniCrc64(ini);
  game::Field<uint64_t>(manager, kIniCrc64) = newCrc;
  LogInfo(manager, 0x1424b10c0, game::Field<const char*>(manager, kIniSection), oldCrc, newCrc);  // "inifile has been modified ..."
  game::Call<void (*)(uint8_t*, void*, const char*)>(0x1415f7880)(manager + kParams, ini,
                                                                   game::Field<const char*>(manager, kIniSection));
  LogInfo(manager, 0x1424b1150);  // "updating the runtime udp params on the Udpmanager"
  game::Call<void (*)(void*, uint8_t*)>(0x140344f40)(game::Field<void*>(manager, kUdpManager), manager + kParams);
}

REBUILD_FUNCTION(UdpManager_SetHandler, 0x1415f5700, UdpManagerSetHandler);
REBUILD_FUNCTION(BaseUdpManager_ServiceStart, 0x1415f54b0, BaseUdpManagerServiceStart);
REBUILD_FUNCTION(BaseUdpManager_ServiceStop, 0x1415f5670, BaseUdpManagerServiceStop);
REBUILD_FUNCTION(BaseUdpManager_GiveTime, 0x1415f4470, BaseUdpManagerGiveTime);

}  // namespace rebuild::csc
