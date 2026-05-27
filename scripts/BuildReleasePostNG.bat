@echo off
setlocal

REM "=== Plugin Selection ==="
REM "Default AIO (NuclearGFX.dll). Set environment variables to switch to individual plugin targets:"
REM "e.g. set AIO=OFF && set UPSCALER=ON && BuildReleasePostNG.bat"
if not defined COMMUNITY_SHADERS set COMMUNITY_SHADERS=ON
if not defined AIO set AIO=ON
if not defined FRAMEGEN set FRAMEGEN=OFF
if not defined REFLEX set REFLEX=OFF
if not defined UPSCALER set UPSCALER=OFF
set "COMMUNITY_SHADERS=%COMMUNITY_SHADERS: =%"
set "AIO=%AIO: =%"
set "FRAMEGEN=%FRAMEGEN: =%"
set "REFLEX=%REFLEX: =%"
set "UPSCALER=%UPSCALER: =%"

echo [PostNG] COMMUNITY_SHADERS=%COMMUNITY_SHADERS%  AIO=%AIO%  FrameGen=%FRAMEGEN%  Reflex=%REFLEX%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PostNG"
set "BUILD_OUTPUT=build\PostNG\Release"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PostNG ^
    -DCOMMUNITY_SHADERS=%COMMUNITY_SHADERS% ^
    -DAIO=%AIO% ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DREFLEX=%REFLEX% ^
    -DUPSCALER=%UPSCALER%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PostNG --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

if /I "%COMMUNITY_SHADERS%"=="ON" xcopy "%BUILD_OUTPUT%\CommunityShaders.dll" "dist\F4SE\Plugins\" /I /Y
if /I "%AIO%"=="ON" xcopy "%BUILD_OUTPUT%\NuclearGFX.dll" "dist\F4SE\Plugins\" /I /Y
if /I "%FRAMEGEN%"=="ON" xcopy "%BUILD_OUTPUT%\FrameGen.dll" "dist\F4SE\Plugins\FrameGen\" /I /Y
if /I "%REFLEX%"=="ON" xcopy "%BUILD_OUTPUT%\Reflex.dll" "dist\F4SE\Plugins\Reflex\" /I /Y
if /I "%UPSCALER%"=="ON" xcopy "%BUILD_OUTPUT%\Upscaler.dll" "dist\F4SE\Plugins\Upscaler\" /I /Y

if exist "package\Common" xcopy "package\Common" "dist" /I /Y /E
if /I "%COMMUNITY_SHADERS%"=="ON" (
    xcopy "package\CommunityShaders" "dist" /I /Y /E
    if exist "package\Shaders" xcopy "package\Shaders" "dist\Shaders" /I /Y /E
    REM CommunityShaders.dll now contains ALL Features — deploy their shaders and INIs too
    if exist "package\FrameGen" xcopy "package\FrameGen" "dist" /I /Y /E
    if exist "package\Reflex" xcopy "package\Reflex" "dist" /I /Y /E
    if exist "package\Upscaler" xcopy "package\Upscaler" "dist" /I /Y /E
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
