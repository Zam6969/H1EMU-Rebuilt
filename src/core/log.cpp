#include "core/log.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "core/config.h"

namespace rebuild::log {
namespace {

FILE* g_file = nullptr;
bool g_console = false;
std::mutex g_mutex;

void Write(const char* level, const char* fmt, va_list args) {
  char message[2048];
  vsnprintf(message, sizeof(message), fmt, args);

  SYSTEMTIME t;
  GetLocalTime(&t);
  char line[2200];
  snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d] %s %s\n", t.wHour, t.wMinute,
           t.wSecond, t.wMilliseconds, level, message);

  std::lock_guard lock(g_mutex);
  if (g_file) {
    fputs(line, g_file);
    fflush(g_file);
  }
  if (g_console) fputs(line, stdout);
}

}  // namespace

void Init(bool console) {
  g_file = _wfsopen((config::GameDir() + L"rebuild.log").c_str(), L"w", _SH_DENYWR);
  if (console && AllocConsole()) {
    FILE* ignored;
    freopen_s(&ignored, "CONOUT$", "w", stdout);
    SetConsoleTitleA("H1Z1 client rebuild");
    g_console = true;
  }
}

void Info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Write("INFO ", fmt, args);
  va_end(args);
}

void Error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Write("ERROR", fmt, args);
  va_end(args);
}

}  // namespace rebuild::log
