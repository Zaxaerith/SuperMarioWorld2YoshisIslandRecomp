# Game-save write feedback

**SAVED** appears for 1.4 seconds after a successful file write containing changed, checksummed slot data. **SAVE ERROR** appears for five seconds only when opening, writing or closing the file fails. An uninitialized/empty slot or temporarily invalid game checksum is not a file-write error. Valid slots are compared independently, using the native backup when necessary; restoring an unchanged primary does not produce another notification.

The original game still decides when progress enters its cartridge save. The existing host write schedule remains every 300 game frames and normal exit. This adds no manual save, checkpoint persistence, save-state or new file format. The indicator is drawn after the write has completed, not while it is in progress. F9 presentation captures include the actual visible badge.

The ROM-free contract covers changed/unchanged data, initial zero/erased SRAM, mixed valid/uninitialized slots, backup fallback, transient invalid checksums, actual write failure/recovery and unchanged source/renderer state.

Real-game acceptance: start a normal empty slot; complete a stage and return to the world map; observe SAVED; quit and restart; verify that the stage progress loads. This test is still pending for the revised implementation.

Review images and gameplay results belong in the [PR #12 discussion](https://github.com/Zaxaerith/SuperMarioWorld2YoshisIslandRecomp/pull/12), not in the source tree. Historical rendered previews are not evidence of a real save event.
