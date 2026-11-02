@echo off
cd /d "%~dp0"
echo Read-only bootloader checks. No flash or erase commands are executed.
if not exist "bin\windows\fastboot.exe" (
  echo Missing bin\windows\fastboot.exe. Place this file in the extracted ROM folder.
  pause
  exit /b 1
)
bin\windows\fastboot.exe getvar product
bin\windows\fastboot.exe getvar unlocked
bin\windows\fastboot.exe getvar partition-size:super
bin\windows\fastboot.exe getvar partition-size:boot_a
bin\windows\fastboot.exe getvar partition-size:dtbo_a
pause
