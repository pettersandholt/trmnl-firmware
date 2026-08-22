#pragma once

#include <stddef.h>
#include <stdint.h>

struct Gray4FrameDiff {
  bool valid;
  uint32_t changedPixels;
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

Gray4FrameDiff compareGray4Frames(const uint8_t *previous, size_t previousSize,
                                  const uint8_t *current, size_t currentSize,
                                  uint16_t width, uint16_t height);
