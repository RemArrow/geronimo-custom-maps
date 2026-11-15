@echo off
rem Builds the Custom Maps GML plugin.
rem   build.bat            build into build\CustomMaps\CustomMaps.dll
rem   build.bat install    also copy it into the game (refuses while the game runs)
rem Needs the GML SDK (..\ModLoader\include, or set GML_SDK) and Visual Studio 2022.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set SDK=%ROOT%external\gml\include
if not exist "%SDK%\GML\GML.hpp" set SDK=%ROOT%..\ModLoader\include
if defined GML_SDK set SDK=%GML_SDK%
set GAME=C:\Program Files (x86)\Steam\steamapps\common\GERONIMO\Geronimo\Binaries\Win64
if defined GML_GAME_DIR set GAME=%GML_GAME_DIR%
if not exist "%SDK%\GML\GML.hpp" (
  echo GML SDK not found at %SDK% - set GML_SDK
  exit /b 1
)
if not defined VCToolsInstallDir (
  call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
)
for %%D in ("%OUT%\obj" "%OUT%\CustomMaps") do if not exist "%%~D" mkdir "%%~D"
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%src\CustomMaps.cpp" /Fe"%OUT%\CustomMaps\CustomMaps.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
rem the test harness (never installed)
if not exist "%OUT%\tests\MapsDev" mkdir "%OUT%\tests\MapsDev"
cl /nologo /std:c++20 /O2 /MT /EHsc /W3 /Zi /DUNICODE /D_UNICODE /I"%SDK%" /LD /Fo"%OUT%\obj\\" /Fd"%OUT%\obj\\" "%ROOT%tests\MapsDev\MapsDev.cpp" /Fe"%OUT%\tests\MapsDev\MapsDev.dll" /link /DEBUG /INCREMENTAL:NO || exit /b 1
for /r "%OUT%" %%F in (*.exp *.lib *.ilk) do del /q "%%F"
if /i "%1"=="install" (
  tasklist /FI "IMAGENAME eq Geronimo-Win64-Shipping.exe" | find /I "Geronimo-Win64" >nul && (
    echo Geronimo is running - close it first.
    exit /b 1
  )
  if not exist "%GAME%\GML\plugins\CustomMaps" mkdir "%GAME%\GML\plugins\CustomMaps"
  copy /y "%OUT%\CustomMaps\CustomMaps.dll" "%GAME%\GML\plugins\CustomMaps\" >nul || exit /b 1
  echo === installed to "%GAME%\GML\plugins\CustomMaps"
)
echo === done
exit /b 0
