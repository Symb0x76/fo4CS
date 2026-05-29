@echo off
setlocal

REM "=== Plugin Selection ==="
REM "Default AIO (NuclearGFX.dll). Set environment variables to switch to individual plugin targets:"
REM "e.g. set AIO=OFF && set UPSCALER=ON && BuildReleasePreNG.bat"
if not defined AIO set AIO=ON
if not defined FRAMEGEN set FRAMEGEN=OFF
if not defined REFLEX set REFLEX=OFF
if not defined UPSCALER set UPSCALER=OFF
if not defined OVERLAY set OVERLAY=ON
set "AIO=%AIO: =%"
set "FRAMEGEN=%FRAMEGEN: =%"
set "REFLEX=%REFLEX: =%"
set "UPSCALER=%UPSCALER: =%"
set "OVERLAY=%OVERLAY: =%"

echo [PreNG] AIO=%AIO%  FrameGen=%FRAMEGEN%  Reflex=%REFLEX%  Upscaler=%UPSCALER%  Overlay=%OVERLAY%

set "COMMONLIB_PATH=extern\CommonLibF4PreNG"
set "BUILD_OUTPUT=build\PreNG\Release"
set "FIDELITYFX_RUNTIME=package\Common\F4SE\Plugins\FidelityFX\amd_fidelityfx_dx12.dll"
set "DIST_FIDELITYFX_RUNTIME=dist\F4SE\Plugins\FidelityFX\amd_fidelityfx_dx12.dll"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

if not exist "%FIDELITYFX_RUNTIME%" (
  echo [PreNG] Missing FSR runtime: %FIDELITYFX_RUNTIME%
  exit /b 1
)

RMDIR dist /S /Q

cmake -S . --preset=PreNG ^
    -DAIO=%AIO% ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DREFLEX=%REFLEX% ^
    -DUPSCALER=%UPSCALER% ^
    -DOVERLAY=%OVERLAY%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PreNG --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

if /I "%AIO%"=="ON" xcopy "%BUILD_OUTPUT%\NuclearGFX.dll" "dist\F4SE\Plugins\" /I /Y
if /I "%FRAMEGEN%"=="ON" xcopy "%BUILD_OUTPUT%\FrameGen.dll" "dist\F4SE\Plugins\FrameGen\" /I /Y
if /I "%REFLEX%"=="ON" xcopy "%BUILD_OUTPUT%\Reflex.dll" "dist\F4SE\Plugins\Reflex\" /I /Y
if /I "%UPSCALER%"=="ON" xcopy "%BUILD_OUTPUT%\Upscaler.dll" "dist\F4SE\Plugins\Upscaler\" /I /Y
if /I "%OVERLAY%"=="ON" xcopy "%BUILD_OUTPUT%\Overlay.dll" "dist\F4SE\Plugins\Overlay\" /I /Y

if exist "package\Common" xcopy "package\Common" "dist" /I /Y /E
if not exist "%DIST_FIDELITYFX_RUNTIME%" (
  echo [PreNG] Failed to package FSR runtime: %DIST_FIDELITYFX_RUNTIME%
  exit /b 1
)
if /I "%AIO%"=="ON" (
  xcopy "package\FrameGen" "dist" /I /Y /E
  xcopy "package\Reflex" "dist" /I /Y /E
  xcopy "package\Upscaler" "dist" /I /Y /E
)
if /I "%FRAMEGEN%"=="ON" if exist "package\FrameGen" xcopy "package\FrameGen" "dist" /I /Y /E
if /I "%REFLEX%"=="ON" if exist "package\Reflex" xcopy "package\Reflex" "dist" /I /Y /E
if /I "%UPSCALER%"=="ON" if exist "package\Upscaler" xcopy "package\Upscaler" "dist" /I /Y /E
pause
endlocal
