@echo off
setlocal

set "COMMONLIB_PATH=extern\CommonLibF4PreNG"

git submodule sync --recursive -- "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1
git submodule update --init --recursive "%COMMONLIB_PATH%"
if %ERRORLEVEL% NEQ 0 exit /b 1

RMDIR dist /S /Q

cmake -S . --preset=PRE-NG --check-stamp-file "build\CMakeFiles\generate.stamp"
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 exit /b 1

xcopy "build\release\*.dll" "dist\F4SE\Plugins\" /I /Y
xcopy "build\release\*.pdb" "dist\F4SE\Plugins\" /I /Y

xcopy "package" "dist" /I /Y /E

pause
endlocal
