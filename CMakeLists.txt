cmake_minimum_required(VERSION 3.20)
project(h1z1_client_rebuild LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "H1Z1.exe is x64 - configure with -A x64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL needs no redistributable next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook, compiled straight into the DLL.
set(MH third_party/minhook/src)
add_library(minhook STATIC
  ${MH}/buffer.c ${MH}/hook.c ${MH}/trampoline.c ${MH}/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

# The rebuild ships as a version.dll proxy: H1Z1.exe imports VERSION.dll, so
# Windows loads this from the game folder before the game's entry point runs.
file(GLOB_RECURSE REBUILD_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_library(rebuild SHARED ${REBUILD_SOURCES})
set_target_properties(rebuild PROPERTIES OUTPUT_NAME version)
target_include_directories(rebuild PRIVATE src)
target_compile_definitions(rebuild PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_compile_options(rebuild PRIVATE /W4 /permissive- /Zi)
target_link_options(rebuild PRIVATE /DEBUG)
target_link_libraries(rebuild PRIVATE minhook ws2_32 winmm)

# Offline hook verification (see tools/hookcheck.cpp). Based away from
# 0x140000000 so the real H1Z1.exe image can be mapped at its fixed base.
add_executable(hookcheck tools/hookcheck.cpp)
target_compile_definitions(hookcheck PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_options(hookcheck PRIVATE /BASE:0x180000000 /FIXED:NO)
add_dependencies(hookcheck rebuild)
