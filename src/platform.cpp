#include "platform.h"

#include <cstdio>

#ifdef _WIN32
// Kept to this file: <windows.h> defines macros (near, far, min, max) that
// would collide with ordinary names elsewhere.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

Utf8Console::Utf8Console() : previous_(GetConsoleOutputCP()) {
  if (previous_ != 0) SetConsoleOutputCP(CP_UTF8);
}

Utf8Console::~Utf8Console() {
  std::fflush(stdout);  // anything still buffered was written as UTF-8
  if (previous_ != 0) SetConsoleOutputCP(previous_);
}
#else
Utf8Console::Utf8Console() {}
Utf8Console::~Utf8Console() {}
#endif
