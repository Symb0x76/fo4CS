@echo off
setlocal

REM === 插件选择 ===
if not defined FRAMEGEN set FRAMEGEN=ON
if not defined UPSCALER set UPSCALER=ON
set "FRAMEGEN=%FRAMEGEN: =%"
set "UPSCALER=%UPSCALER: =%"

echo [PreNG] FrameGen=%FRAMEGEN%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PreNG"
set "BUILD_OUTPUT=build\PreNG\Release"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PreNG ^
    -DFRAMEGEN=%FRAMEGEN% ^
    -DUPSCALER=%UPSCALER%
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build\PreNG --config Release
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
if /I "%UPSCALER%"=="ON" if exist "package\Upscaler" xcopy "package\Upscaler" "dist" /I /Y /E

if exist "dist\F4SE\Plugins\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Streamline\*.dll"
if exist "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll" del /Q "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll"
if exist "dist\F4SE\Plugins\Upscaler\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Upscaler\Streamline\*.dll"

pause
endlocal
