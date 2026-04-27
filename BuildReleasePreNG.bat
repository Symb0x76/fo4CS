@echo off
setlocal

REM === 插件选择 ===
if not defined FRAMEGEN set FRAMEGEN=ON
if not defined REFLEX set REFLEX=ON
if not defined UPSCALER set UPSCALER=ON
set "FRAMEGEN=%FRAMEGEN: =%"
set "REFLEX=%REFLEX: =%"
set "UPSCALER=%UPSCALER: =%"

echo [PreNG] FrameGen=%FRAMEGEN%  Reflex=%REFLEX%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PreNG"
set "BUILD_OUTPUT=build\PreNG\Release"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PreNG ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DREFLEX=%REFLEX% ^
    -DUPSCALER=%UPSCALER%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PreNG --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

if /I "%FRAMEGEN%"=="ON" xcopy "%BUILD_OUTPUT%\FrameGen.dll" "dist\F4SE\Plugins\" /I /Y
if /I "%REFLEX%"=="ON" xcopy "%BUILD_OUTPUT%\Reflex.dll" "dist\F4SE\Plugins\" /I /Y
if /I "%UPSCALER%"=="ON" xcopy "%BUILD_OUTPUT%\Upscaler.dll" "dist\F4SE\Plugins\" /I /Y

if exist "package\Common" xcopy "package\Common" "dist" /I /Y /E
if /I "%FRAMEGEN%"=="ON" if exist "package\FrameGen" xcopy "package\FrameGen" "dist" /I /Y /E
if /I "%REFLEX%"=="ON" if exist "package\Reflex" xcopy "package\Reflex" "dist" /I /Y /E
if /I "%UPSCALER%"=="ON" if exist "package\Upscaler" xcopy "package\Upscaler" "dist" /I /Y /E

pause
endlocal
