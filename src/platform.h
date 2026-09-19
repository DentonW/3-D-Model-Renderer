// The one piece of platform-specific code: the Windows console.
#pragma once

// While it exists, the console shows UTF-8, so model names outside the
// legacy code page print as they are; the old code page goes back after. A
// no-op elsewhere, and when there is no console.
class Utf8Console {
 public:
  Utf8Console();
  ~Utf8Console();
  Utf8Console(const Utf8Console &) = delete;
  Utf8Console &operator=(const Utf8Console &) = delete;

 private:
  unsigned int previous_ = 0;
};
