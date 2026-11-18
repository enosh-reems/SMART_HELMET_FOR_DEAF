@echo off
title Smart Helmet ESP32-S3 Flasher
color 0A
echo =======================================================================
echo          SMART HELMET ESP32-S3 1-CLICK FIRMWARE FLASHER
echo =======================================================================
echo.
echo This tool will flash the complete Smart Helmet AI firmware to an ESP32-S3:
echo  - Bootloader               @ 0x0
echo  - Partition Table (16MB)   @ 0x8000
echo  - Firmware + AI Model      @ 0x10000
echo  - Speech / Audio Models    @ 0x510000
echo.
echo Please connect your ESP32-S3 via USB.
echo.

python --version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Python is not found in PATH!
    echo Please ensure Python is installed with esptool (pip install esptool).
    pause
    exit /b 1
)

python -m esptool version >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [INFO] Installing esptool...
    pip install esptool
)

echo Available Serial Ports:
python -c "import serial.tools.list_ports as lp; [print('  ' + p.device + ' - ' + p.description) for p in lp.comports()]" 2>nul

echo.
set /p COMPORT="Enter your ESP32 COM port (e.g. COM3, COM4, COM5): "
if "%COMPORT%"=="" (
    echo [ERROR] No COM port entered!
    pause
    exit /b 1
)

echo.
echo Flashing to %COMPORT% at 460800 baud...
echo (If the ESP32 fails to connect, hold the BOOT button, press and release RESET, then release BOOT)
echo.

python -m esptool --chip esp32s3 -p %COMPORT% -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 template-app.bin 0x510000 srmodels.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo =======================================================================
    echo   FLASH SUCCESSFUL! The Smart Helmet AI is now running on your ESP32-S3!
    echo =======================================================================
) else (
    echo.
    echo [ERROR] Flashing failed! Check cable connection and COM port.
)

echo.
pause
