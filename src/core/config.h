#pragma once

#include <string>

// rebuild.ini, next to H1Z1.exe:
//   [general]
//   console=1        ; open a log console window
//   enabled=1        ; 0 = load nothing, run the stock client
//   [hooks]
//   UdpPlatformDriver_SocketSend=0   ; use the original for this one
namespace rebuild::config {

void Load();
const std::wstring& GameDir();  // with trailing backslash
bool Enabled();
bool Console();
bool HookEnabled(const char* name);

}  // namespace rebuild::config
