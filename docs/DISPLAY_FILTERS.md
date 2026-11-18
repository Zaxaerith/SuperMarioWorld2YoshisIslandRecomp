# Display filters

Build normally and enable Display effects below Player 1 in the launcher. Sharp is selected when enabled. The list and cycle order are Original, Sharp, Scanlines, CRT and LCD grid. Click the hotkey box to change the cycle key (F7 by default). Settings are saved in config.ini. Sharp preserves source colors and uses 3x prescaling; Linear Filtering controls the final resize.

Build `video_effects_contract` and run `ctest --test-dir build -R video_effects_contract --output-on-failure`. Run `YoshisIslandRecomp.exe --effects-selftest` from a writable directory to check hotkey rebinding, persistence and cycling. Both tests run without a ROM.
