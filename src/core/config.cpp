#include "core/config.h"

#include <windows.h>

namespace rebuild::config {
namespace {

std::wstring g_dir;
std::wstring g_ini;

int ReadInt(const char* section, const char* key, int fallback) {
  wchar_t wsection[64], wkey[128];
  MultiByteToWideChar(CP_UTF8, 0, section, -1, wsection, 64);
  MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, 128);
  // Absolute path required: a bare file name makes Windows read from C:\Windows.
  return static_cast<int>(GetPrivateProfileIntW(wsection, wkey, fallback, g_ini.c_str()));
}

}  // namespace

void Load() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  g_dir = path;
  g_dir.resize(g_dir.find_last_of(L'\\') + 1);
  g_ini = g_dir + L"rebuild.ini";
}

const std::wstring& GameDir() { return g_dir; }
bool Enabled() { return ReadInt("general", "enabled", 1) != 0; }
bool Console() { return ReadInt("general", "console", 1) != 0; }
bool HookEnabled(const char* name) { return ReadInt("hooks", name, 1) != 0; }

}  // namespace rebuild::config
