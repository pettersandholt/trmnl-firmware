# TRMNL X custom firmware maintenance

## v1.8.18 candidate

This branch merges upstream `v1.8.18` (`4ffa33acbe317e559aa50ee505ffa5fe4ed0272d`)
into our tested v1.8.17 fork (`1315ba9a21950fbd906e63d2d6807c66ec4b1a0b`).
The previous v1.8.16 and v1.8.17 releases were already merged, not merely
cherry-picked. Keep upstream's new module layout; do not restore the old `bl.cpp`.

FastEPD remains pinned to `4732e2271b68b51f8dc3c89b43e685b3f0b3c5a3`.
No driver code, waveform, panel timing or padding changes are included.
See [the separate driver backlog](FASTEPD_BACKLOG.md) for the reviewed gap.

## Intentional differences from upstream

| Area | Why it remains / integration rule | Verification |
| --- | --- | --- |
| `frame_diff` and native gray4 rendering | Preserve all 16 gray levels, unchanged-image skipping and up to four regions. Keep the 80% regional height/coverage limit. | Native frame-diff tests; physical grayscale/edge checks still needed. |
| Previous-frame handoff in `display_session.cpp` | Capture only an actually displayed, native gray4 image. Pass it to the renderer before purge/write, then free it. Upstream's PNG-header validation and render-before-cache order are retained. | `test_display_session.py` runs production excerpts with ASan/UBSan. |
| Refresh policy in `display.cpp` | After seven regional refreshes, the next render does a full cleanup. Invalid baselines, compatibility mode, temperature profiles and driver errors retain full-refresh fallbacks. | `test_refresh_policy.py`; native planner tests. |
| Physical-screen invalidation | Menus, tutorial, messages, QA screens, wipes and touch indicators call `DisplayedImage::cover()`. Keep the filename for restoration, but never compare against it while the panel shows something else. | Retained-state behavior plus source-order checks; hardware transitions remain untested. |
| FastEPD regional/DMA fork | Stock lacks multi-region driving and our DMA clear-buffer synchronization. | Driver `tests/run_clear_dma.sh`, ASan/UBSan. |
| Logger hardening | Free serial-only allocations; pass already formatted text through `%s`. Upstream v1.8.18 still needs these fixes. | `test_app_logger.py`; adapted for upstream's relocated upload helpers. |
| Timer-wake serial skip | Avoid waiting for a debug USB console during scheduled updates. Upstream's timer-wake no-logo behavior is already retained. | Source review; wake duration still needs device measurement. |
| Local menu mode | `TRMNL_X_LOCAL_MENU_ONLY` hides the cloud-only Special submenu in our three X build environments. Navigation, tutorial, diagnostics, Wi-Fi reset and shutdown stay available. Builds without the flag retain upstream menu choices. | `test_local_menu.py`, local/cloud modes with/without tutorial. |
| LilyGo device-table guard | Our retained driver lacks the newer LilyGo display enum. Keep its row restricted to that board. | X firmware build; this does not claim support for LilyGo with the old driver. |

No dashboard or collector changes are required. No `.env` changes required.
Do not implement cloud API actions in the dashboard merely to complete a firmware merge.

## Validation commands

On 2026-10-31: all 151 native tests passed; logger and both menu modes passed;
9 display-session and 12 refresh-policy cases passed with ASan/UBSan; the retained
driver DMA regression passed with ASan/UBSan; `TRMNL_X_dev` built successfully.
The complete simulator suite and physical-panel checks have not been run.

With PlatformIO on PATH, from the firmware repository:

```sh
pio test -e native
python3 scripts/test_app_logger.py
python3 scripts/test_display_session.py
python3 scripts/test_local_menu.py
python3 scripts/test_refresh_policy.py
pio run -e TRMNL_X_dev
```

Run `bash tests/run_clear_dma.sh` separately from a checkout of the exact FastEPD
pin. It tests the driver's production mask/clear functions, not ESP32 peripherals.
Host harnesses use production excerpts with explicit I/O stubs; they are not
the complete upstream simulator suite or hardware tests.

## Before approving a flash

- Build again after the merge commit so the image reports its actual commit.
- Keep the previous approved application image and existing device backups.
- Verify a small text edit, distant simultaneous changes, unchanged frames,
  seven regional updates followed by full cleanup, and all gray levels.
- Verify menu/tutorial/error/indicator recovery with both changed and unchanged
  dashboard filenames; no stale menu pixels or scheduled logo interstitial.
- Verify scheduled Wi-Fi recovery and measure wake/battery behavior separately.
- Flash only with explicit approval and the verified device/active application
  slot. A successful host build does not authorize flashing.

## Scope limits and future updates

The new PNG preflight checks the header, not the entire compressed image.
Upstream still does not propagate every rendering/decoder failure to the cache
caller; this merge is not a general malformed-image hardening change.
The post-v1.8.18 truncated-HTTP-200 retry fix is also not part of this stable tag.

For each future stable release, compare this inventory with upstream, remove a
custom patch only after an equivalent behavior is tested, and update its tests
when code moves. Review firmware integration and driver changes separately.
Non-timing driver follow-ups and hardware/timing experiments are distinct steps;
do not silently replace the pinned driver during an otherwise routine merge.
