@echo off
rem Builds and runs mathbench with MSVC (without CORE-MATH, which needs GCC or Clang). Run it from an x64 or x86
rem Native Tools command prompt. Usage: build.bat <tag> [cl flags], for example build.bat msvc-x64-md /MD
rem Writes the objects, the program and its output (<tag>.txt) to build-netcodec\mathbench\<tag>\.
setlocal
set TAG=%~1
set EXTRA=%~2 %~3 %~4
set HERE=%~dp0
set M=%HERE%..\..\..\..\build-netcodec\mathlibs
set OUT=%HERE%..\..\..\..\build-netcodec\mathbench\%TAG%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /fp:precise %EXTRA% /std:c++17 /EHs-c- /GR- /I "%M%\llvm-project\libc" /c "%HERE%lm.cpp" /Fo"%OUT%\lm.obj" || exit /b 1
cl /nologo /O2 /fp:precise %EXTRA% /std:c++17 /EHs-c- /GR- /I "%HERE%..\..\..\..\lib\llvm-libc" /c "%HERE%..\..\..\..\shared\qcommon\q_libm.cpp" /Fo"%OUT%\q_libm.obj" || exit /b 1
cl /nologo /O2 /fp:precise %EXTRA% "%HERE%mathbench.c" "%OUT%\lm.obj" "%OUT%\q_libm.obj" /Fo"%OUT%\mathbench.obj" /Fe"%OUT%\mathbench.exe" || exit /b 1
"%OUT%\mathbench.exe" > "%OUT%\%TAG%.txt"
type "%OUT%\%TAG%.txt"
