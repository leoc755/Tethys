@echo off
rem Builds luna.dll with MinGW (g++ on PATH). Use the 32-bit toolchain for an x86 C# project.
g++ -std=c++11 -O2 -shared -DLUNA_BUILD -o luna.dll engine.cpp util.cpp aurelius.cpp htmlparse.cpp net.cpp -lwinhttp -static -static-libgcc -static-libstdc++
if errorlevel 1 exit /b 1
g++ -std=c++11 -O2 -o luna_test.exe luna_test.cpp -L. -l:luna.dll -static-libgcc -static-libstdc++
echo done
