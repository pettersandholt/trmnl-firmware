#include <frame_diff.h>
#include <unity.h>

static Gray4FrameDiff singleRegionDiff(uint32_t changedPixels, int32_t left, int32_t top,
                                       int32_t right, int32_t bottom) {
  Gray4FrameDiff diff = {};
  diff.valid = true;
  diff.changedPixels = changedPixels;
  diff.left = left;
  diff.top = top;
  diff.right = right;
  diff.bottom = bottom;
  diff.regionCount = 1;
  diff.regions[0] = {left, top, right, bottom};
  return diff;
}

static void setGray4Pixel(uint8_t *frame, uint16_t width, uint16_t x, uint16_t y,
                          uint8_t value) {
  const size_t offset = static_cast<size_t>(y) * ((width + 1) / 2) + x / 2;
  if ((x & 1) == 0) {
    frame[offset] = (frame[offset] & 0x0F) | ((value & 0x0F) << 4);
  } else {
    frame[offset] = (frame[offset] & 0xF0) | (value & 0x0F);
  }
}

void test_rejects_invalid_input(void) {
  const uint8_t frame[] = {0x00};

  TEST_ASSERT_FALSE(compareGray4Frames(NULL, 0, frame, 1, 2, 1).valid);
  TEST_ASSERT_FALSE(compareGray4Frames(frame, 1, NULL, 0, 2, 1).valid);
  TEST_ASSERT_FALSE(compareGray4Frames(frame, 1, frame, 1, 0, 1).valid);
  TEST_ASSERT_FALSE(compareGray4Frames(frame, 0, frame, 1, 2, 1).valid);
}

void test_identical_frames_have_no_changed_rectangle(void) {
  const uint8_t frame[] = {0x12, 0x34, 0x56, 0x78};
  const Gray4FrameDiff diff = compareGray4Frames(frame, sizeof(frame), frame, sizeof(frame), 4, 2);

  TEST_ASSERT_TRUE(diff.valid);
  TEST_ASSERT_EQUAL_UINT32(0, diff.changedPixels);
  TEST_ASSERT_EQUAL_INT32(-1, diff.left);
  TEST_ASSERT_EQUAL_INT32(-1, diff.top);
  TEST_ASSERT_EQUAL_INT32(-1, diff.right);
  TEST_ASSERT_EQUAL_INT32(-1, diff.bottom);
  TEST_ASSERT_EQUAL_UINT8(0, diff.regionCount);
  TEST_ASSERT_FALSE(diff.regionOverflow);
}

void test_counts_nibbles_as_individual_pixels(void) {
  const uint8_t previous[] = {0x12};
  const uint8_t current[] = {0xA3};
  const Gray4FrameDiff diff = compareGray4Frames(previous, sizeof(previous), current, sizeof(current), 2, 1);

  TEST_ASSERT_EQUAL_UINT32(2, diff.changedPixels);
  TEST_ASSERT_EQUAL_INT32(0, diff.left);
  TEST_ASSERT_EQUAL_INT32(1, diff.right);
  TEST_ASSERT_EQUAL_INT32(0, diff.top);
  TEST_ASSERT_EQUAL_INT32(0, diff.bottom);
  TEST_ASSERT_EQUAL_UINT8(1, diff.regionCount);
}

void test_reports_bounding_rectangle_across_rows(void) {
  const uint8_t previous[9] = {0};
  const uint8_t current[9] = {
      0x00, 0x00, 0x01,
      0x00, 0x00, 0x00,
      0xFF, 0x00, 0x00,
  };
  const Gray4FrameDiff diff = compareGray4Frames(previous, sizeof(previous), current, sizeof(current), 6, 3);

  TEST_ASSERT_EQUAL_UINT32(3, diff.changedPixels);
  TEST_ASSERT_EQUAL_INT32(0, diff.left);
  TEST_ASSERT_EQUAL_INT32(0, diff.top);
  TEST_ASSERT_EQUAL_INT32(5, diff.right);
  TEST_ASSERT_EQUAL_INT32(2, diff.bottom);
}

void test_ignores_padding_nibble_for_odd_width(void) {
  const uint8_t previous[] = {0x00, 0x00};
  const uint8_t paddingOnly[] = {0x00, 0x0F};
  const uint8_t lastPixel[] = {0x00, 0xF0};

  TEST_ASSERT_EQUAL_UINT32(0, compareGray4Frames(previous, 2, paddingOnly, 2, 3, 1).changedPixels);

  const Gray4FrameDiff diff = compareGray4Frames(previous, 2, lastPixel, 2, 3, 1);
  TEST_ASSERT_EQUAL_UINT32(1, diff.changedPixels);
  TEST_ASSERT_EQUAL_INT32(2, diff.left);
  TEST_ASSERT_EQUAL_INT32(2, diff.right);
}

void test_plans_aligned_regional_refresh_rectangle(void) {
  const Gray4FrameDiff diff = singleRegionDiff(12, 10, 5, 20, 9);
  const Gray4RefreshPlan plan = planGray4Refresh(diff, 100, 100, 80);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_UINT8(1, plan.regionCount);
  TEST_ASSERT_EQUAL_INT32(8, plan.regions[0].left);
  TEST_ASSERT_EQUAL_INT32(4, plan.regions[0].top);
  TEST_ASSERT_EQUAL_INT32(23, plan.regions[0].right);
  TEST_ASSERT_EQUAL_INT32(11, plan.regions[0].bottom);
}

