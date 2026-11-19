# Display filters

Build normally and enable Display effects below Player 1 in the launcher. Sharp is selected when enabled. The list and cycle order are Original, Sharp, Scanlines, CRT and LCD grid. Click the hotkey box to change the cycle key (F7 by default). Settings are saved in config.ini. Sharp preserves source colors and uses 3x prescaling; Linear Filtering controls the final resize.

Build `video_effects_contract` and run `ctest --test-dir build -R video_effects_contract --output-on-failure`. Run `YoshisIslandRecomp.exe --effects-selftest` from a writable directory to check hotkey rebinding, persistence and cycling. Both tests run without a ROM.

## Local review captures and performance

F9 saves Original plus the currently selected processed mode from the **same emulated frame**,
and the current final renderer image as `effects-<id>-*.bmp`. The raw Original is
256x224 (342x224 with optional widescreen); the selected filtered image is exactly 3x that
size. Use the final `present.bmp` to assess display aspect and final SDL scaling.
Capture happens before the RA overlay, so account text is not added to the image.
All files stay local; no capture is automatically uploaded.

Every five stable seconds, `[EffectsPerf]` records mode, source size, requested
VSync, actual frame rate, filter-only milliseconds per frame and process CPU
usage. `cpu_one_core_pct=100` means one full CPU core, not 100% of the whole PC;
it includes all game threads. Changing mode or native/wide dimensions starts a
new sample. Captures cause extra work: wait for a later sample before comparing.
FPS near the region cap alone does not prove that a filter has no overhead.

For a repeatable real-game check, keep the same room, position, window size,
VSync and region. Record 30 seconds each of Original, Sharp, Scanlines, CRT and
LCD grid, then compare CPU and filter_ms as well as FPS. Test VSync Off and On
separately. Rebind F7 to F2 in the launcher, check cycling in-game, exit normally
and confirm the binding/mode survive restart. Restore your preferred key afterward.

`effects_review_tool <existing.bmp>` applies the exact filter implementation to
an existing local screenshot and prints an offline 120-frame pixel benchmark.
`--synthetic` checks native/wide capture sizes and source preservation without
a game/window. Neither result is an in-game FPS/CPU measurement. Existing captures
can illustrate the filter modes, but do not replace the runtime check above.

## Shared integration

The panel helper accumulates all registered callbacks, removes duplicate
registrations and gives each target its own generated source. RA, filters and
widescreen must use the same helper version. VSync remains the saved main-branch
setting; no filter code forces it Off or changes PAL/NTSC pacing.

The original standalone filter presentation assumes a 256x224 source. A combined
build must instead choose native/wide pixels first, then filter that selected
width, preserve the aspect-correct destination rectangle, and draw the RA overlay
last. A clean textual merge alone does not establish that ordering. Test both
dimensions and fallback transitions; do not filter a stale 256-wide buffer while
widescreen is active.
