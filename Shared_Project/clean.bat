@echo off
setlocal enabledelayedexpansion

echo =========================================
echo Cleaning build artifacts
echo =========================================
echo.

echo Files to remove:
for %%F in (
  ifc\*.ifc
  obj\*.obj
  mylib.lib
  tests.exe
  *.pdb
  *.ilk
  *.exp
  *.idb
  *.log
) do (
  if exist %%F (
    echo   - %%F
  )
)

echo.
echo Directories to remove:
for %%D in (
  App
  x64
  mylib
  .vs
  ifc
  obj
) do (
  if exist "%%D" (
    echo   - %%D\
  )
)

echo.
echo -----------------------------------------
echo Deleting files...
echo -----------------------------------------
for %%F in (
  ifc\*.ifc
  obj\*.obj
  mylib.lib
  tests.exe
  *.pdb
  *.ilk
  *.exp
  *.idb
  *.log
) do (
  if exist %%F (
    echo Deleting %%F
    del /q %%F
  )
)

echo.
echo -----------------------------------------
echo Deleting directories...
echo -----------------------------------------
for %%D in (
  App
  x64
  mylib
  .vs
  ifc
  obj
) do (
  if exist "%%D" (
    echo Removing directory %%D\
    rmdir /s /q "%%D"
  )
)

echo.
echo =========================================
echo Clean complete
echo =========================================
pause
