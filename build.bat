@echo off
setlocal
echo =================================================================
echo   Building Yoshi's Island (SNESRecomp Native AOT)
echo =================================================================
rem Check optional local toolchain paths
if exist "C:\MYAPPLY\mingw64\bin" set PATH=C:\MYAPPLY\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\MYAPPLY\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\MYAPPLY\mingw64\bin;%PATH%

if not exist build mkdir build
cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b %ERRORLEVEL%
)

ninja yoshis_island_recomp
if %ERRORLEVEL% neq 0 (
    echo Compilation failed!
    pause
    exit /b %ERRORLEVEL%
)

copy /y yoshis_island_recomp.exe ..\ >nul

echo.
echo =================================================================
echo   Build Successful! 
echo   Executable: yoshis_island_recomp.exe
echo =================================================================
cd ..

