#include <frame_diff.h>
#include <unity.h>

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

void test_plans_aligned_regional_refresh_rows(void) {
  const Gray4FrameDiff diff = {true, 12, 10, 5, 20, 9};
  const Gray4RefreshPlan plan = planGray4Refresh(diff, 100, 80);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_INT32(4, plan.top);
  TEST_ASSERT_EQUAL_INT32(11, plan.bottom);
}

void test_skips_unchanged_frames(void) {
  const Gray4FrameDiff diff = {true, 0, -1, -1, -1, -1};

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_SKIP, planGray4Refresh(diff, 100, 80).kind);
}

void test_falls_back_when_regional_band_is_too_tall(void) {
  const Gray4FrameDiff diff = {true, 12, 0, 0, 20, 79};

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_FULL, planGray4Refresh(diff, 100, 80).kind);
}

void test_clips_aligned_region_to_last_row(void) {
  const Gray4FrameDiff diff = {true, 1, 0, 9, 0, 9};
  const Gray4RefreshPlan plan = planGray4Refresh(diff, 10, 80);

  TEST_ASSERT_EQUAL(GRAY4_REFRESH_REGIONAL, plan.kind);
  TEST_ASSERT_EQUAL_INT32(8, plan.top);
  TEST_ASSERT_EQUAL_INT32(9, plan.bottom);
}

void process(void) {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_invalid_input);
  RUN_TEST(test_identical_frames_have_no_changed_rectangle);
  RUN_TEST(test_counts_nibbles_as_individual_pixels);
  RUN_TEST(test_reports_bounding_rectangle_across_rows);
  RUN_TEST(test_ignores_padding_nibble_for_odd_width);
  RUN_TEST(test_plans_aligned_regional_refresh_rows);
  RUN_TEST(test_skips_unchanged_frames);
  RUN_TEST(test_falls_back_when_regional_band_is_too_tall);
  RUN_TEST(test_clips_aligned_region_to_last_row);
  UNITY_END();
}

int main(int argc, char **argv) {
  process();
  return 0;
}
