@echo off
setlocal

REM === 插件选择 ===
REM 可在调用前设置环境变量覆盖默认值，例如：
REM   set FRAMEGEN=OFF && BuildReleasePostNG.bat
if not defined FRAMEGEN set FRAMEGEN=ON
if not defined UPSCALER set UPSCALER=ON
set "FRAMEGEN=%FRAMEGEN: =%"
set "UPSCALER=%UPSCALER: =%"

echo [PostNG] FrameGen=%FRAMEGEN%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PostNG"
set "BUILD_OUTPUT=build\PostNG\Release"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PostNG ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DUPSCALER=%UPSCALER%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PostNG --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

if /I "%FRAMEGEN%"=="ON" (
    xcopy "%BUILD_OUTPUT%\FrameGen.dll" "dist\F4SE\Plugins\" /I /Y
    if exist "%BUILD_OUTPUT%\FrameGen.pdb" xcopy "%BUILD_OUTPUT%\FrameGen.pdb" "dist\F4SE\Plugins\" /I /Y
)
if /I "%UPSCALER%"=="ON" (
    xcopy "%BUILD_OUTPUT%\Upscaler.dll" "dist\F4SE\Plugins\" /I /Y
    if exist "%BUILD_OUTPUT%\Upscaler.pdb" xcopy "%BUILD_OUTPUT%\Upscaler.pdb" "dist\F4SE\Plugins\" /I /Y
)

if exist "package\Common" xcopy "package\Common" "dist" /I /Y /E
if /I "%FRAMEGEN%"=="ON" if exist "package\FrameGen" xcopy "package\FrameGen" "dist" /I /Y /E
if /I "%UPSCALER%"=="ON" if exist "package\Upscaling" xcopy "package\Upscaling" "dist" /I /Y /E

if exist "dist\F4SE\Plugins\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Streamline\*.dll"
if exist "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll" del /Q "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll"
if exist "dist\F4SE\Plugins\Upscaling\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Upscaling\Streamline\*.dll"

pause
endlocal
