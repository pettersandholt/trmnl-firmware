#!/usr/bin/env python3
"""Run the production X refresh-policy branch and frame planner against a fake panel.

Also guard baseline invalidation before non-dashboard writes. This checks policy
and call ordering, not physical waveforms or driver timing.
"""

import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SUPPORT = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <frame_diff.h>
#define Log_info(...) ((void)0)
#define Log_error(...) ((void)0)
enum { CLEAR_NONE, CLEAR_FAST, CLEAR_SLOW, BBEP_SUCCESS = 0 };
struct BB_RECT { int x, y, w, h; };
struct Panel {
  int regional = 0, full = 0, clear = -1, result = BBEP_SUCCESS;
  std::vector<BB_RECT> rectangles;
  int fullUpdateRegions(int mode, bool power, BB_RECT *rects, int count) {
    assert(mode == CLEAR_FAST && !power);
    ++regional;
    rectangles.assign(rects, rects + count);
    return result;
  }
  void fullUpdate(int mode, bool power) { assert(!power); ++full; clear = mode; }
} bbep;
static struct { struct { bool maximum_compatibility = false; } response; } apiDisplayResult;
static int iUpdateCount = 0, iTempProfile = 0;
static const int width = 1872, height = 1404;
'''

HARNESS = r'''
static void reset(int count = 0) {
  bbep = Panel();
  iUpdateCount = count;
  iTempProfile = 0;
  apiDisplayResult.response.maximum_compatibility = false;
}
static void expectFull(int mode, int regionalAttempts = 0) {
  assert(bbep.full == 1 && bbep.clear == mode && bbep.regional == regionalAttempts);
  assert(iUpdateCount == 0);
}
int main() {
  static_assert(FASTEPD_REGIONAL_REFRESH_LIMIT == 7, "Keep seven-batch cleanup");
  static_assert(FASTEPD_REGIONAL_MAX_HEIGHT_PERCENT == 80, "Keep area/height safeguard");
  const size_t size = width * height / 2;
  std::vector<uint8_t> previous(size, 0xff), current(previous);
  const auto unchanged = compareGray4Frames(previous.data(), size, current.data(), size, width, height);
  current[100 * width / 2 + 100 / 2] = 0x0f;
  current[1300 * width / 2 + 1800 / 2] = 0x0f;
  const auto changed = compareGray4Frames(previous.data(), size, current.data(), size, width, height);
  const auto unavailable = compareGray4Frames(nullptr, 0, current.data(), size, width, height);
  assert(unchanged.valid && unchanged.changedPixels == 0);
  assert(changed.valid && changed.regionCount == 2 && !unavailable.valid);

  reset(3);
  refresh(unchanged);
  assert(bbep.regional == 0 && bbep.full == 0 && iUpdateCount == 3);

  reset(2);
  refresh(changed);
  assert(bbep.regional == 1 && bbep.full == 0 && iUpdateCount == 3);
  assert(bbep.rectangles.size() == 2);
  assert(bbep.rectangles[0].x == 100 && bbep.rectangles[0].y == 100);
  assert(bbep.rectangles[1].x == 1800 && bbep.rectangles[1].y == 1300);
  for (const auto &rect : bbep.rectangles) assert(rect.w == 4 && rect.h == 4);

  reset();
  for (int batch = 1; batch <= 7; ++batch) {
    refresh(changed);
    assert(bbep.regional == batch && bbep.full == 0 && iUpdateCount == batch);
  }
  refresh(changed);
  expectFull(CLEAR_SLOW, 7);
  refresh(changed);
  assert(bbep.regional == 8 && bbep.full == 1 && iUpdateCount == 1);

  reset(7);
  refresh(unchanged); // cleanup remains due even if pixels now match
  expectFull(CLEAR_SLOW);

  reset(2);
  bbep.result = -1;
  refresh(changed);
  expectFull(CLEAR_FAST, 1);

  reset();
  bbep.result = -1;
  refresh(changed);
  expectFull(CLEAR_SLOW, 1);

  reset(2);
  apiDisplayResult.response.maximum_compatibility = true;
  refresh(unchanged);
  expectFull(CLEAR_FAST);

  reset(3);
  iTempProfile = 1;
  refresh(changed);
  expectFull(CLEAR_SLOW);

  reset(2);
  refresh(changed, true); // loading-screen override never uses regional refresh
  expectFull(CLEAR_NONE);

  reset();
  refresh(unavailable);
  expectFull(CLEAR_SLOW);

  reset(4);
  refresh(unavailable);
  expectFull(CLEAR_FAST);

  reset(3);
  current.assign(size, 0);
  refresh(compareGray4Frames(previous.data(), size, current.data(), size, width, height));
  expectFull(CLEAR_FAST);
}
'''


def check_invalidation(display):
    signatures = re.findall(r"^void display_(?:wipe|show_msg(?:_qa)?)\([^\n]*\)\n\{", display, re.M)
    assert len(signatures) == 4, "Review all wipe/message entry points"
    sources = [(display, signature) for signature in signatures]
    indicator = (ROOT / "src/touchbar_indicator.cpp").read_text()
    sources.append((indicator, "void display_draw_touchbar_indicator(touchbar_side_t side) {"))
    writes = re.compile(r"bbep\.(?:setMode|setPanelType|fillScreen|fullUpdate|partialUpdate|refresh|"
                        r"load\w*|draw\w*|print\w*|write\w*|setBuffer)\s*\(|"
                        r"(?:display_prepare_message|touchbar_draw_indicator)\s*\(")
    for source, signature in sources:
        start = source.index(signature) + len(signature)
        body = source[start:source.index("\n}", start)]
        assert body.count("DisplayedImage::cover();") == 1, signature
        cover = body.index("DisplayedImage::cover();")
        first_write = writes.search(body)
        assert first_write and cover < first_write.start(), signature
        if "touchbar_indicator" in signature:
            assert body.index("if (cx < 0) return;") < cover, "Invalid side must leave baseline alone"


def main():
    source = (ROOT / "src/display.cpp").read_text()
    check_invalidation(source)
    start = "        const Gray4RefreshPlan refresh_plan = planGray4Refresh("
    end = "\n#else\n        int iClearMode = bSkipClear"
    assert source.count(start) == 1 and source.count(end) == 1, "Refresh branch moved; update extraction"
    branch = start + source.split(start, 1)[1].split(end, 1)[0]
    limits = re.findall(r"^#define FASTEPD_REGIONAL_(?:MAX_HEIGHT_PERCENT|REFRESH_LIMIT)\s+\d+", source, re.M)
    assert len(limits) == 2
    code = SUPPORT + "\n" + "\n".join(limits)
    code += "\nstatic void refresh(const Gray4FrameDiff &gray4_diff, bool bSkipClear = false) {\n"
    code += branch + "\n}\n" + HARNESS
    with tempfile.TemporaryDirectory(prefix="trmnl-refresh-policy-test-") as directory:
        temp = Path(directory)
        (temp / "test.cpp").write_text(code)
        binary = temp / "test"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-g",
            "-fsanitize=address,undefined", f"-I{ROOT / 'lib/trmnl/include'}",
            str(temp / "test.cpp"), str(ROOT / "lib/trmnl/src/frame_diff.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("refresh-policy: 12 cases PASS (ASan/UBSan); 5 baseline-invalidation order guards PASS")


if __name__ == "__main__":
    main()
