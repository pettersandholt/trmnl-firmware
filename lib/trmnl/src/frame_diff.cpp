#include <frame_diff.h>

static Gray4FrameDiff emptyDiff(bool valid) {
  Gray4FrameDiff diff = {valid, 0, -1, -1, -1, -1};
  return diff;
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
  return diff;
}
