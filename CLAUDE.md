# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

The project builds on Windows without WSL. Always use `build.bat` for a guaranteed full rebuild (it runs `make clean` first):

```bat
build.bat            # builds dist\aswd.iso + dist\aswd-usb.img
build.bat -run       # build + launch in QEMU (GUI, no serial)
build.bat -run-serial  # build + QEMU with serial output on stdio (best for debugging)
build.bat -run-usb   # build + launch USB image in QEMU
```

For incremental builds only (when you know what changed):
```bat
SET PATH=C:\tools;C:\NASM;C:\i686-elf-tools\bin;C:\Program Files\qemu;%PATH%
make
```

The Makefile targets `run`, `run-serial`, `run-vga` add `-netdev user,id=n0 -device rtl8139,netdev=n0` for QEMU networking. Real hardware uses the USB image (`dist\aswd-usb.img`) flashed with Rufus in DD mode.

There are no tests. Validation is: build succeeds, QEMU boots to the desktop, features work interactively.

## Architecture Overview

### Boot → Kernel → GUI Flow

`kernel_main()` in `src/kernel.c` is the entry point. It runs a **boot launcher** (menu letting the user choose target), then calls `kernel_boot_normal()` which:
1. Inits storage, timer, keyboard, PCI, USB, network
2. Calls `auth_gui_run()` — fullscreen login/setup gate
3. Enters `gui_run()` in a loop (the entire GUI desktop is one infinite loop)

Three boot targets exist: `BOOT_TARGET_NORMAL_GUI` (default), `BOOT_TARGET_SHELL_ONLY` (raw shell), `BOOT_TARGET_FS_LAB` (filesystem debug shell).

### Graphics System

`src/drivers/gfx.c` is the graphics abstraction. At init it tries VBE/BGA for a linear framebuffer; falls back to VGA text mode. All rendering goes through `gfx_fill_rect`, `gfx_draw_string`, `gfx_put_pixel`, etc. A **double buffer** (`gfx_backbuffer()` / `gfx_swap()`) avoids tearing. Colors are `uint32_t` packed as `0x00RRGGBB` via `gfx_rgb(r,g,b)`.

The font (`src/drivers/font.c`) is a fixed 8×16 bitmap font. `FONT_WIDTH` and `FONT_HEIGHT` are the character cell dimensions used everywhere for layout math.

### GUI Window System

`src/gui/gui.h` + `src/gui/gui.c` implement the desktop. Key data structures:
- `gui_window_t` — one window: frame rect, content rect, focus state, drag state, and four callbacks: `on_paint`, `on_tick`, `on_key`, `on_mouse`, `on_close`
- `gui_app_t` — registered app: id, label, icon kind, `launch()` fn, `in_store` flag (1 = App Store only, not on desktop)
- Up to `GUI_MAX_WINDOWS` (8) windows at once

**Adding a new app**: implement `{name}_gui_launch()` in `src/gui/{name}_gui.c`, add a `gui_app_t` entry in `gui.c`'s app table, add `GUI_ICON_{NAME}` to the enum in `gui.h`, add an icon badge color in `gui.c`'s icon painter, add the source file to `Makefile`.

The GUI main loop (`gui_run`) calls `run_idle_ticks()` on every iteration with no pending events. That function calls `usb_poll()` and `net_poll()` — this is how USB HID and network packets are processed.

### Input System

`src/input/input.c` multiplexes PS/2 keyboard (IRQ1 via `src/drivers/keyboard.c`), USB HID keyboard (injected via `keyboard_push_char()`), and mouse into a unified `input_event_t` queue. The GUI calls `input_try_get_event()` to drain it non-blocking.

### Networking Stack

All in `src/net/`. The layering is:
```
http.c → tcp.c / dns.c
             ↓
           udp.c
             ↓
           ip.c → icmp.c
             ↓
           arp.c
             ↓
         ethernet.c
             ↓
    net.c (NIC dispatch)
    rtl8139.c / rtl8168.c / e1000.c
```

`net_init()` runs a PCI scan and tries each driver in order. On success it calls `dhcp_start()`. `net_poll()` (called from the GUI idle tick) drains received frames and calls `dhcp_poll()`.

**DHCP**: `src/net/dhcp.c` runs a DISCOVER→REQUEST→BOUND state machine. When bound, calls `net_apply_dhcp()` which writes the lease into `net_info_t`.

**RTL8168 BAR note**: Real RTL8111/8168 chips use a 64-bit MMIO BAR at BAR1 (PCI config offset 0x14). The driver reads BAR1 first, then falls back to BAR2. Do not change this back to BAR2-only.

**Packet buffer convention**: Callers allocate `ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN + payload` and pass the whole buffer to `udp_send()`. The `payload_len` argument to `udp_send` is the UDP payload size only — `udp_send` adds `UDP_HEADER_LEN` internally.

### Storage & Filesystem

`src/drivers/ata.c` — PIO ATA driver. `src/drivers/fat32.c` — FAT32 read/write. `src/fs/vfs.c` — thin VFS wrapper. User files live under `/ROOT/` on the FAT32 volume. System files are protected; only `/ROOT/` is writable.

### Auth & Users

`src/auth/auth_store.c` — stores admin credentials on disk. `src/auth/auth_gui.c` — fullscreen login/setup screen shown before the desktop. `src/users/users.c` — multi-user list (persisted to disk).

### USB

`src/usb/uhci.c` handles UHCI controllers (the only fully implemented host). Mouse (HID protocol 0x02) and keyboard (HID protocol 0x01) both use interrupt TDs. Keyboard reports are translated to ASCII/special keys and injected via `keyboard_push_char()`.

## Constraints

- **No malloc, no libc** — all storage is static buffers. When adding features, size all buffers at compile time.
- **Ring 0 only** — no privilege separation, no syscalls.
- **32-bit i686 target** — compiler is `i686-elf-gcc`, flags include `-ffreestanding -fno-builtin`.
- **Single-threaded** — no concurrency. Polling loops in the GUI idle tick handle all async work.
- **No floating point** — use integer arithmetic throughout.
- `src/lib/string.c` provides `str_copy`, `str_cat`, `str_len`, `str_ncmp`, `mem_copy`, `mem_set`, `u32_to_dec`. Use these instead of any libc equivalents.

## Release Workflow

When an update is finished:
1. Add a summary entry to `src/common/changelog.c` — the in-OS `OS Info` app reads from this file.
2. If the version changes, update `ASWD_OS_VERSION` in `src/common/config.h` to match the newest changelog entry.
3. The work is not complete until the changelog is updated.
