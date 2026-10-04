// ClientServerCore::BaseConfig: the application's layered ini configuration
// (command line +0x1DF8, primary ini +0xF60, base ini +0xC8) and the common
// settings read from it.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "core/game.h"
#include "core/hook.h"
#include "soeutil/String.h"

namespace rebuild::csc {
namespace {

constexpr size_t kBaseIniPath = 0x08;      // IString (data +0x10)
constexpr size_t kPrimaryIniPath = 0x68;   // IString (data +0x70)
constexpr size_t kBaseIni = 0xC8;          // ini file (merged result)
constexpr size_t kPrimaryIni = 0xF60;
constexpr size_t kCommandLine = 0x1DF8;
constexpr size_t kMachineName = 0x2CB0;    // const char*
constexpr size_t kAppName = 0x3588;        // const char*
constexpr size_t kPrimaryTime = 0x35C0;    // last seen modification time
constexpr size_t kBaseTime = 0x35C8;

using GetStringFn = void (*)(void*, const char*, const char*, const char*, void*, bool, int, int);
using GetIntFn = int (*)(void*, const char*, const char*, int, bool, int, int);
using GetBoolFn = bool (*)(void*, const char*, const char*, bool, bool, int, int);
using HasKeyFn = bool (*)(void*, const char*, const char*, int, int);
using FileInfoFn = bool (*)(const char*, void*);
using FormatFn = void (*)(soeutil::IString*, const char*, ...);
using ReplaceFn = void (*)(void*, const char*, const char*, int);

const char* Str(uint64_t address) { return reinterpret_cast<const char*>(address); }

void ReleaseFixed(soeutil::IString& text, uint64_t interfaceVtable) {
  text.vtable = reinterpret_cast<void**>(interfaceVtable);
  soeutil::StringRelease(&text);
}

}  // namespace

// 0x14030ab00 (BaseConfig slot 1): Load(reload). Resolves "<app>.ini" from
// the command line, and on first load (or when either ini file changed on
// disk) rebuilds the ini layers and re-reads Settings / Soemon / Profiler /
// CrashReporter / Logging / InfiniteLoopMonitor / Memory.
void BaseConfigLoad(uint8_t* config, bool reload) {
  soeutil::StringFixed<64> defaultIni;
  soeutil::InitFixed(defaultIni, reinterpret_cast<void**>(0x142049d00));
  game::Call<FormatFn>(0x1402bd7f0)(&defaultIni, Str(0x14204a3c4), game::Field<const char*>(config, kAppName));  // "%s.ini"
  game::Call<GetStringFn>(0x1403334f0)(config + kCommandLine, Str(0x142046fcb), Str(0x14204a3d0), defaultIni.data, config + kPrimaryIniPath,
                                       false, -1, -1);  // "Inifile"
  alignas(8) uint8_t primaryInfo[0x20] = {};
  alignas(8) uint8_t baseInfo[0x20] = {};
  bool havePrimary = game::Call<FileInfoFn>(0x140336bd0)(defaultIni.data, primaryInfo);
  bool haveBase = game::Call<FileInfoFn>(0x140336bd0)(game::Field<const char*>(config, kPrimaryIniPath + 8), baseInfo);
  int64_t primaryTime = *reinterpret_cast<int64_t*>(primaryInfo + 0x10);
  int64_t baseTime = *reinterpret_cast<int64_t*>(baseInfo + 0x10);
  if (!reload || (havePrimary && primaryTime != game::Field<int64_t>(config, kPrimaryTime)) ||
      (haveBase && baseTime != game::Field<int64_t>(config, kBaseTime))) {
    const char* verb = reload ? Str(0x14204a3d8) : Str(0x14204a3e8);  // "Reloading" / "Loading"
    using LogFn = void (*)(void*, const char*, ...);
    if (havePrimary) {
      game::Field<int64_t>(config, kPrimaryTime) = primaryTime;
      game::Call<LogFn>(0x1402bab70)(nullptr, Str(0x14204a3f0), verb, defaultIni.data);  // "%s primary ini file %s"
    }
    if (haveBase) {
      game::Field<int64_t>(config, kBaseTime) = *reinterpret_cast<int64_t*>(baseInfo + 0x10);
      game::Call<LogFn>(0x1402bab70)(nullptr, Str(0x14204a408), verb,
                                     game::Field<const char*>(config, kPrimaryIniPath + 8));  // "%s secondary ini file %s"
    }
    uint8_t* base = config + kBaseIni;
    uint8_t* primary = config + kPrimaryIni;
    uint8_t* commandLine = config + kCommandLine;
    using ClearFn = void (*)(void*, int);
    using LoadFn = void (*)(void*, const char*);
    using MergeFn = void (*)(void*, void*, int);
    game::Call<ClearFn>(0x1403322c0)(base, 0);
    game::Field<uint64_t>(config, 0xF50) = 0;
    game::Call<ClearFn>(0x1403322c0)(primary, 0);
    game::Field<uint64_t>(config, 0x1DE8) = 0;
    game::Call<LoadFn>(0x140334100)(primary, game::Field<const char*>(config, kPrimaryIniPath + 8));
    game::Call<MergeFn>(0x140335530)(primary, commandLine, 0);
    // BaseInifile from the command line, else from the primary ini, else
    // "<primary ini>" with ".ini" replaced by "Base.ini".
    if (game::Call<HasKeyFn>(0x140332620)(commandLine, Str(0x142046fcb), Str(0x14204a428), -1, -1)) {  // "BaseInifile"
      game::Call<GetStringFn>(0x1403334f0)(commandLine, Str(0x142046fcb), Str(0x14204a428), Str(0x142046fcb), config + kBaseIniPath, false, -1, -1);
    } else if (game::Call<HasKeyFn>(0x140332620)(primary, Str(0x142046fcb), Str(0x14204a428), -1, -1)) {
      game::Call<GetStringFn>(0x1403334f0)(primary, Str(0x142046fcb), Str(0x14204a428), Str(0x142046fcb), config + kBaseIniPath, false, -1, -1);
    } else {
      game::Call<FormatFn>(0x1402bd7f0)(reinterpret_cast<soeutil::IString*>(config + kBaseIniPath), Str(0x142046fb8),
                                        game::Field<const char*>(config, kPrimaryIniPath + 8));  // "%s"
      game::Call<ReplaceFn>(0x14030b5f0)(config + kBaseIniPath, Str(0x14204a444), Str(0x14204a438), 0);  // ".ini" -> "Base.ini"
    }
    if (game::Call<FileInfoFn>(0x140336bd0)(game::Field<const char*>(config, kBaseIniPath + 8), nullptr))
      game::Call<LoadFn>(0x140334100)(base, game::Field<const char*>(config, kBaseIniPath + 8));
    game::Call<MergeFn>(0x140335530)(base, primary, 0);

    auto getString = [&](uint64_t section, uint64_t key, const char* fallback, size_t offset) {
      game::Call<GetStringFn>(0x1403334f0)(base, Str(section), Str(key), fallback, config + offset, true, -1, -1);
    };
    auto getInt = [&](uint64_t section, uint64_t key, int fallback, bool flag = true) {
      return game::Call<GetIntFn>(0x1403050e0)(base, Str(section), Str(key), fallback, flag, -1, -1);
    };
    const char* empty = Str(0x142046fcb);
    constexpr uint64_t kSettings = 0x14204a478, kSoemon = 0x14204a544, kLogging = 0x14204a5a0, kMemory = 0x14204a678;
    getString(0x142046fcb, 0x14204a450, empty, 0x2D68);  // "OpsClusterName"
    getString(kSettings, 0x14204a460, empty, 0x34C0);    // "PreloadCompleteFilename"
    getString(kSettings, 0x14204a488, empty, 0x3520);    // "PreShutdownNotificationFilename"
    game::Field<int>(config, 0x34A8) = getInt(kSettings, 0x14204a4a8, game::Field<int>(config, 0x34A8));         // "PerFrameSleepTarget"
    game::Field<int>(config, 0x34AC) = getInt(kSettings, 0x14204a4c0, game::Field<int>(config, 0x34AC));         // "MinimumSleepPerFrame"
    game::Field<int>(config, 0x34B0) = getInt(kSettings, 0x14204a4d8, game::Field<int>(config, 0x34B0));         // "SlowFrameLogMilliseconds"
    game::Field<int>(config, 0x34A4) = getInt(kSettings, 0x14204a4f8, game::Field<int>(config, 0x34A4), false);  // "InifileReloadSeconds"

    soeutil::StringFixed<256> agentName;
    soeutil::InitFixed(agentName, reinterpret_cast<void**>(0x142049e08));
    game::Call<FormatFn>(0x1402bd7f0)(&agentName, Str(0x14204a510), game::Field<const char*>(config, kAppName));  // "games/gametechnology/%%machine%%/%s"
    getString(kSoemon, 0x14204a538, agentName.data, 0x3258);  // "AgentName"
    getString(kSoemon, 0x14204a550, empty, 0x3378);           // "ServerAddress"
    getString(kSoemon, 0x14204a560, empty, 0x33D8);           // "XmlServerAddress"
    game::Field<bool>(config, 0x3498) = game::Call<GetBoolFn>(0x1403051c0)(base, Str(0x14204a580), Str(0x14204a574), game::Field<bool>(config, 0x3498),
                                                                           true, -1, -1);  // "Profiler", "Enable"
    game::Field<int>(config, 0x349C) = getInt(0x14204a580, 0x14204a590, game::Field<int>(config, 0x349C));  // "LogSeconds"
    getString(0x142049ff0, 0x142048ff8, empty, 0x3438);  // "CrashReporter", "Address"
    getString(kLogging, 0x142048ff8, empty, 0x2DC8);     // "Address"
    getString(kLogging, 0x14204a5b8, Str(0x14204a5a8), 0x2E28);  // "Username", "%machine%"
    game::Call<ReplaceFn>(0x14030b5f0)(config + 0x2E28, Str(0x14204a5a8), game::Field<const char*>(config, kMachineName), 0);
    soeutil::StringFixed<128> directory;
    soeutil::InitFixed(directory, reinterpret_cast<void**>(0x142049dc8));
    game::Call<FormatFn>(0x1402bd7f0)(&directory, Str(0x14204a5c8), game::Field<const char*>(config, kAppName));  // "%%machine%%/#y/#m-#d/%s"
    getString(kLogging, 0x14204a5e8, Str(0x14204a5e0), 0x2E88);  // "Password", "unset"
    getString(kLogging, 0x14204a5f8, directory.data, 0x2EE8);    // "Directory"
    getString(kLogging, 0x14204a608, empty, 0x3008);             // "LocalDirectory"
    game::Field<int>(config, 0x3248) = getInt(kLogging, 0x14204a618, 3);  // "ConsoleLogLevel"
    game::Field<int>(config, 0x324C) = getInt(kLogging, 0x14204a628, 4);  // "FileLogLevel"
    game::Field<int>(config, 0x3250) = getInt(kLogging, 0x14204a638, 5);  // "LocalLogLevel"
    getString(kLogging, 0x14204a648, empty, 0x3128);                      // "FailureDirectory"
    game::Field<int>(config, 0x34A0) = 0xB4;
    game::Field<int>(config, 0x34A0) = getInt(0x142049fc0, 0x14204a660, 0xB4);                     // "InfiniteLoopMonitor", "TimeoutSeconds"
    game::Field<int>(config, 0x34B4) = getInt(kMemory, 0x14204a670, game::Field<int>(config, 0x34B4));  // "LimitMB"
    game::Field<int>(config, 0x34B8) = getInt(kMemory, 0x14204a680, game::Field<int>(config, 0x34B8));  // "WarnPercent"
    game::Field<int>(config, 0x34BC) = getInt(kMemory, 0x14204a690, game::Field<int>(config, 0x34BC));  // "CheckSeconds"
    ReleaseFixed(directory, 0x142049da8);
    ReleaseFixed(agentName, 0x142049de8);
  }
  ReleaseFixed(defaultIni, 0x142049ce0);
}

}  // namespace rebuild::csc

REBUILD_FUNCTION(BaseConfig_Load, 0x14030ab00, rebuild::csc::BaseConfigLoad);
