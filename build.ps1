param(
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "=== Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT) Builder ===" -ForegroundColor Cyan

if (Test-Path "C:\MYAPPLY\mingw64\bin") {
    $env:PATH = "C:\MYAPPLY\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\MYAPPLY\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\MYAPPLY\mingw64\bin;$env:PATH"
}

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location "build"
try {
    cmake .. -G "Ninja" "-DCMAKE_BUILD_TYPE=$BuildType"
    ninja yoshis_island_recomp
    Write-Host "Build successful! Executable: build/yoshis_island_recomp.exe" -ForegroundColor Green
}
finally {
    Pop-Location
}