void test_plans_tight_rectangle_for_observed_clock_change(void) {
  const Gray4FrameDiff diff = singleRegionDiff(281, 82, 556, 100, 577);
  const Gray4RefreshPlan plan = planGray4Refresh(diff, 1872, 1404, 80);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_UINT8(1, plan.regionCount);
  TEST_ASSERT_EQUAL_INT32(80, plan.regions[0].left);
  TEST_ASSERT_EQUAL_INT32(556, plan.regions[0].top);
  TEST_ASSERT_EQUAL_INT32(103, plan.regions[0].right);
  TEST_ASSERT_EQUAL_INT32(579, plan.regions[0].bottom);
}

void test_skips_unchanged_frames(void) {
  Gray4FrameDiff diff = {};
  diff.valid = true;
  diff.left = diff.top = diff.right = diff.bottom = -1;

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_SKIP, planGray4Refresh(diff, 100, 100, 80).kind);
}

void test_falls_back_when_regional_band_is_too_tall(void) {
  const Gray4FrameDiff diff = singleRegionDiff(12, 0, 0, 20, 79);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_FULL, planGray4Refresh(diff, 100, 100, 80).kind);
}

void test_clips_aligned_region_to_last_row(void) {
  const Gray4FrameDiff diff = singleRegionDiff(1, 0, 9, 0, 9);
  const Gray4RefreshPlan plan = planGray4Refresh(diff, 10, 10, 80);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_INT32(0, plan.regions[0].left);
  TEST_ASSERT_EQUAL_INT32(8, plan.regions[0].top);
  TEST_ASSERT_EQUAL_INT32(3, plan.regions[0].right);
  TEST_ASSERT_EQUAL_INT32(9, plan.regions[0].bottom);
}

void test_plans_two_distant_dashboard_changes_as_separate_regions(void) {
  const uint16_t width = 100;
  uint8_t previous[1000] = {};
  uint8_t current[1000] = {};
  setGray4Pixel(current, width, 5, 5, 0x0F);
  setGray4Pixel(current, width, 80, 5, 0x0F);

  const Gray4FrameDiff diff = compareGray4Frames(previous, sizeof(previous), current,
                                                 sizeof(current), width, 20);
  const Gray4RefreshPlan plan = planGray4Refresh(diff, width, 20, 80);

  TEST_ASSERT_EQUAL_UINT32(2, diff.changedPixels);
  TEST_ASSERT_EQUAL_UINT8(2, diff.regionCount);
  TEST_ASSERT_FALSE(diff.regionOverflow);
  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_UINT8(2, plan.regionCount);
  TEST_ASSERT_EQUAL_INT32(4, plan.regions[0].left);
  TEST_ASSERT_EQUAL_INT32(4, plan.regions[0].top);
  TEST_ASSERT_EQUAL_INT32(7, plan.regions[0].right);
  TEST_ASSERT_EQUAL_INT32(7, plan.regions[0].bottom);
  TEST_ASSERT_EQUAL_INT32(80, plan.regions[1].left);
  TEST_ASSERT_EQUAL_INT32(4, plan.regions[1].top);
  TEST_ASSERT_EQUAL_INT32(83, plan.regions[1].right);
  TEST_ASSERT_EQUAL_INT32(7, plan.regions[1].bottom);
}

void test_merges_nearby_changes_into_one_region(void) {
  const uint16_t width = 40;
  uint8_t previous[400] = {};
  uint8_t current[400] = {};
  setGray4Pixel(current, width, 5, 5, 0x0F);
  setGray4Pixel(current, width, 17, 5, 0x0F);

  const Gray4FrameDiff diff = compareGray4Frames(previous, sizeof(previous), current,
                                                 sizeof(current), width, 20);

  TEST_ASSERT_EQUAL_UINT8(1, diff.regionCount);
  TEST_ASSERT_EQUAL_INT32(5, diff.regions[0].left);
  TEST_ASSERT_EQUAL_INT32(17, diff.regions[0].right);
}

void test_falls_back_after_more_than_four_regions(void) {
  const uint16_t width = 160;
  uint8_t previous[1600] = {};
  uint8_t current[1600] = {};
  const uint16_t changedX[] = {0, 30, 60, 90, 120};
  for (uint16_t x : changedX) setGray4Pixel(current, width, x, 5, 0x0F);

  const Gray4FrameDiff diff = compareGray4Frames(previous, sizeof(previous), current,
                                                 sizeof(current), width, 20);

  TEST_ASSERT_EQUAL_UINT8(GRAY4_MAX_REFRESH_REGIONS, diff.regionCount);
  TEST_ASSERT_TRUE(diff.regionOverflow);
  TEST_ASSERT_EQUAL(GRAY4_REFRESH_FULL, planGray4Refresh(diff, width, 20, 80).kind);
}

void process(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_invalid_input);
  RUN_TEST(test_identical_frames_have_no_changed_rectangle);
  RUN_TEST(test_counts_nibbles_as_individual_pixels);
  RUN_TEST(test_reports_bounding_rectangle_across_rows);
  RUN_TEST(test_ignores_padding_nibble_for_odd_width);
  RUN_TEST(test_plans_aligned_regional_refresh_rectangle);
  RUN_TEST(test_plans_tight_rectangle_for_observed_clock_change);
  RUN_TEST(test_skips_unchanged_frames);
  RUN_TEST(test_falls_back_when_regional_band_is_too_tall);
  RUN_TEST(test_clips_aligned_region_to_last_row);
  RUN_TEST(test_plans_two_distant_dashboard_changes_as_separate_regions);
  RUN_TEST(test_merges_nearby_changes_into_one_region);
  RUN_TEST(test_falls_back_after_more_than_four_regions);
  UNITY_END();
}

int main(int argc, char **argv) {
  process();
  return 0;
}
