@echo off
setlocal

REM "=== Plugin Selection ==="
REM "Default AIO (aioGraphics.dll). Set environment variables to switch to individual plugin targets:"
REM "e.g. set AIO=OFF && set UPSCALER=ON && BuildReleasePostAE.bat"
if not defined AIO set AIO=ON
if not defined FRAMEGEN set FRAMEGEN=OFF
if not defined HDR set HDR=OFF
if not defined REFLEX set REFLEX=OFF
if not defined UPSCALER set UPSCALER=OFF
set "AIO=%AIO: =%"
set "FRAMEGEN=%FRAMEGEN: =%"
set "HDR=%HDR: =%"
set "REFLEX=%REFLEX: =%"
set "UPSCALER=%UPSCALER: =%"

echo [PostAE] AIO=%AIO%  FrameGen=%FRAMEGEN%  HDR=%HDR%  Reflex=%REFLEX%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PostAE"
set "BUILD_OUTPUT=build\PostAE\Release"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PostAE ^
    -DAIO=%AIO% ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DHDR=%HDR% ^
    -DREFLEX=%REFLEX% ^
    -DUPSCALER=%UPSCALER%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PostAE --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

if /I "%AIO%"=="ON" xcopy "%BUILD_OUTPUT%\aioGraphics.dll" "dist\F4SE\Plugins\fo4CS\" /I /Y
if /I "%FRAMEGEN%"=="ON" xcopy "%BUILD_OUTPUT%\FrameGen.dll" "dist\F4SE\Plugins\FrameGen\" /I /Y
if /I "%HDR%"=="ON" xcopy "%BUILD_OUTPUT%\HDR.dll" "dist\F4SE\Plugins\HDR\" /I /Y
if /I "%REFLEX%"=="ON" xcopy "%BUILD_OUTPUT%\Reflex.dll" "dist\F4SE\Plugins\Reflex\" /I /Y
if /I "%UPSCALER%"=="ON" xcopy "%BUILD_OUTPUT%\Upscaler.dll" "dist\F4SE\Plugins\Upscaler\" /I /Y

if exist "package\Common" xcopy "package\Common" "dist" /I /Y /E
if /I "%AIO%"=="ON" (
    xcopy "package\FrameGen" "dist" /I /Y /E
    xcopy "package\HDR" "dist" /I /Y /E
    xcopy "package\Reflex" "dist" /I /Y /E
    xcopy "package\Upscaler" "dist" /I /Y /E
)
if /I "%FRAMEGEN%"=="ON" if exist "package\FrameGen" xcopy "package\FrameGen" "dist" /I /Y /E
if /I "%HDR%"=="ON" if exist "package\HDR" xcopy "package\HDR" "dist" /I /Y /E
if /I "%REFLEX%"=="ON" if exist "package\Reflex" xcopy "package\Reflex" "dist" /I /Y /E
if /I "%UPSCALER%"=="ON" if exist "package\Upscaler" xcopy "package\Upscaler" "dist" /I /Y /E

pause
endlocal
