# Restart game

The confirmed action resets the emulated machine in the current process and reloads the last persisted SRAM. The window, renderer, controller, audio device and launcher stay open. It makes no extra save write. A failed save read leaves the paused game resumable.

CPU, interpreter cache, frame scheduler, PPU, SuperFX and audio delivery reset together. SRAM is read before changing the machine. The original boot runs normally. The player presses Start to skip the intro and enter file selection, just as on a normal launch; reset does not generate Start input. PAL language selection remains interactive when the original game requires it.

The implementation is portable and contains no child-process launch. Synthetic tests cover all four ROM profiles, repeated reset, SRAM preservation, failed reads and absence of automatic title input. Player testing confirmed an in-level reset in the same window and the expected saved slots after manually advancing the intro.
