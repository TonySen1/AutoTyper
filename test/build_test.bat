@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >/dev/null
set "T=%~dp0"
cl /nologo /c /utf-8 /EHsc /O2 /DUNICODE /D_UNICODE /Fo"%T%..\obj\testhost.obj" "%T%testhost.cpp"
if errorlevel 1 exit /b 1
link /nologo /OUT:"%T%..\obj\testhost.exe" /SUBSYSTEM:WINDOWS /MACHINE:X64 /MANIFEST:NO ^
   "%T%..\obj\testhost.obj" user32.lib gdi32.lib
if errorlevel 1 exit /b 1
echo BUILD_OK
