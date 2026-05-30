@echo off
setlocal
set "MINGW=C:\Users\fengz\Desktop\msys64\mingw64\bin"
set "PATH=%MINGW%;%PATH%"
cd /d "%~dp0src"
echo === Cleaning .o files ===
del /q *.o 2>nul
echo === Compiling ===
mingw32-make CC="%MINGW%\gcc" -j4
if %ERRORLEVEL% equ 0 (
    echo === Build OK ===
) else (
    echo === Build FAILED ===
    pause
)
endlocal
