@echo off
setlocal
set "TOOLS=C:\tools"
set "NASM=C:\NASM"
set "CROSS=C:\i686-elf-tools\bin"
set "QEMU=C:\Program Files\qemu"
set "PATH=%TOOLS%;%NASM%;%CROSS%;%QEMU%;%PATH%"
set "PROJECT=C:\aswd-os"
if not exist "%TOOLS%\make.exe" ( echo [ERROR] make.exe not found at %TOOLS% & exit /b 1 )
if not exist "%NASM%\nasm.exe" ( echo [ERROR] nasm.exe not found at %NASM% & exit /b 1 )
if not exist "%CROSS%\i686-elf-gcc.exe" ( echo [ERROR] i686-elf-gcc not found at %CROSS% & exit /b 1 )
echo [*] Cleaning previous build...
cd /d "%PROJECT%"
"%TOOLS%\make.exe" clean < nul
echo [*] Building...
"%TOOLS%\make.exe" < nul
if errorlevel 1 ( echo [ERROR] Build failed. & exit /b 1 )
echo [*] Building USB image...
"%TOOLS%\make.exe" usb < nul
if errorlevel 1 ( echo [ERROR] USB image build failed. & exit /b 1 )
echo.
echo [OK] Built:
echo     %PROJECT%\dist\aswd.iso
echo     %PROJECT%\dist\aswd-usb.img
if "%1"=="-run" ( echo [*] Launching QEMU... & "%QEMU%\qemu-system-i386" -cdrom dist\aswd.iso -boot d -m 64M -vga std -display sdl )
if "%1"=="-run-serial" ( echo [*] Launching QEMU... & "%QEMU%\qemu-system-i386" -cdrom dist\aswd.iso -boot d -m 64M -vga std -serial stdio )
if "%1"=="-run-usb" ( echo [*] Launching QEMU... & "%QEMU%\qemu-system-i386" -drive file=dist\aswd-usb.img,format=raw,if=ide -m 64M -serial stdio )
if "%1"=="-flash" (
  net session >nul 2>&1
  if errorlevel 1 (
    powershell -NoProfile -Command "Add-Type -AssemblyName PresentationFramework; [System.Windows.MessageBox]::Show('YOU NEED ADMIN. DONT WASTE UR TIME WAITING IF ITS GONNA FAIL PLS','Aswd OS Flash',0,48) | Out-Null"
    echo [ERROR] Flashing requires Administrator. Open PowerShell as Administrator and run: cmd /c build.bat -flash
    exit /b 1
  )
  echo [*] Flashing USB... & powershell -ExecutionPolicy Bypass -File scripts\flash_usb.ps1
)
endlocal
