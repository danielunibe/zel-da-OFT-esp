@echo off
setlocal
set "G02C_DIR=%~dp0"
start "Ocarina of Time PC - G02C build-test" "%G02C_DIR%..\..\runtime\build-test\soh.exe"
endlocal
