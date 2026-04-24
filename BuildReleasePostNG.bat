@echo off
setlocal

REM === 插件选择 ===
REM 可在调用前设置环境变量覆盖默认值，例如：
REM   set FRAMEGEN=OFF && BuildReleasePostNG.bat
if not defined FRAMEGEN set FRAMEGEN=ON
if not defined UPSCALER set UPSCALER=ON

echo [PostNG] FrameGen=%FRAMEGEN%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PostNG"

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

xcopy "build\PostNG\Release\*.dll" "dist\F4SE\Plugins\" /I /Y
xcopy "build\PostNG\Release\*.pdb" "dist\F4SE\Plugins\" /I /Y

xcopy "package" "dist" /I /Y /E
if exist "dist\F4SE\Plugins\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Streamline\*.dll"
if exist "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll" del /Q "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll"
if exist "dist\F4SE\Plugins\Upscaling\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Upscaling\Streamline\*.dll"

pause
endlocal
