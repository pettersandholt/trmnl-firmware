# FastEPD backlog for the v1.8.18 integration

Reviewed 2026-10-31. This integration retains custom FastEPD
`4732e2271b68b51f8dc3c89b43e685b3f0b3c5a3`; it does **not** change driver code,
panel timing, padding, or the dependency pin.

Upstream firmware v1.8.17 and v1.8.18 both pin
`9a84c35e52849f9f22d2a7ad5231ef501be49e04`. The comparison baseline shared by
that driver and our fork is `855ce9a42c0be5cd8ce2f2425483a8271a04668d`.
This is a bounded review of that deferred driver gap, not an audit of later
FastEPD releases. See the [upstream changes][comparison].

## Retain our custom behavior

- Native 4-bit multi-region refresh, mirrored-coordinate handling, and masks
  that leave pixels outside the selected regions undriven. Upstream still lacks
  our `fullUpdateRegions()` API.
- [DMA completion waits][dma-fix] before clear-buffer reuse and after the final
  transfer. These fix buffer ownership without changing waveform settings.
- Firmware-side previous-frame capture, unchanged-frame suppression, no loading
  logo on scheduled wakes, and full cleanup after seven regional updates.
  These are intended features, not temporary shortcuts to remove in a merge.
- Keep the existing LilyGo device-table guard while using this pin: its header
  lacks upstream's `BBEP_DISPLAY_ED047TC1` enum.

## Applicable non-timing changes: separate follow-up candidates

| Change | Relevance and next decision |
| --- | --- |
| [Explicit 16-byte DMA-buffer allocation][alignment] | Used by the X; worth a small, separate allocation-hardening patch after checking allocation failure and both alternating row-buffer addresses. It is not a substitute for our DMA waits, and no performance gain has been measured. |
| [4-bit `backupPlane()` guard][backup] | Prevents a real overwrite, but firmware currently calls neither `backupPlane()` nor its `clearWhite()` / `clearBlack()` wrappers. A small defensive backport is reasonable; it is not an observed dashboard fix. |
| [Parallel-bus teardown in `deInit()`][deinit] | Firmware calls `deInit()` from `display_sleep()`. Our version invokes board cleanup only; upstream additionally deletes LCD I/O and bus handles. Audit outstanding DMA and reinitialization before a separate lifecycle patch. No current leak or power saving has been demonstrated. |

These changes justify a follow-up driver patch review, not an automatic driver
upgrade or an expansion of this firmware merge.

## Hardware / timing changes: defer to an isolated experiment

- TRMNL X line padding: **16 to 44 bytes**, plus removal of its
  `BB_PANEL_FLAG_DARK` flag ([panel definition][panel]).
- Two EPDiy V7 row-start delays: **18 to 10 microseconds**, plus placement of
  row/write/clear/partial-update functions in IRAM ([row timing][timing]).

Keep these out of the non-timing follow-up. The X bus clock remains 26,666,666 Hz
and VCOM remains -1100 mV across these versions. Changes to pulse spacing,
padding, code placement, or drive policy require physical-panel comparisons:
right-edge pixels, black/white and all gray levels, separated small regions,
unchanged areas, menu-to-dashboard restoration, and periodic cleanup. Host tests
cannot establish image quality, ghosting, or panel lifetime.

## Not needed for this X integration

Linux, IT8951, other-board support, and IDF-6 compatibility are not requirements
of the current ESP32-S3 / IDF-5.5 build. SIMD is not used by this X's normal
refresh path; its [original author reported][simd] PSRAM latency prevented a
speedup. `BB_SPEED_BITBANG` is
[explicitly clear-only][bitbang]: it repeats the first 16-bit pattern across each
row and cannot replace normal dashboard image transfers.

## Regression evidence

On 2026-10-31, from a clean checkout of the exact custom pin:

```sh
bash tests/run_clear_dma.sh
```

Passed with AddressSanitizer and UndefinedBehaviorSanitizer. The script extracts
the actual production mask/clear functions and tests delayed DMA completion,
four regions, overlap and sub-byte edges, mirrored rows, all clear codes,
repeated passes, and full-screen clear. This validates the retained DMA fix;
it does not test a newer driver, ESP32 DMA hardware, or waveform timing.

[comparison]: https://github.com/bitbank2/FastEPD/compare/855ce9a42c0be5cd8ce2f2425483a8271a04668d...9a84c35e52849f9f22d2a7ad5231ef501be49e04
[dma-fix]: https://github.com/pettersandholt/FastEPD/commit/4732e2271b68b51f8dc3c89b43e685b3f0b3c5a3
[alignment]: https://github.com/bitbank2/FastEPD/blob/9a84c35e52849f9f22d2a7ad5231ef501be49e04/src/FastEPD.inl#L1879
[backup]: https://github.com/bitbank2/FastEPD/commit/3e47f1e3ab6d5e00a979e1c32557bb3bc89e8653
[deinit]: https://github.com/bitbank2/FastEPD/blob/9a84c35e52849f9f22d2a7ad5231ef501be49e04/src/FastEPD.cpp#L759
[panel]: https://github.com/bitbank2/FastEPD/blob/9a84c35e52849f9f22d2a7ad5231ef501be49e04/src/FastEPD.inl#L230
[timing]: https://github.com/bitbank2/FastEPD/blob/9a84c35e52849f9f22d2a7ad5231ef501be49e04/src/FastEPD.inl#L1367
[bitbang]: https://github.com/bitbank2/FastEPD/commit/7580f9ba51873ce466d526f9a4d7095436dcbf29
[simd]: https://github.com/bitbank2/FastEPD/commit/b248b077271b3d99d3fa213d971bef96b2435e0a
