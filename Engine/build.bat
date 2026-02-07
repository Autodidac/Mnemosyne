@echo off
setlocal

echo === Building mylib.lib (contains main) + app.exe (no main in app) ===
echo.

REM ------------------------------------------------------------
REM Ensure MSVC environment WITHOUT vswhere
REM ------------------------------------------------------------
where cl >nul 2>&1
if not errorlevel 1 goto :env_ok

echo cl.exe not found in PATH. Probing common Visual Studio locations...

for %%P in (
  "%ProgramFiles%\Microsoft Visual Studio\2026\BuildTools"
  "%ProgramFiles%\Microsoft Visual Studio\2026\Community"
  "%ProgramFiles%\Microsoft Visual Studio\2026\Professional"
  "%ProgramFiles%\Microsoft Visual Studio\2026\Enterprise"
  "%ProgramFiles%\Microsoft Visual Studio\2026\Preview"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\BuildTools"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Community"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Professional"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Enterprise"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Preview"

  "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Preview"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
  "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Preview"
) do (
  if exist "%%~P\Common7\Tools\VsDevCmd.bat" (
    call "%%~P\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
    goto :env_check
  )
  if exist "%%~P\VC\Auxiliary\Build\vcvars64.bat" (
    call "%%~P\VC\Auxiliary\Build\vcvars64.bat"
    goto :env_check
  )
)

echo ERROR: Could not locate Visual Studio / Build Tools with MSVC.
echo Install "Build Tools for Visual Studio" with MSVC v143 (x64/x86) and a Windows SDK.
pause
exit /b 1

:env_check
where cl >nul 2>&1
if errorlevel 1 (
  echo ERROR: Environment init ran, but cl.exe still not found.
  pause
  exit /b 1
)

:env_ok
where lib >nul 2>&1 || (echo lib.exe not found & pause & exit /b 1)
where link >nul 2>&1 || (echo link.exe not found & pause & exit /b 1)

REM ------------------------------------------------------------
REM Layout
REM ------------------------------------------------------------
set "MODDIR=modules"
set "SRCDIR=src"
set "INCDIR=include"
set "IFCDIR=ifc"
set "OBJDIR=obj"

if not exist "%IFCDIR%" mkdir "%IFCDIR%" >nul 2>&1
if not exist "%OBJDIR%" mkdir "%OBJDIR%" >nul 2>&1

REM ------------------------------------------------------------
REM Build inputs
REM ------------------------------------------------------------
set "CXX=/nologo /std:c++latest /EHsc /W4 /WX /c"
set "CXX_IFC=%CXX% /ifcSearchDir "%IFCDIR%""
set "CXX_INC=%CXX% /I"%INCDIR%""

REM Modules (interface name == module name)
set "M0=mylib"
set "M1=core.log"
set "M2=core.time"
set "M3=core.error"
set "M4=core.format"
set "M5=core.string"
set "M6=core.id"
set "M7=core.math"
set "M8=core.env"
set "M9=core.path"
set "M10=core.assert"
set "M11=runtime"

set "LIBFILE=mylib.lib"
set "EXE=app.exe"
set "TESTEXE=tests.exe"

REM ------------------------------------------------------------
REM Clean previous deterministic outputs (keep app.exe)
REM ------------------------------------------------------------
del /q "%IFCDIR%\*.ifc" "%OBJDIR%\*.obj" "%LIBFILE%" "%TESTEXE%" 2>nul

REM ------------------------------------------------------------
REM Compile module interfaces (produce .ifc + obj into folders)
REM ------------------------------------------------------------
echo [1/3] compile module interfaces
for %%M in (%M0% %M1% %M2% %M3% %M4% %M5% %M6% %M7% %M8% %M9% %M10% %M11%) do (
  echo   interface %%M
  cl %CXX_IFC% /interface /ifcOutput "%IFCDIR%\%%M.ifc" /Fo"%OBJDIR%\%%M_ifc.obj" "%MODDIR%\%%M.ixx" || exit /b 1
)

