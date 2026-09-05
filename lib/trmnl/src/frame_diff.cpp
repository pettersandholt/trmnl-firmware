#include <frame_diff.h>
#include <string.h>

static Gray4FrameDiff emptyDiff(bool valid) {
  Gray4FrameDiff diff = {};
  diff.valid = valid;
  diff.left = diff.top = diff.right = diff.bottom = -1;
  return diff;
}

static const int32_t REGION_MERGE_GAP = 12;

static bool regionsNear(const Gray4Region &a, const Gray4Region &b) {
  return a.left <= b.right + REGION_MERGE_GAP &&
         b.left <= a.right + REGION_MERGE_GAP &&
         a.top <= b.bottom + REGION_MERGE_GAP &&
         b.top <= a.bottom + REGION_MERGE_GAP;
}

static void mergeRegion(Gray4Region &target, const Gray4Region &source) {
  if (source.left < target.left) target.left = source.left;
  if (source.top < target.top) target.top = source.top;
  if (source.right > target.right) target.right = source.right;
  if (source.bottom > target.bottom) target.bottom = source.bottom;
}

static void mergeNearbyRegions(Gray4FrameDiff &diff) {
  bool merged;
  do {
    merged = false;
    for (uint8_t i = 0; i < diff.regionCount && !merged; i++) {
      for (uint8_t j = i + 1; j < diff.regionCount; j++) {
        if (!regionsNear(diff.regions[i], diff.regions[j])) continue;
        mergeRegion(diff.regions[i], diff.regions[j]);
        for (uint8_t k = j + 1; k < diff.regionCount; k++) {
          diff.regions[k - 1] = diff.regions[k];
        }
        diff.regionCount--;
        merged = true;
        break;
      }
    }
  } while (merged);
}

static void recordRegionPixel(Gray4FrameDiff &diff, int32_t x, int32_t y) {
  if (diff.regionOverflow) return;

  const Gray4Region pixel = {x, y, x, y};
  for (uint8_t i = 0; i < diff.regionCount; i++) {
    if (!regionsNear(diff.regions[i], pixel)) continue;
    const Gray4Region &region = diff.regions[i];
    // Interior pixels cannot change bounds or make another region mergeable.
    if (x >= region.left && x <= region.right &&
        y >= region.top && y <= region.bottom) return;
    mergeRegion(diff.regions[i], pixel);
    mergeNearbyRegions(diff);
    return;
  }

  // A raster scan can temporarily split one final area into several clusters
  // before later rows connect them. Keep those provisional clusters long
  // enough to merge before enforcing the four-region hardware limit.
  if (diff.regionCount >= GRAY4_MAX_TRACKED_REGIONS) {
    diff.regionOverflow = true;
    return;
  }
  diff.regions[diff.regionCount++] = pixel;
}

static void recordChangedPixel(Gray4FrameDiff &diff, int32_t x, int32_t y) {
  if (diff.changedPixels == 0) {
    diff.left = diff.right = x;
    diff.top = diff.bottom = y;
  } else {
    if (x < diff.left) diff.left = x;
    if (x > diff.right) diff.right = x;
    if (y < diff.top) diff.top = y;
    if (y > diff.bottom) diff.bottom = y;
  }
  diff.changedPixels++;
  recordRegionPixel(diff, x, y);
}

Gray4FrameDiff compareGray4Frames(const uint8_t *previous, size_t previousSize,
                                  const uint8_t *current, size_t currentSize,
                                  uint16_t width, uint16_t height) {
  if (previous == NULL || current == NULL || width == 0 || height == 0) {
    return emptyDiff(false);
  }

  const size_t rowBytes = (static_cast<size_t>(width) + 1) / 2;
  const size_t frameBytes = rowBytes * height;
  if (previousSize < frameBytes || currentSize < frameBytes) {
    return emptyDiff(false);
  }

  Gray4FrameDiff diff = emptyDiff(true);
  for (uint16_t y = 0; y < height; y++) {
    const size_t rowOffset = static_cast<size_t>(y) * rowBytes;
    if (memcmp(previous + rowOffset, current + rowOffset, rowBytes) == 0) continue;
    for (size_t byteX = 0; byteX < rowBytes; byteX++) {
      const uint8_t before = previous[rowOffset + byteX];
      const uint8_t after = current[rowOffset + byteX];
      if (before == after) continue;

      const int32_t x = static_cast<int32_t>(byteX * 2);
      if ((before & 0xF0) != (after & 0xF0)) {
        recordChangedPixel(diff, x, y);
      }
      if (x + 1 < width && (before & 0x0F) != (after & 0x0F)) {
        recordChangedPixel(diff, x + 1, y);
      }
    }
  }
  if (diff.regionOverflow || diff.regionCount > GRAY4_MAX_REFRESH_REGIONS) {
    diff.regionOverflow = true;
    diff.regionCount = GRAY4_MAX_REFRESH_REGIONS;
  }
  return diff;
}

Gray4RefreshPlan planGray4Refresh(const Gray4FrameDiff &diff, uint16_t width,
                                  uint16_t height, uint8_t maxHeightPercent) {
  Gray4RefreshPlan full = {};
  full.kind = GRAY4_REFRESH_FULL;
  if (!diff.valid || width == 0 || height == 0 || maxHeightPercent == 0 ||
      maxHeightPercent > 100) return full;
  if (diff.changedPixels == 0) {
    Gray4RefreshPlan skip = {};
    skip.kind = GRAY4_REFRESH_SKIP;
    return skip;
  }
  if (diff.left < 0 || diff.right < diff.left || diff.right >= width ||
      diff.top < 0 || diff.bottom < diff.top || diff.bottom >= height) return full;
  if (diff.regionOverflow || diff.regionCount == 0 ||
      diff.regionCount > GRAY4_MAX_REFRESH_REGIONS) return full;

  Gray4RefreshPlan plan = {};
  plan.kind = GRAY4_REFRESH_REGIONAL;
  plan.regionCount = diff.regionCount;
  uint64_t coveredPixels = 0;
  for (uint8_t i = 0; i < diff.regionCount; i++) {
    const Gray4Region &source = diff.regions[i];
    if (source.left < 0 || source.right < source.left || source.right >= width ||
        source.top < 0 || source.bottom < source.top || source.bottom >= height) return full;

    Gray4Region &region = plan.regions[i];
    region.left = source.left & ~3;
    region.right = source.right | 3;
    if (region.right >= width) region.right = width - 1;
    region.top = source.top & ~3;
    region.bottom = source.bottom | 3;
    if (region.bottom >= height) region.bottom = height - 1;

    const uint32_t rows = static_cast<uint32_t>(region.bottom - region.top + 1);
    if (rows * 100 >= static_cast<uint32_t>(height) * maxHeightPercent) return full;
    coveredPixels += static_cast<uint64_t>(region.right - region.left + 1) * rows;
  }

  const uint64_t displayPixels = static_cast<uint64_t>(width) * height;
  if (coveredPixels * 100 >= displayPixels * maxHeightPercent) return full;

  return plan;
}
