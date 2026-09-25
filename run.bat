@echo off
setlocal
title Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT)
echo =================================================================
echo   Super Mario World 2: Yoshi's Island - 1:1 Static Recompilation
echo =================================================================
echo Controls:
echo   W / A / S / D or Arrows : D-Pad (Walk, Aim Up, Crouch)
echo   J or Space              : Jump / Flutter Jump (SNES B)
echo   K or Z                  : Eat / Tongue / Spit (SNES Y)
echo   L or X                  : Throw Egg / Aim (SNES A)
echo   I or C                  : Lock Aim Cursor (SNES X)
echo   Q / E                   : L / R Shoulder
echo   Enter                   : START (Pause / Confirm)
echo   Tab                     : SELECT (Item Menu)
echo   F11                     : Toggle Fullscreen
echo   Esc                     : Quit
echo Gamepads:
echo   Direct plug-and-play support for Xbox / PlayStation controllers!
echo =================================================================
echo.

if not exist "yoshis_island_recomp.exe" (
    echo Error: yoshis_island_recomp.exe not found! Running build.bat...
    call build.bat
)

start "" yoshis_island_recomp.exe "Super Mario World 2 - Yoshi's Island (USA).sfc"
