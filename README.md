# SuperMarioWorld2YoshisIslandRecomp

<a href="https://recomplauncher.com/game/supermarioworld2yoshisislandrecomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#building-from-source)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)](#prerequisites)
[![Language](https://img.shields.io/badge/language-C11%20%2F%20C%2B%2B17-orange)](#what-static-recompilation-means-here)
[![License](https://img.shields.io/badge/license-PolyForm--Noncommercial--1.0.0-blue)](LICENSE)

Native C static recompilation of **Super Mario World 2: Yoshi's Island (SNES, USA, Rev 1.0)** into a standalone native PC executable with native [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) graphical launcher using the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework.

[简体中文说明 (Simplified Chinese Guide)](README.zh-CN.md)

---

## What "static recompilation" means here

The 65816 CPU assembly code from the original ROM is statically translated to pure native C — every function the game executes on the main SNES CPU is compiled as real C translation units located in `src/game/`.

**The rest of the SNES is not recompiled — it is hardware**:
* **SuperFX (GSU-2) Coprocessor**: Yoshi's Island's flagship coprocessor executing 3D Mode 7 island rotation, boss transformations, and dynamic sprite scaling with cycle-accurate synchronization against the main 65816 CPU.
* **PPU (Picture Processing Unit)**, Mode 7 matrix transforms, HDMA raster effects, and sprite rendering run through an optimized C hardware implementation in `snesrecomp/runner/src/snes/`.
* **APU (Audio Processing Unit)** and the SPC700 audio coprocessor run asynchronously with native sample resampling and anti-starvation buffers.

This follows the established architecture of modern static recompilation projects: **recompile the CPU, emulate the silicon**.

---

## Current Status & Features

- [x] **Native recomp-ui GUI Launcher**: Integrated Game, Controls, Video, and Audio tabs with persistent configuration.
- [x] **Fully Playable**: Tested and verified end-to-end from intro sequence, Mode 7 3D title screen, save file selection, forest cutscenes, through in-game stage gameplay.
- [x] **Native 60 FPS**: Exact 60.0988 Hz hardware pacing using sub-millisecond precision accumulators.
- [x] **SuperFX (GSU-2) 3D & Morphmation**: Hardware-synchronized coprocessor execution powering 3D Mode 7 island rotation, boss transformations, and dynamic sprite scaling.
- [x] **Cycle-Accurate HDMA & Scanline Raster Pipeline**: Precise mid-scanline beam latching eliminating phase shift artifacts, comb-teeth tearing, and palette flickering.
- [x] **Single-Player Controller Mapping**: Full plug-and-play support for Xbox, PlayStation, Switch Pro, and standard USB/Bluetooth gamepads with deadzone tuning and `keybinds.ini` persistence.
- [x] **Strict ROM Verification**: SHA-256 and CRC32 verification, supporting both Headerless (`.sfc`, 2,097,152 bytes) and Copier-Headered (`.smc`, 2,097,664 bytes) formats.
- [x] **Portable SRAM Saves**: Battery backup saves automatically located in `saves/save.srm` with seamless backward compatibility.
- [x] **Self-Contained Executable**: Statically linked runtime libraries requiring only `SDL2.dll` alongside `YoshisIslandRecomp.exe`.

---

## Quick Start (ROM Requirement)

> [!IMPORTANT]
> **Legal Notice**: This repository does **NOT** contain any copyrighted ROM data, game assets, or proprietary Nintendo code. You must provide your own legally obtained ROM dump to run the game.

### Verified ROM Information
* **Game**: Super Mario World 2: Yoshi's Island (USA) (Rev 1.0)
* **File Format**:
  * **Headerless (`.sfc`)**: `2,097,152` bytes
  * **Copier-Headered (`.smc`)**: `2,097,664` bytes (512-byte SMC copier header stripped in memory)
* **SHA-256**: `9b4957466798bbdb5b43a450bbb60b2591ae81d95b891430f62d53ca62e8bc7b`
* **CRC32**: `D138F224`
* **SHA-1**: `c807f2856f44fb84326fac5b462340dcdd0471f8`

Launch `YoshisIslandRecomp.exe`, click **Browse** on the Game tab to select your ROM, and click **Play**.

---

## Native Launcher Guide

Running `YoshisIslandRecomp.exe` opens the graphical launcher:

* **Game Tab**: ROM status inspection, file browser, launch button, and "Skip Launcher" option.
* **Controls Tab**: Input source selection (Keyboard, Gamepad, Both), left analog stick deadzone setting, and key remapping.
* **Video Tab**:
  * **Aspect Ratio**: `4:3 (CRT)` (default), `8:7 (Square pixels)`, `1:1 (Square frame)`.
  * **Window Scale**: 1x through 6x integer window scaling.
  * **Fullscreen**: Borderless desktop fullscreen toggle.
  * **Linear Filtering**: Smooth interpolation vs crisp sharp pixels.
* **Audio Tab**: Master volume slider (0% to 100%) and mute toggle.
* **Command Line**:
  * `--launcher`: Force opening the launcher UI even if Skip Launcher is set.
  * `--rom <path>`: Direct path to ROM file.
  * `-h, --help`: Display command line usage.

---

## Controls

### Keyboard & Gamepad Mapping

| Action | Keyboard | Xbox Gamepad | PS Gamepad | SNES Original |
| :--- | :--- | :--- | :--- | :--- |
| **Walk / Aim Up / Crouch** | `W / A / S / D` or `Arrow Keys` | `D-Pad` / `Left Stick` | `D-Pad` / `Left Stick` | `D-Pad` |
| **Jump / Flutter Jump** | `J` or `Space` | `A` Button | `Cross (X)` | `B` Button |
| **Tongue / Eat / Spit** | `K` or `Z` | `X` Button | `Square` | `Y` Button |
| **Throw Egg / Aim Reticle**| `L` or `X` | `B` Button | `Circle (O)` | `A` Button |
| **Lock Aim Reticle Angle** | `I` or `C` | `Y` Button | `Triangle` | `X` Button |
| **Shoulder Look Left** | `Q` | `LB` / `LT` | `L1` / `L2` | `L` Button |
| **Shoulder Look Right** | `E` | `RB` / `RT` | `R1` / `R2` | `R` Button |
| **Confirm / Pause** | `Enter` | `Start / Menu` | `Options` | `START` |
| **Item Bag Menu** | `Tab` or `RShift` | `Back / View` | `Share / Touchpad` | `SELECT` |
| **Toggle Fullscreen** | `F11` or `Alt+Enter` | - | - | - |
| **Quit Game** | `Esc` | - | - | - |

> [!TIP]
> **Flutter Jump**: Press and hold `J` or `Space` while in mid-air to perform Yoshi's flutter jump to gain extra height and airtime!

---

## SRAM Save Compatibility

Battery-backed cartridge saves (64 KB / 65,536 bytes) are managed under `saves/save.srm`.
* When launched, the host checks `saves/save.srm`, falls back to legacy `save.srm`, and safely migrates existing saves without destructive overwrites.
* The game auto-saves every 300 frames (~5 seconds) and flushes state upon clean exit.

---

## Building from Source

### Prerequisites
* **CMake** (>= 3.20)
* **C/C++ Compiler**: MinGW-w64 GCC (C11 and C++17 support), Clang, or MSVC
* **Build System**: Ninja (recommended) or Make
* **SDL2**: Development headers & libraries are included under `vendor/SDL2`.

### Windows (Ninja + MinGW GCC)

```powershell
# Clone the repository with submodules
git clone https://github.com/Zaxaerith/SuperMarioWorld2YoshisIslandRecomp.git
cd SuperMarioWorld2YoshisIslandRecomp
git submodule update --init --recursive

# Configure and build using Ninja (Jobs = 2)
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja -C build -j 2 yoshis_island_recomp

# Run
.\build\YoshisIslandRecomp.exe
```

Or run the automated build script:
```powershell
.\build.bat
```

---

## Automated Tests

```powershell
# Run launcher contract tests
ninja -C build -j 2 launcher_contract
.\build\launcher_contract.exe

# Run 300-frame headless diagnostic verification
ninja -C build -j 2 yi_headless
.\build\yi_headless.exe "Super Mario World 2 - Yoshi's Island (USA).sfc" 300
```

---

## License

This project is a recompiled derivative work based on [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) and [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui), licensed under the **PolyForm Noncommercial License 1.0.0**.

* **Noncommercial Use**: Permitted for personal study, research, and non-profit testing.
* **Commercial Use Prohibited**: Commercial distribution or monetized redistribution is strictly prohibited.
* **Upstream Copyright**: `Copyright (c) 2026 Matthew Stanley`.
* See [LICENSE](LICENSE) for full details.

---

## Credits & Acknowledgments

* **[Zaxaerith](https://github.com/Zaxaerith)**: Project porting, SuperFX pipeline synchronization, launcher integration, and desktop host.
* **Nintendo**: Original creators of *Super Mario World 2: Yoshi's Island* (1995).
* **[Matthew Stanley (mstan)](https://github.com/mstan)**: Author of the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) and [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) frameworks.
* **[Raidenthequick, TheGreekBrit, and brunovalads](https://github.com/brunovalads/yoshisisland-disassembly)**: Yoshi's Island disassembly contributors.
