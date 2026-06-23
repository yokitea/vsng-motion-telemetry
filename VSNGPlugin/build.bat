@echo off
title VSNG SimHub Plugin Builder
color 0A

echo ============================================================
echo   Virtual Sailor NG - SimHub Plugin Compiler
echo ============================================================
echo.

:: --- Cari lokasi SimHub ---
set "SIMHUB_PATH=C:\Program Files (x86)\SimHub"
if not exist "%SIMHUB_PATH%\SimHub.Plugins.dll" (
    set "SIMHUB_PATH=C:\Program Files\SimHub"
)
if not exist "%SIMHUB_PATH%\SimHub.Plugins.dll" (
    echo [ERROR] SimHub tidak ditemukan di folder default.
    echo         Edit variabel SIMHUB_PATH di baris ke-12 file ini.
    pause
    exit /b 1
)
echo [OK] SimHub ditemukan: %SIMHUB_PATH%

:: --- Cari csc.exe (.NET Framework compiler, built-in Windows) ---
set "CSC="
for %%v in (4.0.30319) do (
    if exist "%WINDIR%\Microsoft.NET\Framework64\%%v\csc.exe" (
        set "CSC=%WINDIR%\Microsoft.NET\Framework64\%%v\csc.exe"
    )
)
if not defined CSC (
    :: Cari versi apapun
    for /f "delims=" %%i in ('dir /b /s "%WINDIR%\Microsoft.NET\Framework64\csc.exe" 2^>nul') do set "CSC=%%i"
)
if not defined CSC (
    echo [ERROR] csc.exe tidak ditemukan. Pastikan .NET Framework terinstall.
    pause
    exit /b 1
)
echo [OK] Compiler: %CSC%

:: --- Referensi DLL SimHub yang diperlukan ---
set "REFS=/reference:"%SIMHUB_PATH%\SimHub.Plugins.dll""
set "REFS=%REFS% /reference:"%SIMHUB_PATH%\GameReaderCommon.dll""
set "REFS=%REFS% /reference:"%SIMHUB_PATH%\log4net.dll""

:: --- Kompilasi ---
echo.
echo [*] Mengkompilasi VSNGPlugin.cs ...
"%CSC%" /target:library /optimize+ /out:VSNGPlugin.dll %REFS% VSNGPlugin.cs

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [GAGAL] Kompilasi gagal. Periksa pesan error di atas.
    pause
    exit /b 1
)

echo.
echo [OK] Kompilasi berhasil: VSNGPlugin.dll

::  --- Copy ke ROOT folder SimHub (bukan subfolder Plugins!) ---
copy /Y "VSNGPlugin.dll" "%SIMHUB_PATH%\VSNGPlugin.dll" >nul
if %ERRORLEVEL% EQU 0 (
    echo [OK] Plugin berhasil di-install ke:
    echo      %PLUGINS_PATH%\VSNGPlugin.dll
    echo.
    echo ============================================================
    echo   SELESAI! Restart SimHub dan cek "Available properties"
    echo   untuk properti VSNGPlugin_RPM, VSNGPlugin_Speed_Knots, dll.
    echo ============================================================
) else (
    echo [WARNING] Gagal copy ke folder Plugins. Coba jalankan sebagai Administrator.
    echo          Atau copy manual: VSNGPlugin.dll -> %PLUGINS_PATH%\
)

echo.
pause
