#!/usr/bin/env python3
"""Exercise the production gray4 capture/render/cache sequence without ESP32 hardware.

Compile the helper and PNG/JPEG branch from display_session.cpp, the real retained
image state and PNG-header parser. Device I/O and decoding are checked host stubs;
this is not an end-to-end HTTP or panel test.
"""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SUPPORT = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <png.h>
#include <trmnl_log.h>
#include "displayed_image.h"

struct String : std::string {
  using std::string::string;
  bool isEmpty() const { return empty(); }
};
struct Preferences {
  String getString(const char *, const char *) { return "/previous"; }
  void putString(const char *, const String &) {}
} preferences;
struct LogStub {
  template<typename... Args> void info(const char *, Args...) {}
} Log;
struct ApiResult { struct { String filename; } response; } apiDisplayResult;
static const char *PREFERENCES_CURRENT_PATH_KEY = "current";
static const char *PREFERENCES_LAST_PATH_KEY = "last";
static const char *PREFERENCES_BROWSE_PATH_KEY = "browse";
static int wakeup_reason = 4;
static bool missingFile, allocationFailure, decodeFailure, expectedBaseline;
static uint8_t *compressed, *allocatedFrame;
static std::vector<std::string> events;
static uint8_t incoming[33];
static uint8_t *buffer = incoming;
static uint32_t content_size = sizeof(incoming);
static int display_width() { return 1872; }
static int display_height() { return 1404; }
static const size_t FRAME_BYTES = 1872 * 1404 / 2;