REM ------------------------------------------------------------
REM References used by any TU that imports modules
REM ------------------------------------------------------------
set "REFS=/reference mylib="%IFCDIR%\mylib.ifc""
set "REFS=%REFS% /reference core.log="%IFCDIR%\core.log.ifc""
set "REFS=%REFS% /reference core.time="%IFCDIR%\core.time.ifc""
set "REFS=%REFS% /reference core.error="%IFCDIR%\core.error.ifc""
set "REFS=%REFS% /reference core.format="%IFCDIR%\core.format.ifc""
set "REFS=%REFS% /reference core.string="%IFCDIR%\core.string.ifc""
set "REFS=%REFS% /reference core.id="%IFCDIR%\core.id.ifc""
set "REFS=%REFS% /reference core.math="%IFCDIR%\core.math.ifc""
set "REFS=%REFS% /reference core.env="%IFCDIR%\core.env.ifc""
set "REFS=%REFS% /reference core.path="%IFCDIR%\core.path.ifc""
set "REFS=%REFS% /reference core.assert="%IFCDIR%\core.assert.ifc""
set "REFS=%REFS% /reference runtime="%IFCDIR%\runtime.ifc""

REM ------------------------------------------------------------
REM Compile module implementations
REM ------------------------------------------------------------
echo [2/3] compile module implementations
for %%M in (%M0% %M1% %M2% %M3% %M4% %M5% %M6% %M7% %M8% %M9% %M10% %M11%) do (
  echo   impl %%M
  cl %CXX_IFC% %REFS% /Fo"%OBJDIR%\%%M.obj" "%SRCDIR%\%%M.cpp" || exit /b 1
)

echo   lib main (main() goes into the .lib)
cl %CXX_IFC% %REFS% %CXX_INC% /Fo"%OBJDIR%\lib_main.obj" "%SRCDIR%\lib_main.cpp" || exit /b 1

REM ------------------------------------------------------------
REM Archive -> mylib.lib
REM ------------------------------------------------------------
echo [3/3] archive + link
lib /nologo /OUT:"%LIBFILE%" "%OBJDIR%\*_ifc.obj" "%OBJDIR%\mylib.obj" "%OBJDIR%\core.log.obj" "%OBJDIR%\core.time.obj" "%OBJDIR%\core.error.obj" "%OBJDIR%\core.format.obj" "%OBJDIR%\core.string.obj" "%OBJDIR%\core.id.obj" "%OBJDIR%\core.math.obj" "%OBJDIR%\core.env.obj" "%OBJDIR%\core.path.obj" "%OBJDIR%\core.assert.obj" "%OBJDIR%\runtime.obj" "%OBJDIR%\lib_main.obj" || exit /b 1

REM app TU (no main)
cl %CXX_INC% /Fo"%OBJDIR%\app.obj" "%SRCDIR%\app.cpp" || exit /b 1

REM Link app.exe (main is in the .lib; app.cpp forces the anchor with #pragma comment(linker,...))
link /nologo /SUBSYSTEM:CONSOLE "%OBJDIR%\app.obj" "%LIBFILE%" /OUT:"%EXE%" || exit /b 1

REM ------------------------------------------------------------
REM Modes: smoke / tests / normal
REM ------------------------------------------------------------
if /I "%~1"=="smoke" (
  echo.
  echo [smoke] DEMO_SMOKE=1
  set "DEMO_SMOKE=1"
  "%EXE%"
  goto :done
)

REM Optional: build and run tests.exe
if /I "%~1"=="tests" (
  echo.
  echo [tests] building %TESTEXE%
  if not exist "%SRCDIR%\tests_main.cpp" (
    echo ERROR: missing "%SRCDIR%\tests_main.cpp"
    exit /b 1
  )
  cl %CXX_IFC% %REFS% %CXX_INC% /Fo"%OBJDIR%\tests_main.obj" "%SRCDIR%\tests_main.cpp" || exit /b 1
  link /nologo /SUBSYSTEM:CONSOLE "%OBJDIR%\tests_main.obj" "%LIBFILE%" /OUT:"%TESTEXE%" || exit /b 1
  "%TESTEXE%"
  goto :done
)

REM Normal run
"%EXE%"

:done
pause
