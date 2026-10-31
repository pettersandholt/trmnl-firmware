#!/usr/bin/env python3
"""Run the real logger's formatting/routing code with host stubs, without ESP32 hardware."""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SUPPORT = r'''
#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <trmnl_log.h>

static std::string printed;
static int allocations = 0, stored = 0, submitted = 0;
static void *tracked_malloc(size_t size) {
  void *p = std::malloc(size);
  if (p) ++allocations;
  return p;
}
static void tracked_free(void *p) {
  if (p) --allocations;
  std::free(p);
}
struct SerialStub {
  void println(const char *text) { printed = text; }
} Serial;
struct LogStub {
  void output(const char *format, const char *text) {
    assert(std::strcmp(format, "%s") == 0);
    printed = text;
  }
  // The default arg makes an unsafe single-argument call fail the assertion,
  // rather than consuming nonexistent varargs as the device library would.
  void verboseln(const char *f, const char *s = nullptr) { output(f, s); }
  void infoln(const char *f, const char *s = nullptr) { output(f, s); }
  void warningln(const char *f, const char *s = nullptr) { output(f, s); }
  void errorln(const char *f, const char *s = nullptr) { output(f, s); }
  void fatalln(const char *f, const char *s = nullptr) { output(f, s); }
} Log;
enum LogAction { LOG_ACTION_STORE, LOG_ACTION_SUBMIT_OR_STORE };
struct ClockStub { time_t getTime() { return 1; } } clockStub;
static ClockStub &systemClock() { return clockStub; }
static void logWithAction(LogAction action, LogLevel, const char *, time_t, int, const char *) {
  if (action == LOG_ACTION_STORE) ++stored;
  else ++submitted;
}
static void format_message_truncated(char *out, int size, const char *format, va_list args) {
  if (std::vsnprintf(out, size, format, args) >= size) std::strcpy(out + size - 4, "...");
}
'''
HARNESS = r'''
#include "support.h"
#define malloc tracked_malloc
#define free tracked_free
#include "logger_under_test.cpp"
#undef malloc
#undef free

int main(int argc, char **argv) {
  assert(argc == 2);
  const char *message = "50% done; literal %s and %d";
  const std::string expected = std::string("test.cpp [42]: ") + message;
  if (std::strcmp(argv[1], "serial") == 0) {
    for (int i = 0; i < 100; ++i) {
      log_impl(LOG_ERROR, LOG_SERIAL_ONLY, "test.cpp", 42, "%s", message);
      assert(printed == expected);
      assert(allocations == 0);
      assert(stored == 0 && submitted == 0);
    }
  } else {
    for (int level = LOG_VERBOSE; level <= LOG_FATAL; ++level) {
      log_impl(static_cast<LogLevel>(level), LOG_STORE_ONLY, "test.cpp", 42, "%s", message);
      assert(printed == expected);
      assert(allocations == 0);
    }
    assert(stored == 2 && submitted == 0);
    log_impl(LOG_ERROR, LOG_SUBMIT_OR_STORE, "test.cpp", 42, "%s", message);
    assert(printed == expected && allocations == 0);
    assert(stored == 2 && submitted == 1);
  }
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="trmnl-logger-test-") as directory:
        temp = Path(directory)
        # Upstream moved network/storage helpers into this file. Keep the same
        # test boundary: the real formatter/router, with logWithAction stubbed.
        source = (ROOT / "src/app_logger.cpp").read_text()
        start = source.index("static LogLevel store_submit_threshold")
        (temp / "logger_under_test.cpp").write_text(source[start:])
        (temp / "support.h").write_text(SUPPORT)
        (temp / "test.cpp").write_text(HARNESS)
        binary = temp / "test"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra",
            f"-I{temp}", f"-I{ROOT / 'src'}", f"-I{ROOT / 'lib/trmnl/include'}",
            str(temp / "test.cpp"), "-o", str(binary),
        ], check=True)
        failed = False
        for case in ("serial", "format-and-routing"):
            result = subprocess.run([str(binary), case])
            print(f"{case}: {'PASS' if result.returncode == 0 else 'FAIL'}", flush=True)
            failed |= result.returncode != 0
        if failed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
