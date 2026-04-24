@echo off
setlocal

REM === 插件选择 ===
if not defined FRAMEGEN set FRAMEGEN=ON
if not defined UPSCALER set UPSCALER=ON

echo [PreNG] FrameGen=%FRAMEGEN%  Upscaler=%UPSCALER%

set "COMMONLIB_PATH=extern\CommonLibF4PreNG"

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

xcopy "build\PreNG\Release\*.dll" "dist\F4SE\Plugins\" /I /Y
xcopy "build\PreNG\Release\*.pdb" "dist\F4SE\Plugins\" /I /Y

xcopy "package" "dist" /I /Y /E
if exist "dist\F4SE\Plugins\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Streamline\*.dll"
if exist "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll" del /Q "dist\F4SE\Plugins\FrameGeneration\Streamline\*.dll"
if exist "dist\F4SE\Plugins\Upscaling\Streamline\*.dll" del /Q "dist\F4SE\Plugins\Upscaling\Streamline\*.dll"

pause
endlocal
