@echo off
cd /d %~dp0\sys\winnt\
call nhsetup
set PATH=%PATH%;c:\msys64\mingw64\bin
cd /d d:\Builds\vulture\src
echo.
echo.
echo Prerequisites:
echo   - MSYS2 installed to c:\msys64 (http://msys2.github.io) - tested with msys2-x86_64-20160921
echo   - And the following packages:
echo       * mingw-w64-x86_64-toolchain
echo       * mingw-w64-x86_64-libpng
echo       * mingw-w64-x86_64-SDL_ttf
echo       * mingw-w64-x86_64-SDL_mixer
echo       * mingw-w64-x86_64-libtheo
echo       * mingw-w64-x86_64-smpeg
echo.
echo.
echo.
echo Enter "mingw32-make -f Makefile.gcc install" to build Vulture.
echo.
echo.
cmd