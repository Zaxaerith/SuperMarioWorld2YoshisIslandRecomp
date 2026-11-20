# SRAM write indicator

A small **SAVING...** badge appears for 1.4 seconds after a successful file write containing changed permanent game-save data. Unchanged periodic writes and volatile GSU graphics/work RAM do not trigger it. **SAVE ERROR** appears for five seconds if opening, writing or closing the save file fails; the successful baseline is not advanced on failure.

The original game controls what becomes persistent SRAM: level results/progression and overworld/bonus updates are examples. This is not a save state and does not make a mid-level checkpoint survive restarting the program.

The existing desktop schedule remains unchanged: write SRAM every 300 game frames (about five seconds NTSC or six seconds PAL at normal speed) and on a normal exit. The badge reports completed writes; it may briefly remain visible after the short synchronous write has finished. For a gameplay check, complete a new stage or improve its score, return to the map and wait for the next periodic write.

The permanent payload is SRAM0x7C00..0x7E7B: three104-byte files, backups and checksums. Source memory is read only. The SDL overlay restores renderer state and has no pause-menu, filter, RA or widescreen dependency.

`save_indicator_contract` checks unchanged/volatile-only writes, permanent changes, failures, retries, expiry, source preservation and rendering state at native/wide logical sizes using a synthetic surface. No game ROM or account is required.
