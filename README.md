# Super Mario World 2: Yoshi's Island - 1:1 Static Recompilation (Native C)

[![Platform: Windows / Linux / macOS](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue)](https://github.com/)
[![License: PolyForm Noncommercial](https://img.shields.io/badge/License-PolyForm%20Noncommercial%201.0.0-orange.svg)](https://polyformproject.org/licenses/noncommercial/1.0.0/)
[![Standard: C11](https://img.shields.io/badge/c-11-green.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))

基于原版 ROM (`Super Mario World 2 - Yoshi's Island (USA).sfc`) 与反汇编工程 (`brunovalads/yoshisisland-disassembly`)，通过 `snesrecomp` 静态重编译框架完全 1:1 AOT（Ahead-Of-Time）重编译为原生 C 代码的开源重编译游戏工程。

本项目**绝非模拟器前端或自写游戏重制**，而是真正的**静态二进制重编译（Static Binary Recompilation / AOT）**，将 SNES 65816 CPU 指令与 SuperFX 协处理器管线 1:1 转译为纯 C 代码，并由硬件级光栅扫描线框架驱动。

---

## 核心架构与技术实现 (Architecture)

1. **1:1 汇编到 C 静态重编译**：
   - 提取并解析官方反汇编 `yi.sym` 中共 319,760 个符号与跳转表。
   - 将 65816 CPU 代码（Bank `$00`–`$07`, `$0C`–`$13`, `$17` 等 17 个代码 Bank）通过控制流分析全部重编译为原生 C 代码（`src/game/bank*.c` 与 `src/game/dispatch_v2.c`）。
   - 将全部重编译后的游戏逻辑静态链接为核心库 `libsnesrecomp_game.a`。

2. **硬件级 LLE Runner 驱动与 SuperFX 协处理器协同**：
   - 完整集成真实 SNES 硬件运行时（光栅级 PPU、DMA/HDMA、SPC700/APU 音频合成）。
   - 内置高精度 SuperFX (GSU-2) 协处理器运行环境（Bank `$08`–`$0B`），主 CPU 与 SuperFX 严格按照真实时钟周期同步。
   - 硬件级逐行光栅扫描线帧调度（262 线/帧，在第 225 线触发 VBlank/NMI，精准处理光栅 IRQ 中断与 `$4212` `HVBJOY` 扫描状态），彻底解决 HDMA 调色板与滚动条错位。

3. **现代跨平台桌面宿主 (`desktop.c`)**：
   - 原生 SDL2 视窗渲染，精准 60.0988 FPS 锁帧与低延迟垂直同步。
   - 44,100 Hz 立体声高保真音频输出。
   - 键盘与 USB / 蓝牙即插即用手柄（Xbox / PlayStation / Switch Pro）原生映射。
   - 自动持久化电池存档管理（`save.srm`）。

---

## 编译与构建 (Building)

### 前置要求 (Prerequisites)
- **CMake** >= 3.20
- **Ninja** 或 **Make**
- **C11 兼容编译器**（GCC、Clang 或 MSVC）
- **SDL2** 开发库（Windows 预编译库已内置于 `vendor/SDL2`）

### 构建步骤 (Build Steps)

#### Windows (PowerShell / CMD)
可以直接运行根目录下的自动化构建脚本：
```bat
build.bat
```
或通过 PowerShell：
```powershell
.\build.ps1
```

#### 手动 CMake 构建 (Cross-Platform)
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target yoshis_island_recomp
```
编译完成后，生成的可执行文件为 `build/yoshis_island_recomp.exe`（或 Linux/macOS 平台下的 `build/yoshis_island_recomp`）。

---

## 运行与 ROM 说明 (How to Run)

> [!IMPORTANT]
> **版权保护与 ROM 需求说明**：
> 本代码仓库遵循法律法规，**严禁且绝不包含**任何受任天堂版权保护的商业 ROM 镜像或原始音频素材。用户必须自行提供合法获取的 NTSC-U 1.0 版原版 ROM。

1. 准备原版 ROM：
   - 目标文件：`Super Mario World 2 - Yoshi's Island (USA).sfc`
   - SHA-1 校验码：`C807F2856F44FB84326FAC5B462340DCDD0471F8`
2. 将 ROM 文件放置于可执行文件同目录下。
3. 运行游戏：
   - 双击根目录下的 `run.bat`，或在命令行中执行：
     ```bash
     ./yoshis_island_recomp "Super Mario World 2 - Yoshi's Island (USA).sfc"
     ```

---

## 默认操作指南 (Controls)

| 功能 | 键盘按键 | 对应 SNES 按键 | 手柄按键 (Xbox) | 手柄按键 (PlayStation) |
| :--- | :--- | :--- | :--- | :--- |
| **移动 / 瞄准 / 俯冲** | `W / A / S / D` 或 `方向键` | `十字键 Up / Down / Left / Right` | `十字键` / `左摇杆` | `十字键` / `左摇杆` |
| **跳跃 / 悬空踩水 (Flutter)**| `J` 或 `空格键` | `B` 键 | `A` 键 | `×` 键 |
| **伸舌吞食 / 吐出** | `K` 或 `Z` | `Y` 键 | `X` 键 | `□` 键 |
| **投掷耀西蛋 / 呼出准星** | `L` 或 `X` | `A` 键 | `B` 键 | `○` 键 |
| **锁定准星角度** | `I` 或 `C` | `X` 键 | `Y` 键 | `△` 键 |
| **左肩键** | `Q` | `L` 键 | `LB` / `LT` | `L1` / `L2` |
| **右肩键** | `E` | `R` 键 | `RB` / `RT` | `R1` / `R2` |
| **暂停 / 确认** | `Enter` (回车) | `START` | `Start / Menu` | `Options` |
| **道具选择菜单** | `Tab` | `SELECT` | `Back / View` | `Share / Touchpad` |
| **全屏切换** | `F11` | - | - | - |
| **退出** | `Esc` | - | - | - |

---

## 开源许可证与致谢 (License & Credits)

### 许可证 (License)
本项目代码与运行时遵循 **[PolyForm Noncommercial License 1.0.0](LICENSE)** 协议。
- 仅供个人研究、学术交流、教育与非商业目的免费使用与修改。
- 严禁任何形式的商业盈利行为或转售。

### 上游项目与致谢 (Acknowledgements)
- **[snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp)** by *Matthew Stanley*：提供了卓越的 SNES 静态二进制重编译底层架构与硬件级 Runner 运行时。
- **[yoshisisland-disassembly](https://github.com/brunovalads/yoshisisland-disassembly)** by *Raidenthequick, TheGreekBrit, and contributors*：详尽且高质量的《耀西岛》反汇编与符号数据库。

### 免责声明 (Disclaimer)
Super Mario World 2: Yoshi's Island, Super Mario, Yoshi, 以及 Super Nintendo Entertainment System (SNES) 是任天堂公司（Nintendo Co., Ltd.）的注册商标。本项目为非官方的逆向工程与重编译学术研究项目，与任天堂公司没有任何附属、关联或背书关系。
