@echo off
echo Compiling vsng_telemetry.cpp into vsng_telemetry.dll...
x86_64-w64-mingw32-g++ -shared -o vsng_telemetry.dll vsng_telemetry.cpp vsng_telemetry.def -lws2_32 -static-libgcc -static-libstdc++ -O2
if %ERRORLEVEL% NEQ 0 (
    echo Compilation failed! Make sure MinGW is installed and in your PATH.
    pause
    exit /b %ERRORLEVEL%
)
echo Compilation successful!
echo You can now copy vsng_telemetry.dll to your Virtual Sailor NG folder.
pause
