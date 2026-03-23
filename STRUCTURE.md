# Project Structure

```
Aswd-OS/
|-- Makefile
|-- README.md
|-- STRUCTURE.md
|-- TODO.md
|-- build.bat
|-- grub.cfg
|-- linker.ld
|-- build/
|-- dist/
|-- scripts/
|   |-- demo.aswd
|   |-- make_usb_image.ps1
|   `-- make_usb_image.py
`-- src/
    |-- boot/
    |   |-- bios.asm
    |   |-- boot.asm
    |   |-- gdt.asm
    |   |-- multiboot.c
    |   `-- multiboot.h
    |-- common/
    |   |-- colors.h
    |   `-- config.h
    |-- confirm/
    |   |-- confirm.c
    |   `-- confirm.h
    |-- console/
    |   |-- console.c
    |   `-- console.h
    |-- cpu/
    |   |-- cpuid.c
    |   |-- cpuid.h
    |   |-- idt.c
    |   |-- idt.h
    |   |-- irq.asm
    |   |-- pic.c
    |   |-- pic.h
    |   `-- ports.h
    |-- drivers/
    |   |-- keyboard.c
    |   |-- keyboard.h
    |   |-- serial.c
    |   |-- serial.h
    |   |-- vga.c
    |   `-- vga.h
    |-- input/
    |   |-- input.c
    |   `-- input.h
    |-- kernel.c
    |-- lib/
    |   |-- ctype.h
    |   |-- string.c
    |   `-- string.h
    |-- script/
    |   |-- builtin_scripts.c
    |   |-- builtin_scripts.h
    |   |-- script.c
    |   |-- script.h
    |   |-- vars.c
    |   `-- vars.h
    |-- shell/
    |   |-- commands.c
    |   |-- commands.h
    |   |-- shell.c
    |   |-- shell.h
    |   |-- sysinfo.c
    |   `-- sysinfo.h
    `-- usbboot/
        |-- mbr.asm
        |-- vbr.asm
        `-- stage2.asm
```
