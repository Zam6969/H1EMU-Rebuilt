#pragma once

namespace rebuild::log {

void Init(bool console);
void Info(const char* fmt, ...);
void Error(const char* fmt, ...);

}  // namespace rebuild::log
