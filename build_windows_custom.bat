@echo off
setlocal EnableDelayedExpansion

echo --- Ustawienia stałe (absolutne ścieżki) ---

echo MSVC 2019 Build Tools
set MSVC_VCVARS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat

echo Qt 6.4.2 msvc2019_64
set QT_BIN_DIR=C:\Qt\6.4.2\msvc2019_64\bin

echo STM32 DFU Driver
set STM32_DRIVER_DIR=C:\Users\jzakr\Desktop\STM32 Driver

echo JOM (jeśli używasz alternatywnego make)
set JOM=C:\Qt\Tools\QtCreator\bin\jom.exe

echo Katalog projektu (dostosuj jeśli masz inny katalog)
set PROJECT_DIR=C:\Users\jzakr\Desktop\qFlipper
set BUILD_DIR=%PROJECT_DIR%\build
set QML_DIR=%PROJECT_DIR%\application
set DRIVER_TOOL_DIR=%PROJECT_DIR%\driver-tool

set TARGET=qFlipper
set TARGET_CLI=qFlipper-cli
set PROTO_TARGET=flipperproto
set DIST_DIR=%BUILD_DIR%
set PLUGINS_DIR=%DIST_DIR%\plugins

echo OpenSSL binaria
set OPENSSL_DIR=C:\Qt\Tools\OpenSSLv3\Win_x64\bin

echo Visual C++ Redistributables
set VCREDIST_DIR=C:\Qt\vcredist
set VCREDIST2019_EXE=%VCREDIST_DIR%\vcredist_msvc2019_x64.exe
set VCREDIST2010_EXE=%VCREDIST_DIR%\vcredist_x64.exe

echo NSIS (installer)
set NSIS=C:\Program Files (x86)\NSIS\makensis.exe

echo --- Import środowiska kompilatora MSVC ---
if not exist "%MSVC_VCVARS_PATH%" (
    echo Could not find MSVC environment
    goto error
)
call "%MSVC_VCVARS_PATH%"

echo --- Zamkniecie aplikacji qFlipper.exe (o ile jest otwarta) ---
taskkill /IM qFlipper.exe /F

echo --- Przygotowanie builda ---
rem if exist "%BUILD_DIR%" (
rem     echo Usuwam stary build...
rem     rmdir /S /Q "%BUILD_DIR%"
rem )
rem mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

echo --- Budowanie aplikacji ---
"%QT_BIN_DIR%\qmake.exe" "%PROJECT_DIR%\qFlipper.pro" -spec win32-msvc "CONFIG+=qtquickcompiler" || goto error
nmake || goto error

echo --- Deploy aplikacji ---
if not exist "%DIST_DIR%" (
    echo Brak katalogu wynikowego aplikacji: %DIST_DIR%
    goto error
)
cd "%DIST_DIR%"

if exist "%PLUGINS_DIR%\%PROTO_TARGET%0.dll" (
    "%QT_BIN_DIR%\windeployqt.exe" --release --no-compiler-runtime --dir "%DIST_DIR%" "%PLUGINS_DIR%\%PROTO_TARGET%0.dll"
) else (
    echo Ostrzeżenie: %PLUGINS_DIR%\%PROTO_TARGET%0.dll nie istnieje, pomijam deploy tego pluginu.
)
"%QT_BIN_DIR%\windeployqt.exe" --release --no-compiler-runtime --qmldir "%QML_DIR%" "%TARGET%.exe" || goto error
"%QT_BIN_DIR%\windeployqt.exe" --release --no-compiler-runtime "%TARGET_CLI%.exe" || goto error

echo Kopiowanie OpenSSL
if exist "%OPENSSL_DIR%\*.dll" (
    copy /Y "%OPENSSL_DIR%\*.dll" .
) else (
    echo Ostrzeżenie: Brak plików DLL OpenSSL w %OPENSSL_DIR%
)

echo Kopiowanie drivera
if exist "%STM32_DRIVER_DIR%" (
    xcopy /Y /E /I "%STM32_DRIVER_DIR%" "%DIST_DIR%\STM32 Driver"
) else (
    echo Ostrzeżenie: Brak katalogu z driveecho: %STM32_DRIVER_DIR%
)

rem echo Kopiowanie vcredist
rem if exist "%VCREDIST2019_EXE%" (
rem     copy /Y "%VCREDIST2019_EXE%" .
rem ) else (
rem     echo Ostrzeżenie: Brak pliku %VCREDIST2019_EXE%
rem )
rem if exist "%VCREDIST2010_EXE%" (
rem     copy /Y "%VCREDIST2010_EXE%" .
rem ) else (
rem     echo Ostrzeżenie: Brak pliku %VCREDIST2010_EXE%
rem )
rem 
rem if defined SIGNING_TOOL (
rem     echo Opcjonalny podpis binarek
rem     call "%SIGNING_TOOL%" "%DIST_DIR%\%TARGET%.exe" || goto error
rem     call "%SIGNING_TOOL%" "%DIST_DIR%\%TARGET_CLI%.exe" || goto error
rem )
rem 
rem echo Tworzenie archiwum ZIP
rem tar -a -cf "%BUILD_DIR%\qFlipper-64bit.zip" *
rem 
rem echo Tworzenie instalatora NSIS
rem cd "%PROJECT_DIR%"
rem "%NSIS%" /DNAME=%TARGET% /DARCH_BITS=64 installer_windows.nsi || goto error
rem 
rem timeout /T 5 /NOBREAK > nul
rem 
rem if defined SIGNING_TOOL (
rem     call "%SIGNING_TOOL%" "%BUILD_DIR%\qFlipperSetup-64bit.exe" || goto error
rem )
rem 
rem echo The resulting installer is %BUILD_DIR%\qFlipperSetup-64bit.exe.
echo Finished, launching qFlipper.exe ...
start "" "qFlipper.exe"
exit 0

:error
echo There were errors during the build process!
exit 1