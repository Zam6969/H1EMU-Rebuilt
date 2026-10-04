// ClientServerCore::BaseApp: application bootstrap shared by the client -
// startup log, configuration, logging service, infinite-loop watchdog and
// crash reporter settings.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::csc {
namespace {

const char* Str(uint64_t address) { return reinterpret_cast<const char*>(address); }

template <class Fn>
Fn Virtual(void* object, size_t offset) {
  return (*reinterpret_cast<Fn**>(object))[offset / 8];
}

void ReleaseFixed(soeutil::IString& text, uint64_t interfaceVtable) {
  text.vtable = reinterpret_cast<void**>(interfaceVtable);
  soeutil::StringRelease(&text);
  text.data = soeutil::EmptyStringData();
  text.length = 0;
  text.capacity = 0;
  text.vtable = soeutil::IStringVtable();
}

// Crash reporter settings handed to BaseApp vfunc 0x20 and 0x1403160f0.
struct CrashReporterSettings {
  const char* productName;  // +0x00
  const char* path;         // +0x08 config +0x3440
  const char* version;      // +0x10
  const char* empty;        // +0x18
  const char* uploader;     // +0x20
  int reservedMemoryMb;     // +0x28
  bool noUploadFromInit;    // +0x2C
  bool outputLocalFullDump; // +0x2D
  bool continueExecution;   // +0x2E
  bool includeIndirectMemory;  // +0x2F
};
static_assert(sizeof(CrashReporterSettings) == 0x30);

using GetStringFn = void (*)(void*, const char*, const char*, const char*, void*, bool, int, int);
using GetIntFn = int (*)(void*, const char*, const char*, int, bool, int, int);
using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
using FormatFn = void (*)(soeutil::IString*, const char*, ...);
using LogFn = void (*)(void*, const char*, ...);

}  // namespace

