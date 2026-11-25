# Return to title

The confirmed action resets the emulated machine in the current process and reloads the last persisted SRAM. The window, renderer, controller, audio device and launcher stay open. It makes no extra save write. A failed save read leaves the paused game resumable.

CPU, interpreter cache, frame scheduler, PPU, SuperFX and audio delivery reset together. SRAM is read before changing the machine. The original boot runs normally; generated Start input skips its opening cinematic and enters file selection, stopping before any slot is selected. PAL language selection remains interactive when the original game requires it.

The implementation is portable and contains no child-process launch. Synthetic tests cover all four ROM profiles, repeated reset, SRAM preservation, failed reads and input release at file selection. Actual boot and save reload still require gameplay validation.
