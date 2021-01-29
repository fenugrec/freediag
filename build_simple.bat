@echo off
REM
REM Make sure cmake.exe and MinGW are in the PATH
REM
IF [%OS%]==[Windows_NT] SETLOCAL
IF NOT EXIST build (
   mkdir build
)

cd build || GOTO :ERROR
cmake -G"MinGW Makefiles" ^
	  -DBUILD_DIAGTEST=ON ^
	  -DCMAKE_VERBOSE_MAKEFILE=0 ^
	  -S .. || GOTO :ERROR
cmd /k cmd /c mingw32-make

:ERROR
cmd /k 