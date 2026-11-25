@echo off
setlocal enabledelayedexpansion
rem Builds luna.dll with MinGW (gcc and g++ on PATH). Use the 32-bit toolchain for an x86 C# project.
if not exist obj mkdir obj

rem 1. C files: luna_webp.c and the bundled libwebp decoder
set CFILES=luna_webp.c
for /r third_party\libwebp\src %%f in (*.c) do set CFILES=!CFILES! "%%f"
gcc -std=gnu99 -O2 -DLUNA_BUILD -I. -Ithird_party\libwebp -c !CFILES!
if errorlevel 1 exit /b 1
move /y *.o obj >nul

rem 2. C++ files and link
g++ -std=c++11 -O2 -shared -DLUNA_BUILD -o luna.dll engine.cpp pages.cpp util.cpp aurelius.cpp htmlparse.cpp net.cpp obj\*.o -lwinhttp -static -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1

echo done
