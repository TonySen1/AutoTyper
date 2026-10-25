@echo off
setlocal

set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "OBJ=%ROOT%obj"
set "DIST=%ROOT%dist"
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

call "%VCVARS%" >nul
if errorlevel 1 (
  echo [ERROR] vcvars64.bat not found
  exit /b 1
)

if not exist "%DIST%" mkdir "%DIST%"
if not exist "%OBJ%" mkdir "%OBJ%"

echo [1/5] resources
rc /nologo /fo "%OBJ%\app.res" /I "%SRC%" "%SRC%\app.rc"
if errorlevel 1 exit /b 1

echo [2/5] compile
cl /nologo /c /utf-8 /EHsc /O2 /GS /guard:cf /W3 /DNDEBUG /DUNICODE /D_UNICODE /Fo"%OBJ%\main.obj" "%SRC%\main.cpp"
if errorlevel 1 exit /b 1

echo [3/5] link
link /nologo /OUT:"%DIST%\AutoTyper.exe" /SUBSYSTEM:WINDOWS /MACHINE:X64 /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /MANIFEST:NO "%OBJ%\main.obj" "%OBJ%\app.res" user32.lib gdi32.lib msimg32.lib comdlg32.lib comctl32.lib shell32.lib advapi32.lib
if errorlevel 1 exit /b 1

echo [4/5] test host
cl /nologo /c /utf-8 /EHsc /O2 /DUNICODE /D_UNICODE /Fo"%OBJ%\testhost.obj" "%ROOT%test\testhost.cpp"
if errorlevel 1 exit /b 1
link /nologo /OUT:"%OBJ%\testhost.exe" /SUBSYSTEM:WINDOWS /MACHINE:X64 /MANIFEST:NO "%OBJ%\testhost.obj" user32.lib gdi32.lib
if errorlevel 1 exit /b 1

echo [5/5] done
for %%F in ("%DIST%\AutoTyper.exe") do echo   output: %%F  %%~zF bytes
exit /b 0
