@echo off
REM Build/flash helper for the Freenove ESP32-S3 CYD (bunbun's board).
REM
REM Uses idf.py rather than PlatformIO on purpose. PlatformIO mangles the path for components
REM that embed binary data (target_add_binary_data) once Arduino-as-a-component drags in
REM Espressif's telemetry stack: it emits .pio/build/<env>/.pio/build/<env>/https_server.crt.S
REM and then fails on a file that is present on disk. Upstream CI builds with idf.py and the
REM repo already ships one PlatformIO workaround, so this stops fighting the wrong tool.
REM
REM export.bat cannot be chained with && from another shell ? it uses goto labels internally and
REM dies with "cannot find the batch label specified". Hence this wrapper.
REM
REM   build-freenove.bat              build
REM   build-freenove.bat flash        build + flash (set PORT= first, default COM24)
REM   build-freenove.bat menuconfig   Kconfig UI

setlocal
set IDF_TOOLS_PATH=C:\Espressif
REM export.bat needs a python on PATH. PlatformIO keeps a real (non-venv) interpreter here;
REM its penv one cannot be used because IDF refuses to build a virtualenv inside a virtualenv.
set PATH=%USERPROFILE%\.platformio\python3;%USERPROFILE%\.platformio\python3\Scripts;%PATH%
call "%USERPROFILE%\.platformio\packages\framework-espidf@3.50503.0\export.bat"
if errorlevel 1 goto :eof

cd /d "%~dp0"
if "%PORT%"=="" set PORT=COM24

set DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.freenove-s3

if "%1"=="" (
  idf.py -B build-freenove -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="%DEFAULTS%" build
) else if "%1"=="flash" (
  idf.py -B build-freenove -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="%DEFAULTS%" -p %PORT% flash
) else (
  idf.py -B build-freenove -DIDF_TARGET=esp32s3 -DSDKCONFIG_DEFAULTS="%DEFAULTS%" %*
)
endlocal