static uint8_t *display_read_file(const char *path, int *size) {
  events.push_back("read");
  assert(DisplayedImage::exists());
  assert(std::strcmp(path, "/previous") == 0);
  if (missingFile) return nullptr;
  compressed = static_cast<uint8_t *>(std::malloc(7));
  assert(compressed);
  *size = 7;
  return compressed;
}
static void *ps_malloc(size_t size) {
  events.push_back("allocate");
  assert(size == FRAME_BYTES);
  if (allocationFailure) return nullptr;
  allocatedFrame = static_cast<uint8_t *>(std::malloc(size));
  assert(allocatedFrame);
  return allocatedFrame;
}
static bool display_decode_gray4(const uint8_t *png, int size, uint8_t *frame, size_t frameSize) {
  events.push_back("decode");
  assert(png == compressed && size == 7);
  assert(frame == allocatedFrame && frameSize == FRAME_BYTES);
  if (decodeFailure) return false;
  std::memset(frame, 0xa5, frameSize);
  return true;
}
static void tracked_free(void *pointer) {
  if (!pointer) return;
  if (pointer == compressed) {
    events.push_back("free_png");
    compressed = nullptr;
  } else {
    assert(pointer == allocatedFrame);
    events.push_back("free_frame");
    allocatedFrame = nullptr;
  }
  std::free(pointer);
}
static void display_show_image(uint8_t *image, int size, bool wait, bool skipClear = false,
                               const uint8_t *previous = nullptr, size_t previousSize = 0) {
  events.push_back("display");
  assert(image == incoming && size == sizeof(incoming) && wait && !skipClear);
  assert(!compressed);
  if (expectedBaseline) {
    assert(previous && previous == allocatedFrame && previousSize == FRAME_BYTES);
    assert(previous[0] == 0xa5 && previous[previousSize - 1] == 0xa5);
    assert(DisplayedImage::matches("/previous"));
  } else {
    assert(!previous && previousSize == 0 && !allocatedFrame);
  }
}
static void filesystem_fix_filename(const char *name, char *out) { std::strcpy(out, name); }
static void filesystem_purge_old_file(const char *path) {
  events.push_back("purge");
  assert(std::strcmp(path, "/next") == 0);
  assert(!compressed && !allocatedFrame);
  assert(!DisplayedImage::matches("/next"));
}
static void writeImageToFile(const char *path, uint8_t *image, uint32_t size) {
  events.push_back("write");
  assert(std::strcmp(path, "/next") == 0 && image == incoming && size == sizeof(incoming));
  assert(!DisplayedImage::matches("/next"));
}
static void update_playlist_order(Preferences &, const char *, const char *) {}
enum bmp_err_e { BMP_NOT_BMP };
static bmp_err_e parseBMPHeader(uint8_t *, bool &) { return BMP_NOT_BMP; }
'''

HARNESS = r'''
static void reset() {
  assert(!compressed && !allocatedFrame);
  events.clear();
  missingFile = allocationFailure = decodeFailure = expectedBaseline = false;
  DisplayedImage::remember("/previous");
  apiDisplayResult.response.filename = "/next";
  std::memset(incoming, 0, sizeof(incoming));
  const uint8_t signature[] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  std::memcpy(incoming, signature, sizeof(signature));
  incoming[11] = 13;
  std::memcpy(incoming + 12, "IHDR", 4);
  incoming[18] = 7; incoming[19] = 80; // 1872
  incoming[22] = 5; incoming[23] = 124; // 1404
  incoming[24] = 4;
}
static void expect(const std::vector<std::string> &sequence, bool displayed = true) {
  assert(events == sequence);
  assert(!compressed && !allocatedFrame);
  if (displayed) assert(DisplayedImage::matches("/next"));
}
int main() {
  reset();
  expectedBaseline = true;
  render_download(true, false);
  expect({"read", "allocate", "decode", "free_png", "display", "free_frame", "purge", "write"});

  reset();
  DisplayedImage::remember("");
  render_download(true, false);
  expect({"display", "purge", "write"});

  reset();
  DisplayedImage::cover();
  assert(std::strcmp(DisplayedImage::get(), "/previous") == 0);
  assert(!DisplayedImage::exists() && !DisplayedImage::matches("/previous"));
  render_download(true, false);
  expect({"display", "purge", "write"});
  assert(DisplayedImage::exists()); // remember() makes the new image eligible again

  reset();
  missingFile = true;
  render_download(true, false);
  expect({"read", "display", "purge", "write"});

  reset();
  allocationFailure = true;
  render_download(true, false);
  expect({"read", "allocate", "free_png", "display", "purge", "write"});

  reset();
  decodeFailure = true; // malformed, wrong dimensions or non-native gray4 baseline
  render_download(true, false);
  expect({"read", "allocate", "decode", "free_frame", "free_png", "display", "purge", "write"});

  reset();
  incoming[0] = 0xff; incoming[1] = 0xd8;
  render_download(false, true);
  expect({"display", "purge", "write"});

  reset();
  incoming[0] = '<'; // e.g. HTML returned with an image/png content type
  render_download(true, false);
  expect({}, false);
  assert(DisplayedImage::matches("/previous"));

  reset();
  incoming[18] = incoming[19] = 0;
  render_download(true, false);
  expect({}, false);
  assert(DisplayedImage::matches("/previous"));
}
'''


def between(source, start, end):
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError("Production section moved; update the host harness extraction")
    return source.split(start, 1)[1].split(end, 1)[0]


def main():
    source = (ROOT / "src/display_session.cpp").read_text()
    helper = "static uint8_t *load_prev_gray4_frame" + between(
        source, "static uint8_t *load_prev_gray4_frame", "\n#endif\n\nhttps_request_err_e downloadAndShow")
    render = "  bool image_reverse = false;\n  if (isPNG)" + between(
        source, "  bool image_reverse = false;\n  if (isPNG)", "\n  Serial.println();")
    code = '#include "support.h"\n#include "displayed_image.cpp"\n#define free tracked_free\n'
    code += helper + "\nstatic void render_download(bool isPNG, bool isJPEG) {\n"
    code += "image_err_e png_res = PNG_DECODE_ERR; bmp_err_e bmp_res = BMP_NOT_BMP;\n"
    code += render + "\n(void)bmp_res;\n}\n#undef free\n" + HARNESS
    with tempfile.TemporaryDirectory(prefix="trmnl-display-session-test-") as directory:
        temp = Path(directory)
        (temp / "esp_attr.h").write_text("#define RTC_DATA_ATTR\n")
        (temp / "trmnl_log.h").write_text(
            "#pragma once\n#define Log_info(...) ((void)0)\n#define Log_error(...) ((void)0)\n")
        (temp / "support.h").write_text(SUPPORT)
        (temp / "test.cpp").write_text(code)
        binary = temp / "test"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-g",
            "-fsanitize=address,undefined", "-DBOARD_TRMNL_X",
            f"-I{temp}", f"-I{ROOT / 'src'}", f"-I{ROOT / 'lib/trmnl/include'}",
            str(temp / "test.cpp"), str(ROOT / "lib/trmnl/src/png.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
        print("display-session: 9 lifecycle/validation cases PASS (ASan/UBSan)")


if __name__ == "__main__":
    main()
