// Host benchmark and exact differential check against the pre-optimization source.
// Run from the repository root (requires a C++11 compiler):
//   benchmark_dir=$(mktemp -d)
//   git show 6d673bf:lib/trmnl/src/frame_diff.cpp > "$benchmark_dir/baseline.cpp"
//   c++ -O2 -std=c++11 -I lib/trmnl/include -DBASELINE_SOURCE="\"$benchmark_dir/baseline.cpp\"" scripts/benchmark-frame-diff.cpp -o "$benchmark_dir/benchmark"
//   "$benchmark_dir/benchmark"
// These are host CPU timings, not ESP32 or panel refresh timings.

#include <frame_diff.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#ifndef BASELINE_SOURCE
#error "Define BASELINE_SOURCE as a quoted path to the baseline frame_diff.cpp"
#endif

namespace baseline {
#include BASELINE_SOURCE
}
namespace current {
#include "../lib/trmnl/src/frame_diff.cpp"
}

static bool equal(const Gray4FrameDiff &a, const Gray4FrameDiff &b) {
  if (a.valid != b.valid || a.changedPixels != b.changedPixels ||
      a.left != b.left || a.right != b.right || a.top != b.top ||
      a.bottom != b.bottom || a.regionCount != b.regionCount ||
      a.regionOverflow != b.regionOverflow) return false;
  for (uint8_t i = 0; i < a.regionCount; i++) {
    if (a.regions[i].left != b.regions[i].left || a.regions[i].right != b.regions[i].right ||
        a.regions[i].top != b.regions[i].top || a.regions[i].bottom != b.regions[i].bottom) return false;
  }
  return true;
}

static void pixel(std::vector<uint8_t> &frame, uint16_t width, int x, int y) {
  frame[static_cast<size_t>(y) * ((width + 1) / 2) + x / 2] |= (x & 1) ? 0x0F : 0xF0;
}

static volatile uint64_t checksum = 0;
static void benchmark(const char *name, const std::vector<uint8_t> &before,
                      const std::vector<uint8_t> &after, uint16_t width, uint16_t height) {
  const auto expected = baseline::compareGray4Frames(before.data(), before.size(),
                                                     after.data(), after.size(), width, height);
  std::printf("%s changed=%u regions=%u overflow=%d", name, expected.changedPixels,
              expected.regionCount, expected.regionOverflow);
  for (bool optimized : {false, true}) {
    auto compare = optimized ? current::compareGray4Frames : baseline::compareGray4Frames;
    assert(equal(expected, compare(before.data(), before.size(), after.data(), after.size(), width, height)));
    std::vector<double> samples;
    const int iterations = expected.changedPixels > 100000 ? 15 : 50;
    for (int repetition = 0; repetition < 5; repetition++) {
      const auto start = std::chrono::steady_clock::now();
      for (int i = 0; i < iterations; i++) {
        checksum += compare(before.data(), before.size(), after.data(), after.size(), width, height).changedPixels;
      }
      samples.push_back(std::chrono::duration<double, std::micro>(
                          std::chrono::steady_clock::now() - start).count() / iterations);
    }
    std::sort(samples.begin(), samples.end());
    std::printf(" %s=%.1fus", optimized ? "optimized" : "baseline", samples[2]);
  }
  std::puts("");
}

int main() {
  std::mt19937 random(240905);
  for (int i = 0; i < 2500; i++) {
    const uint16_t width = 1 + random() % 256, height = 1 + random() % 96;
    std::vector<uint8_t> before(((width + 1) / 2) * height), after;
    for (auto &value : before) value = random();
    after = before;
    for (int changes = random() % (width * height); changes > 0; changes--) {
      after[random() % after.size()] ^= random();
    }
    assert(equal(baseline::compareGray4Frames(before.data(), before.size(), after.data(), after.size(), width, height),
                 current::compareGray4Frames(before.data(), before.size(), after.data(), after.size(), width, height)));
  }
  std::puts("2500 deterministic random frame pairs: identical results");

  const uint16_t width = 1872, height = 1404;
  std::vector<uint8_t> before(static_cast<size_t>(width) * height / 2), after(before.size());
  benchmark("identical", before, after, width, height);
  for (int y = 556; y < 578; y++) {
    for (int x = 82; x < 101; x++) if ((x + y) % 3) pixel(after, width, x, y);
  }
  benchmark("clock", before, after, width, height);

  after.assign(after.size(), 0);
  const Gray4Region regions[] = {{662, 51, 729, 118}, {752, 49, 753, 120},
                                 {772, 51, 839, 118}, {721, 1117, 933, 1173}};
  for (const auto &region : regions) {
    for (int y = region.top; y <= region.bottom; y++) {
      for (int x = region.left; x <= region.right; x++) if ((x + y) % 3 == 0) pixel(after, width, x, y);
    }
  }
  benchmark("four_regions", before, after, width, height);

  after.assign(after.size(), 0);
  for (int y = 100; y < 700; y++) {
    for (int x = 100; x < 1000; x++) pixel(after, width, x, y);
  }
  benchmark("dense_rectangle", before, after, width, height);
  after.assign(after.size(), 255);
  benchmark("full_screen", before, after, width, height);
}
