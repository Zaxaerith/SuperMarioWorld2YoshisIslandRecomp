# SuperMarioWorld2YoshisIslandRecomp

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](#building-from-source)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue)](#prerequisites)
[![Language](https://img.shields.io/badge/language-C11-orange)](#what-static-recompilation-means-here)
[![License](https://img.shields.io/badge/license-PolyForm--Noncommercial--1.0.0-blue)](LICENSE)

Native C static recompilation of **Super Mario World 2: Yoshi's Island (SNES, USA)** into a standalone, native PC executable using the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework.

---

## What "static recompilation" means here

The 65816 CPU assembly code from the original ROM is statically translated to pure native C — every function the game executes on the main SNES CPU is compiled as real C translation units located in `src/game/`.

**The rest of the SNES is not recompiled — it is hardware**:
* **PPU (Picture Processing Unit)**, Mode 7 matrix transforms, HDMA raster effects, and sprite rendering run through an optimized C hardware implementation in `snesrecomp/runner/src/snes/`.
* **APU (Audio Processing Unit)** and the SPC700 audio coprocessor run asynchronously with native sample resampling and anti-starvation buffers.
* **SuperFX (GSU-2) Coprocessor** executes 3D polygon projection, dynamic sprite scaling, and island rotation with cycle-accurate synchronization against the main 65816 CPU.

This follows the established architecture of modern static recompilation projects: **recompile the CPU, emulate the silicon**.

---

## Current Status & Features

- [x] **Fully Playable**: Tested and verified end-to-end from intro sequence, Mode 7 3D title screen, save file selection, forest cutscenes, through in-game stage gameplay.
- [x] **Native 60 FPS**: Exact 60.0988 Hz hardware pacing using sub-millisecond precision accumulators.
- [x] **SuperFX (GSU-2) 3D & Morphmation**: Hardware-synchronized coprocessor execution powering 3D Mode 7 island rotation, boss transformations, and dynamic sprite scaling.
- [x] **Cycle-Accurate HDMA & Scanline Raster Pipeline**: Precise mid-scanline beam latching eliminating phase shift artifacts, comb-teeth tearing, and palette flickering.
- [x] **Controller Support**: Full plug-and-play support for Xbox, PlayStation, Switch Pro, and standard USB/Bluetooth gamepads via SDL2 GameController.
- [x] **Ultra-Low Latency Audio**: Dedicated background audio pull callback at 44.1 kHz with APU mutex synchronization.
- [x] **Persistent Battery Saves**: Automatic SRAM battery backup saving to `save.srm`.

---

## Quick Start (ROM Requirement)

> [!IMPORTANT]
> **Legal Notice**: This repository does **NOT** contain any copyrighted ROM data, game assets, or proprietary Nintendo code. You must provide your own legally obtained ROM dump to run the game.

### Verified ROM Information
* **Game**: Super Mario World 2 - Yoshi's Island (USA) (V1.0)
* **File Name**: `Super Mario World 2 - Yoshi's Island (USA).sfc`
* **File Size**: `2,097,152` bytes (Headerless `.sfc`)
* **CRC32**: `D138F224`
* **MD5**: `CB472164C5A71CCD3739963390EC6A50`
* **SHA1**: `C807F2856F44FB84326FAC5B462340DCDD0471F8`
* **SHA256**: `9B4957466798BBDB5B43A450BBB60B2591AE81D95B891430F62D53CA62E8BC7B`

Simply place your ROM file in the same directory as the executable (or in the project root) named `Super Mario World 2 - Yoshi's Island (USA).sfc`.

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
| **Item Bag Menu** | `Tab` | `Back / View` | `Share / Touchpad` | `SELECT` |
| **Toggle Fullscreen** | `F11` | - | - | - |
| **Quit Game** | `Esc` | - | - | - |

> [!TIP]
> **Flutter Jump**: Press and hold `J` or `Space` while in mid-air to perform Yoshi's signature flutter jump and gain extra height and airtime!

---

## Building from Source

### Prerequisites
* **CMake** (>= 3.20)
* **C Compiler**: GCC (MinGW-w64 on Windows), Clang, or MSVC (C11 support required)
* **Build System**: Ninja (recommended) or Make
* **SDL2**: Development library (`SDL2-devel`) — prebuilt Windows headers & import libraries are included in `vendor/SDL2` for zero-setup compilation.

### Windows (Ninja + MinGW / Clang / MSVC)

```powershell
# Clone the repository
git clone https://github.com/Zaxaerith/SuperMarioWorld2YoshisIslandRecomp.git
cd SuperMarioWorld2YoshisIslandRecomp

# Configure and build using Ninja
mkdir build
cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja yoshis_island_recomp

# Run the game (ensure Super Mario World 2 - Yoshi's Island (USA).sfc is placed in the project root)
cd ..
./run.bat
```

Or simply run the automated one-click build script:
```bat
build.bat
```

### Linux (Ubuntu / Debian / Fedora / Arch)

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt update && sudo apt install -y build-essential cmake ninja-build libsdl2-dev

# Build
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja yoshis_island_recomp

# Run
./yoshis_island_recomp "Super Mario World 2 - Yoshi's Island (USA).sfc"
```

---

## Repository Structure

```
SuperMarioWorld2YoshisIslandRecomp/
├── CMakeLists.txt              # Unified cross-platform CMake build configuration
├── README.md                   # Project documentation & guide
├── LICENSE                     # PolyForm Noncommercial License 1.0.0
├── build.bat / build.ps1       # Portable one-click build helpers
├── run.bat                     # Quick launcher script
├── config/                     # Recompiler function symbols & configuration
│   └── functions.toml          # Static function mapping & metadata
├── docs/                       # Technical notes & development documentation
├── scripts/                    # Label & symbol parsing utilities
├── src/
│   ├── desktop.c               # Interactive SDL2 desktop runner (window, audio, input)
│   ├── game_rtl.c / game_rtl.h # Hardware scanline synchronization & PPU timeline bridge
│   ├── headless.c              # Diagnostic headless test & benchmark runner
│   └── game/                   # Recompiled native C source code (17 code banks)
│       ├── bank00_v2.c         # System core, reset vectors & main game state
│       ├── bank01_v2.c         # Player physics, Yoshi mechanics & entity handlers
│       ├── ...                 # Additional recompiled game banks
│       ├── bank17_v2.c         # Title screen, menu mode & transition routines
│       └── dispatch_v2.c       # Global function jump & LLE fallback dispatch table
├── snesrecomp/                 # SNESRecomp core LLE execution & silicon emulation framework
│   └── runner/                 # PPU, SPC700, SuperFX coprocessor & bus controllers
└── vendor/SDL2/                # Portable prebuilt SDL2 development files for Windows
```

---

## License

This project is a recompiled derivative work based on [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) and is licensed under the **PolyForm Noncommercial License 1.0.0**.

* **Source-Available / Noncommercial**: Any noncommercial purpose (personal use, personal study, research, private entertainment, non-profit community testing) is permitted.
* **Commercial Use Prohibited**: Commercial use, monetized distribution, or deriving profit from this software is strictly prohibited under the upstream license terms.
* **Upstream Copyright**: `Copyright (c) 2026 Matthew Stanley`.
* For the full legal text, see the [LICENSE](LICENSE) file.

---

## Credits & Acknowledgments

* **[Zaxaerith](https://github.com/Zaxaerith)**: Project porting, SuperFX pipeline synchronization, HDMA scanline timing alignment, and host runtime.
* **Nintendo**: Original creators of *Super Mario World 2: Yoshi's Island* (1995).
* **[Matthew Stanley](https://github.com/mstan)**: Author of the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp) framework and pioneer of SNES static recompilation.
* **[Raidenthequick, TheGreekBrit, and brunovalads](https://github.com/brunovalads/yoshisisland-disassembly)**: Authors and contributors of the comprehensive Yoshi's Island disassembly.
* **LakeSnes & snes9x**: Foundation for embedded SNES silicon emulation.

---

## 简体中文说明 (Simplified Chinese Guide)

基于原版 ROM (`Super Mario World 2 - Yoshi's Island (USA).sfc`)，通过 `snesrecomp` 静态重编译框架实现的 1:1 原生 C 语言重编译游戏。

### 核心特性
- **纯原生 C 代码执行**：65816 CPU 汇编逻辑全量静态转译为 C 代码，杜绝模拟器解释开销。
- **SuperFX (GSU-2) 协处理器同步**：硬件时钟级驱动 Mode 7 3D 旋转岛屿与同屏角色缩放变形。
- **扫描线光栅时序对齐**：彻底修复 HDMA 调色板与滚动条锁存错位，无频闪，画质完全还原原装硬件。
- **即插即用控制器支持**：完美支持键盘与各类主流游戏手柄（Xbox / PlayStation / Switch Pro）。
- **ROM 获取提示**：本项目遵循开源合规标准，不包含任何任天堂受版权保护的原始素材。玩家须自行准备合法的美版原版 ROM 并放置于运行目录下即可畅玩。
