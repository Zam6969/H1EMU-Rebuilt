// Verifies every REBUILD_FUNCTION hook installs against the real H1Z1.exe
// without running the game: maps the exe as an image at its fixed base,
// loads the rebuild DLL, and calls its RebuildSelfTest export.
//
//   hookcheck.exe [path\to\H1Z1.exe]
// Exit code = number of failed hooks. Details go to rebuild.log beside hookcheck.exe.
#include <windows.h>

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  const wchar_t* exe = argc > 1 ? argv[1] : L"C:\\Users\\zam\\Documents\\H1emu\\H1Z1.exe";
  HMODULE game = LoadLibraryExW(exe, nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (reinterpret_cast<uintptr_t>(game) != 0x140000000) {
    fwprintf(stderr, L"could not map %s at 0x140000000 (got %p, error %lu)\n", exe, game,
             GetLastError());
    return 1000;
  }

  wchar_t dll[MAX_PATH];
  GetModuleFileNameW(nullptr, dll, MAX_PATH);
  wcscpy(wcsrchr(dll, L'\\') + 1, L"version.dll");
  HMODULE rebuild = LoadLibraryW(dll);
  auto selfTest = rebuild ? reinterpret_cast<int (*)()>(GetProcAddress(rebuild, "RebuildSelfTest"))
                          : nullptr;
  if (!selfTest) {
    fwprintf(stderr, L"could not load %s (error %lu)\n", dll, GetLastError());
    return 1001;
  }

  int failed = selfTest();
  printf("hookcheck: %d hook(s) failed - see rebuild.log\n", failed);
  return failed;
}