// 0x140307bf0 (BaseApp slot 2): Init(commandLine).
bool BaseAppInit(uint8_t* app, void* commandLine) {
  using NameFn = const char* (*)(uint8_t*);
  using ConfigFn = uint8_t* (*)(uint8_t*);
  soeutil::StringFixed<256> startupLog;
  soeutil::InitFixed(startupLog, reinterpret_cast<void**>(0x142049e08));
  game::Call<FormatFn>(0x1402bd7f0)(&startupLog, Str(0x142049f10), Virtual<NameFn>(app, 0x28)(app));  // "%sStartup.log"
  {
    void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0xF580);
    *reinterpret_cast<void**>(app + 0x10) = memory ? game::Call<void* (*)(void*, int, bool, int)>(0x14030bba0)(memory, 0x10000, true, 0) : nullptr;
  }
  game::Call<void (*)(uint8_t*)>(0x14032fb40)(app);
  game::Call<void (*)(void*, const char*)>(0x140310480)(*reinterpret_cast<void**>(app + 0x10), startupLog.data);
  alignas(16) uint8_t commandLineIni[0xEA0];
  game::Call<void (*)(void*)>(0x140331340)(commandLineIni);
  game::Call<void (*)(void*, void*)>(0x140333e70)(commandLineIni, commandLine);
  soeutil::StringFixed<128> workingDir;
  soeutil::InitFixed(workingDir, reinterpret_cast<void**>(0x142049dc8));
  game::Call<GetStringFn>(0x1403334f0)(commandLineIni, Str(0x142049f2c), Str(0x142049f20), Str(0x142046fcb), &workingDir, false, -1,
                                       -1);  // "System", "WorkingDir"
  if (workingDir.length > 0) game::Call<void (*)(const char*)>(0x14032eb00)(workingDir.data);
  game::Call<void (*)(const char*, const char*)>(0x14032d230)(Str(0x142049f48), Str(0x142049f38));  // "C:/Crash", "user.dmp"
  {
    const char* name = Virtual<NameFn>(app, 0x28)(app);
    uint8_t* config = Virtual<ConfigFn>(app, 0x40)(app);
    game::Call<void (*)(uint8_t*, void*, const char*)>(0x14030a960)(config, commandLine, name);
  }
  uint8_t* config = Virtual<ConfigFn>(app, 0x40)(app);
  Virtual<void (*)(uint8_t*, bool)>(config, 8)(config, false);  // BaseConfig::Load
  *reinterpret_cast<uint8_t**>(0x142b068a0) = Virtual<ConfigFn>(app, 0x40)(app);
  game::Call<void (*)(uint8_t*, bool)>(0x1403072a0)(app, config[0x3498]);  // profiler enable
  void* logging = *reinterpret_cast<void**>(app + 0x10);
  auto field = [&](size_t offset) { return *reinterpret_cast<const char**>(config + offset); };
  game::Call<void (*)(void*, const char*, const char*, const char*)>(0x1403106a0)(logging, field(0x2E30), field(0x2E90), field(0x2EF0));
  game::Call<void (*)(void*, const char*)>(0x140310580)(*reinterpret_cast<void**>(app + 0x10), field(0x3010));
  game::Call<void (*)(void*, int)>(0x140310470)(*reinterpret_cast<void**>(app + 0x10), *reinterpret_cast<int*>(config + 0x3248));
  game::Call<void (*)(void*, int)>(0x140310840)(*reinterpret_cast<void**>(app + 0x10), *reinterpret_cast<int*>(config + 0x324C));
  game::Call<void (*)(void*, int)>(0x140310570)(*reinterpret_cast<void**>(app + 0x10), *reinterpret_cast<int*>(config + 0x3250));
  soeutil::StringAssign(reinterpret_cast<soeutil::IString*>(*reinterpret_cast<uint8_t**>(app + 0x10) + 0xE448), field(0x3130));
  if (std::strlen(field(0x2DD0)) > 0) {
    game::Call<LogFn>(0x1402bab70)(nullptr, Str(0x142049f58), field(0x2DD0));  // "Calling LoggingApi::Connect call with address %s"
    void* loggingApi = *reinterpret_cast<void**>(app + 0x10);
    Virtual<void (*)(void*, const char*, int, bool)>(loggingApi, 0x30)(loggingApi, field(0x2DD0), 30000, true);
  }
  soeutil::StringFixed<256> crashFile;
  soeutil::InitFixed(crashFile, reinterpret_cast<void**>(0x142049e08));
  const char* crashPrefix = field(0x2D70);
  if (static_cast<int>(std::strlen(crashPrefix)) > 0)
    game::Call<FormatFn>(0x1402bd7f0)(&crashFile, Str(0x142049f90), crashPrefix, Virtual<NameFn>(app, 0x28)(app));  // "crash.%s.%s.txt"
  else
    game::Call<FormatFn>(0x1402bd7f0)(&crashFile, Str(0x142049fa0), Virtual<NameFn>(app, 0x28)(app));  // "crash.%s.txt"
  // InfiniteLoopMonitor watchdog (check interval from the ini, default 5000 ms).
  char defaultInterval[0x400];
  game::Call<void (*)(char*, int, int, int)>(0x140305380)(defaultInterval, 0x400, 5000, 0);
  char intervalText[0x400];
  using GetBufferFn = void (*)(void*, const char*, const char*, const char*, char*, int, bool, int, int);
  game::Call<GetBufferFn>(0x1403334e0)(config + 0xC8, Str(0x142049fc0), Str(0x142049fb0), defaultInterval, intervalText, 0x400, false, -1,
                                       -1);  // "InfiniteLoopMonitor", "CheckIntervalMs"
  char expanded[0x800];
  expanded[0] = 0;
  game::Call<void (*)(const char*, char*, int, int, bool, bool)>(0x1403309b0)(intervalText, expanded, 0x800, 0, true, true);
  int checkInterval = 0;
  game::Call<void (*)(const char*, int*)>(0x1402ecf30)(expanded, &checkInterval);
  {
    void* memory = game::Call<void* (*)(size_t)>(0x1402fc0f0)(0xF0);
    *reinterpret_cast<void**>(app + 0x312D8) =
        memory ? game::Call<void* (*)(void*, int, const char*, int)>(0x14032cc60)(memory, *reinterpret_cast<int*>(config + 0x34A0) * 1000,
                                                                                crashFile.data, checkInterval)
               : nullptr;
  }
  bool watchdog = game::Call<GetBoolFn>(0x1403051c0)(config + 0xC8, Str(0x142049fc0), Str(0x142049fd8), true, false, -1, -1);  // "Enabled"
  game::Call<void (*)(void*, bool)>(0x14032d190)(*reinterpret_cast<void**>(app + 0x312D8), watchdog);
  game::Call<void (*)(void*)>(0x140335b70)(*reinterpret_cast<void**>(app + 0x312D8));
  const char* crashPath = field(0x3440);
  if (*crashPath) {
    soeutil::StringFixed<64> productName, version, uploader;
    for (auto* text : {&productName, &version, &uploader}) soeutil::InitFixed(*text, reinterpret_cast<void**>(0x142049d00));
    auto settingsIni = [&] { return Virtual<ConfigFn>(app, 0x40)(app) + 0xC8; };
    uint8_t* ini = settingsIni();
    game::Call<GetStringFn>(0x1403334f0)(ini, Str(0x142049ff0), Str(0x142049fe0), Virtual<NameFn>(app, 0x28)(app), &productName, false, -1,
                                         -1);  // "CrashReporter", "ProductName"
    ini = settingsIni();
    game::Call<GetStringFn>(0x1403334f0)(ini, Str(0x142049ff0), Str(0x14204a000), Virtual<NameFn>(app, 0x30)(app), &version, false, -1,
                                         -1);  // "Version"
    game::Call<GetStringFn>(0x1403334f0)(settingsIni(), Str(0x142049ff0), Str(0x14204a008), Str(0x142049ba0), &uploader, false, -1,
                                         -1);  // "Uploader", "wws_crashreport_uploader.exe"
    CrashReporterSettings settings{};
    settings.empty = Str(0x142046fcb);
    settings.reservedMemoryMb = 4;
    settings.path = crashPath;
    settings.productName = productName.data;
    settings.version = version.data;
    settings.uploader = uploader.data;
    settings.reservedMemoryMb = game::Call<GetIntFn>(0x1403050e0)(settingsIni(), Str(0x142049ff0), Str(0x14204a018), 0x40, false, -1, -1);  // "ReservedMemoryMB"
    settings.noUploadFromInit = game::Call<GetBoolFn>(0x1403051c0)(settingsIni(), Str(0x142049ff0), Str(0x14204a030), true, false, -1, -1);  // "NoUploadFromInit"
    settings.outputLocalFullDump =
        game::Call<GetBoolFn>(0x1403051c0)(settingsIni(), Str(0x142049ff0), Str(0x14204a048), false, false, -1, -1);  // "OutputLocalFullDump"
    settings.includeIndirectMemory =
        game::Call<GetBoolFn>(0x1403051c0)(settingsIni(), Str(0x142049ff0), Str(0x14204a060), false, false, -1, -1);  // "IncludeIndirectMemory"
    settings.continueExecution = game::Call<GetBoolFn>(0x1403051c0)(settingsIni(), Str(0x142049ff0), Str(0x14204a078), false, false, -1,
                                                                    -1);  // "ContinueExecutionAfterCrashReporting"
    Virtual<void (*)(uint8_t*, CrashReporterSettings*)>(app, 0x20)(app, &settings);
    game::Call<void (*)(uint8_t*, CrashReporterSettings*)>(0x1403160f0)(app + 0xB0, &settings);
    *reinterpret_cast<uint8_t**>(app + 0xB8) = app + 8;
    ReleaseFixed(uploader, 0x142049ce0);
    ReleaseFixed(version, 0x142049ce0);
    productName.vtable = reinterpret_cast<void**>(0x142049ce0);
    soeutil::StringRelease(&productName);
  }
  game::Call<FormatFn>(0x1402bd7f0)(reinterpret_cast<soeutil::IString*>(app + 0x50), Str(0x14204a0a0), Virtual<NameFn>(app, 0x28)(app));  // "%s.log"
  game::Call<void (*)(uint8_t*, uint8_t*)>(0x140308bb0)(app, config);
  {
    soeutil::StringFixed<256> banner;
    soeutil::InitFixed(banner, reinterpret_cast<void**>(0x142049e08));
    game::Call<void (*)(soeutil::IString*)>(0x14032e120)(&banner);
    const char* built = Virtual<NameFn>(app, 0x38)(app);
    const char* versionText = Virtual<NameFn>(app, 0x30)(app);
    uint64_t pid = game::Call<uint64_t (*)()>(0x14032e6c0)();
    game::Call<FormatFn>(0x1402ed6c0)(&banner, Str(0x14204a0a8), pid, versionText, built);  // " -  Pid: %llu / Version: %s / Built: %s" (append)
    game::Call<void (*)(const char*)>(0x14032e8c0)(banner.data);
    game::Call<LogFn>(0x1402bab70)(nullptr, Str(0x14204a0d0), crashFile.data);  // "CrashFileName=%s"
    game::Call<void (*)(uint8_t*)>(0x14030b450)(config);
    ReleaseFixed(banner, 0x142049de8);
  }
  ReleaseFixed(crashFile, 0x142049de8);
  ReleaseFixed(workingDir, 0x142049da8);
  game::Call<void (*)(void*)>(0x140331760)(commandLineIni);
  startupLog.vtable = reinterpret_cast<void**>(0x142049de8);
  soeutil::StringRelease(&startupLog);
  return true;
}

}  // namespace rebuild::csc

REBUILD_FUNCTION(BaseApp_Init, 0x140307bf0, rebuild::csc::BaseAppInit);
