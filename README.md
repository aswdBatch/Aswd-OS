# Aswd OS

Bare-metal x86 (i686) command-line operating system (CLI only).

## Start here

1. Install the tools listed below.
2. Open a terminal in `C:\aswd-os`.
3. Run `build.bat`.
4. Flash `dist\aswd-usb.img` to a USB stick with Rufus using **DD mode** for real hardware.
5. Use `dist\aswd.iso` only for QEMU / VM testing.

If you just want the shortest path for hardware boot:

1. Build `dist\aswd-usb.img`.
2. Flash that image to the USB stick in Rufus.
3. Boot the machine from that USB stick.

## Building on Windows (no WSL)

### Prerequisites

You need these tools installed once. Exact paths matter — the build script expects them here.

| Tool | Expected path | Get it from |
|---|---|---|
| GNU Make 4.4.1 | `C:\tools\make.exe` | Chocolatey nupkg → extract as zip → grab `make.exe` |
| BusyBox (Unix utils) | `C:\tools\busybox.exe` | [frippery.org/busybox](https://frippery.org/files/busybox/) |
| &nbsp;&nbsp;→ also copy as | `C:\tools\mkdir.exe`, `rm.exe`, `cp.exe`, `sh.exe` | same binary, different names |
| NASM 2.16+ | `C:\NASM\nasm.exe` | [nasm.us](https://www.nasm.us/) zip release |
| i686-elf-gcc cross-compiler | `C:\i686-elf-tools\bin\` | [lordmilko/i686-elf-tools](https://github.com/lordmilko/i686-elf-tools) |
| QEMU | `C:\Program Files\qemu\` | [qemu.org](https://www.qemu.org/download/#windows) NSIS installer |
| Python 3.11 | wherever, just on PATH | [python.org](https://python.org) |
| pycdlib | — | `pip install pycdlib` |
| GRUB i386-pc modules | `C:\pkgs\grub-pc-bin-files\usr\lib\grub\i386-pc\` | Ubuntu `grub-pc-bin` .deb extracted with `tar` |
| grub-mkrescue shim | `C:\tools\grub-mkrescue.bat` | calls `C:\tools\build_iso.py` (see below) |
| ISO builder | `C:\tools\build_iso.py` | pycdlib-based script — creates bootable El Torito ISO with `boot_info_table=True` |

> **Why not just install grub-mkrescue?** MSYS2/Cygwin binaries hang when stdin isn't a real TTY. The pycdlib script is a native Python replacement that produces an identical ISO.

### Building

Use `build.bat` — it sets PATH, cleans, and builds the ISO and USB image in one shot:

```bat
cd C:\aswd-os
build.bat
```

To build **and** immediately launch in QEMU:

```bat
build.bat -run
```

To build and launch with serial output on stdio:

```bat
build.bat -run-serial
```

To build the raw USB image and launch it in QEMU:

```bat
build.bat -run-usb
```

Outputs are `dist\aswd.iso` and `dist\aswd-usb.img`.

### Why `make` alone says "Nothing to be done for 'all'"

Make only recompiles files whose sources changed since the last build. If you just ran a build and haven't touched any source files, that message is correct and expected — the ISO is already up to date. Use `build.bat` which always runs `make clean` first for a guaranteed full rebuild.

If you want incremental builds (faster, skips unchanged files) set PATH manually and call make directly:

```bat
SET PATH=C:\tools;C:\NASM;C:\i686-elf-tools\bin;C:\Program Files\qemu;%PATH%
cd C:\aswd-os
make
```

---

## Bootable USB

For real hardware, use `dist\aswd-usb.img`.

Flash with:
- **Rufus** on Windows — select `dist\aswd-usb.img`, choose **DD Image mode**, MBR partition scheme, BIOS/CSM target
- **balenaEtcher** — pick the USB image and write it directly
- `dd` on Linux/macOS

The raw USB image now uses a standard FAT32 partitioned layout with the partition starting at sector `2048`.

The ISO remains available for VM testing.

The current build is BIOS/CSM only (`i386-pc` GRUB). On UEFI machines enable **Legacy Boot / CSM** in firmware settings first.

---

## Boot output

On boot you should see:

```
Aswd OS v0.test
Hello :D
aswd>
```

## Shell commands

- `help` — list commands
- `osinfo` — show OS version
- `sysinfo` — show CPU/RAM info
- `clear` — clear screen
- `echo <text>` — print text
- `confirm <message>` — ask for confirmation
- `run <script>` — run a built-in script

## Confirmation responses

YES: `acknowledge`, `ack`, `yes`, `y`
NO: `veto`, `v`, `no`, `n`

Invalid input is treated as veto and prints a red error.

## Script system

- One command per line
- `#` comments
- `set var=value` / `$var` substitution
- `iflast ack` — skip next command if last confirm wasn't ack
- Output lines are prefixed with `█`

Built-in script: `run demo`

## Hardware notes

- 32-bit kernel, BIOS-only
- PS/2 keyboard input
- Serial output mirrored on COM1
- No libc, no malloc — static buffers only
- Everything runs in ring 0
