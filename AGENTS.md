# AswdOS — Agent Guide

### Creators (Aswd's) note:
Yes infact i did sit here for the past 3 hours to write it up with minimal AI help, how stupid of me, i know.

## Project Overview

**AswdOS** is a bare-metal hobby operating system targeting 32-bit x86 (i386). Current version: **v0.9.1**.

- Runs in ring 0 with no memory management — everything shares one flat address space.
- **No libc, no malloc** — all buffers are static and sized at compile time.
- BIOS/CSM boot only (no UEFI support).
- Custom bootloader chain for USB, GRUB multiboot for ISO.
- Features: FAT32 filesystem, full TCP/IP networking, USB host stack, GUI desktop with 15+ apps, a shell, two scripting systems, and user authentication.

Live demo: [https://aswdbatch.github.io/Aswd-OS](https://aswdbatch.github.io/Aswd-OS) (runs via v86 JS emulator — slower than native).

---

## Architecture at a Glance

| Aspect | Detail |
|---|---|
| Architecture | i386 (32-bit protected mode) |
| Entry point | `_start` in `src/boot/boot.asm` (multiboot) or `bios_start` in `src/boot/bios.asm` (standalone BIOS) |
| Kernel base | 1 MiB (`0x00100000`) |
| Compiler | `i686-elf-gcc`, `-std=c99 -O2 -ffreestanding` |
| Graphics | VESA framebuffer (from multiboot or Bochs BGA) with backbuffer + dirty-rect present |
| Text output | Direct VGA text-mode (`0xB8000`) or virtual console rendered onto graphics backbuffer |
| Debugging | Serial COM1 (0x3F8) mirrors all output; BIOS boot also writes to `dbg.log` via debugcon |

---

## Build System

### Toolchain

| Tool | Expected path | Purpose |
|---|---|---|
| GNU Make | `C:\tools\make.exe` | Build orchestration |
| NASM | `C:\NASM\nasm.exe` | Assembly (boot, exceptions, IRQ stubs) |
| i686-elf-gcc | `C:\i686-elf-tools\bin\` | C cross-compiler |
| i686-elf-objcopy | `C:\i686-elf-tools\bin\` | ELF → raw binary conversion |
| QEMU | `C:\Program Files\qemu\` | Emulator for testing |
| Python 3.11 | on PATH | USB image builder, font asset generator |
| pycdlib | pip | ISO creation (fallback for grub-mkrescue) |
| GRUB i386-pc modules | `C:\pkgs\grub-pc-bin-files\usr\lib\grub\i386-pc\` | ISO boot code |

### Makefile Targets

| Target | Output | Description |
|---|---|---|
| `all` | — | Alias for `iso` |
| `dist/kernel.elf` | ELF binary | GRUB multiboot kernel (entry: `_start`) |
| `dist/kernel-bios.elf` | ELF binary | Standalone BIOS kernel (entry: `bios_start`) |
| `dist/kernel-bios.bin` | Raw binary | Flat binary for USB bootloader |
| `iso` | `dist/aswd.iso` | Bootable ISO with GRUB |
| `usb` / `usb-img` | `dist/aswd-usb.img` | Bootable USB disk image with custom bootloader |
| `run` | — | QEMU with ISO, 64 MB RAM, emulated RTL8139 |
| `run-serial` | — | QEMU with ISO + serial on stdio |
| `run-vga` | — | Same as `run-serial` |
| `run-usb` | — | QEMU booting from USB image via IDE |
| `run-usb-debug` | `dbg.log` | QEMU USB boot with debugcon output captured |
| `clean` | — | Removes `obj/` and `dist/` |

### build.bat Flags

| Flag | Behavior |
|---|---|
| (none) | Cleans, builds ISO + USB image |
| `-run` | Build + launch QEMU with ISO (SDL display) |
| `-run-serial` | Build + launch QEMU with serial console |
| `-run-usb` | Build + launch QEMU booting from USB image |
| `-flash` | Flash USB image to physical USB (requires Admin) |

### Asset Generation

- `scripts/generate_font_assets.py` — Renders TTF fonts (AdwaitaSans/Mono) into C structs at multiple sizes (12–32px). Reads from `assets/upstream/fonts/adwaita/`, outputs to `src/assets/font_assets.{c,h}`.
- `scripts/generate_icon_assets.mjs` — Rasterizes SVG icons into RGBA/alpha C arrays. Reads from `assets/upstream/icons/`, outputs to `src/assets/icon_assets.{c,h}`. Dependencies in `tools/ui-assets/package.json` (`@resvg/resvg-js`, `pngjs`).

---

## Complete Directory Structure

```
C:\aswd-os/
├── Makefile — Build system: compiles 107 C files + 6 ASM files, produces ISO and USB image
├── build.bat — Windows build wrapper: sets PATH, validates tools, runs make, optionally launches QEMU
├── linker.ld — GNU linker script: kernel at 1 MiB, sections .multiboot .text .rodata .data .bss (all 4K-aligned)
├── grub.cfg — GRUB config: 0s timeout, prefers 1366x768x32, multiboots /boot/kernel.elf
├── AGENTS.md — This file: project guide, structure, conventions
├── README.md — User-facing docs: build instructions, shell commands, boot notes
├── CLAUDE.md — Instructions for Claude Code (AI assistant)
├── TODO.md — Planned features and improvements
├── AX_docs.md — Documentation for the Ax scripting language
├── README.md — User-facing docs: build instructions, shell commands, boot notes
├── example.png — Screenshot of the OS
│
├── scripts/
│   ├── demo.aswd — Example .aswd script: demonstrates echo, osinfo, confirm/iflast, variables
│   ├── flash_usb.ps1 — Safe USB flasher: lists only USB disks, requires Admin, double-checks before writing
│   ├── make_usb_image.ps1 — PowerShell wrapper: resolves paths, invokes make_usb_image.py with Python 3.11
│   ├── make_usb_image.py — USB image builder (Python): assembles MBR/VBR/stage2, creates FAT32 filesystem, embeds kernel, creates /USERS and /ROOT directories
│   ├── generate_font_assets.py — Font generator (Python): renders TTF → C structs with glyph bitmaps and metrics
│   └── generate_icon_assets.mjs — Icon generator (Node.js): rasterizes SVG → C arrays of RGBA/alpha pixel data
│
├── docs/ — v86 web demo (GitHub Pages)
│   ├── index.html — Web page UI: "Boot AswdOS" button, loading bar, emulator canvas, fullscreen/screenshot controls
│   ├── libv86.js — v86 x86 emulator JavaScript library
│   ├── v86.wasm — v86 WebAssembly binary
│   ├── seabios.bin — SeaBIOS firmware image for v86
│   ├── vgabios.bin — VGA BIOS image for v86
│   ├── aswd.iso — ISO image (present but emulator boots the USB image)
│   └── aswd-usb.img — USB image booted as hda in the emulator
│
├── tools/
│   └── ui-assets/ — Node.js dev package: @resvg/resvg-js + pngjs for icon asset generation
│
├── Aswd-OS/ — Separate nested git repository (placeholder/related project)
│
└── src/ — All OS source code
    ├── kernel.c — OS entry point (kernel_main). Initializes IDT, serial, multiboot info, gfx, vga, console.
    │              Runs boot launcher to let user pick target (GUI/TUI/shell/FS-lab). Dispatches to
    │              kernel_boot_normal() or kernel_boot_fs_lab(). Normal path: storage → VFS → users →
    │              timer → keyboard → PCI → USB → network → auth screen → GUI main loop or shell.
    │
    ├── auth/ — User authentication
    │   ├── auth_gui.c — Graphical login/setup screen: user avatar tiles, PIN entry, error feedback, session management
    │   ├── auth_gui.h — Declares auth_gui_run(target) — entry point to the graphical auth flow
    │   ├── auth_store.c — Runtime credential verification (devacc hardcoded PIN), active username tracking, begin/end session
    │   └── auth_store.h — Declares devacc credential constants, PIN verification, session API
    │
    ├── assets/ — Pre-rendered font and icon data (auto-generated by scripts)
    │   ├── font_assets.c — Bitmap font glyphs for AdwaitaSans (12/16/20/24/32px) and AdwaitaMono (13/16px)
    │   ├── font_assets.h — Defines font_asset_face_t and font_asset_glyph_t structs; declares g_font_asset_faces[]
    │   ├── icon_assets.c — RGBA/alpha pixel data for all system and app icons at multiple sizes
    │   └── icon_assets.h — Defines icon_asset_variant_t; declares g_icon_asset_variants[]
    │
    ├── boot/ — Boot entry points and boot UI
    │   ├── bios.asm — Standalone BIOS entry: clears BSS, sets up stack, flushes GDT, sends serial 'B', calls kernel_main(0,0)
    │   ├── boot.asm — GRUB multiboot entry: multiboot header (magic 0x1BADB002, flags, preferred 1366x768x32), saves PIC masks, flushes GDT, calls kernel_main(magic, ebx)
    │   ├── bootui.c — Boot splash screen with animated progress stages, boot target selection (GUI/TUI/shell/FS-lab), RTC-based timeout, keyboard navigation
    │   ├── bootui.h — Defines boot_target_t enum (NORMAL_GUI, TUI_LEGACY, SHELL_ONLY, FS_LAB), boot_bugcheck_style_t, boot_selection_t; declares boot loading progress API
    │   ├── gdt.asm — Global Descriptor Table: null segment, 32-bit kernel code, 32-bit kernel data, 16-bit code segment (for trampoline); gdt_flush via LGDT
    │   ├── multiboot.c — Parses Multiboot info from GRUB: memory (lower/upper KB), framebuffer (address, pitch, width, height, bpp), cmdline flags
    │   ├── multiboot.h — Declares multiboot_init() and getters for memory, framebuffer, quiet-boot flag
    │   └── videoinfo.h — Inline helpers to read VBE framebuffer info saved by stage2 at low-memory addresses (0x0510–0x051D)
    │
    ├── common/ — Shared configuration and utilities
    │   ├── boot_log.c — Ring buffer (24 lines × 72 cols) capturing boot progress; dumps to VGA console on request; skipped in quiet mode
    │   ├── boot_log.h — Declares boot_log_line(msg) and boot_log_dump()
    │   ├── changelog.c — Embedded release notes for each version (dates, summaries, bullet-point details); read by OS Info app
    │   ├── changelog.h — Defines changelog_entry_t; declares changelog_count(), changelog_entry_at(), changelog_latest()
    │   ├── colors.h — VGA 16-color palette enum, palette array, vga_make_color() inline helper
    │   ├── config.h — OS metadata: ASWD_OS_NAME ("AswdOS"), ASWD_OS_VERSION ("v0.9.1"), banner, hello string, FAT_DEBUG_SERIAL toggle
    │   ├── palette.c — 16-color VGA palette as 32-bit RGB values (standard EGA/VGA colors)
    │   ├── power.c — ACPI shutdown and reboot via PM1a control port; text-mode and graphics-mode status messages; busy-wait fallback
    │   └── power.h — Declares power_shutdown() and power_reboot() (both noreturn)
    │
    ├── confirm/ — Text-mode confirmation prompts
    │   ├── confirm.c — Reads yes/no input ("ack"/"veto"/"y"/"n" and variants), normalizes lowercase, returns CONFIRM_ACK or CONFIRM_VETO, caches last result
    │   └── confirm.h — Defines confirm_result_t enum; declares confirm_prompt(), confirm_last_result(), confirm_last_was_ack()
    │
    ├── console/ — Central text output layer
    │   ├── console.c — Routes output to VGA or redirected winconsole; supports shell and script modes (script mode prefixes lines with block char); handles colors
    │   └── console.h — Declares console_init, mode set/get, color set, putc, write, writeln, colored variants, target redirection
    │
    ├── cpu/ — CPU-level subsystems: interrupts, exceptions, timers
    │   ├── bugcheck.c — Fatal error screen (BSOD-style): LEGACY (white-on-blue) and MODERN (white-on-red) styles; dumps exception frame registers in text or graphics mode; also provides panic()
    │   ├── bugcheck.h — Defines exception_frame_t (packed register dump), bugcheck_style_t enum; declares bugcheck_set_style(), bugcheck(), bugcheck_ex(), panic()
    │   ├── cpuid.c — Executes CPUID: leaf 0 for vendor string, leaves 0x80000002–0x80000004 for brand string; falls back to vendor if brand unsupported
    │   ├── cpuid.h — Declares cpuid_get_vendor() and cpuid_get_brand()
    │   ├── exception_stubs.asm — Macro-generated ISR stubs for CPU exceptions 0–14; pushes fake error code for exceptions without one, jumps to exception_common
    │   ├── exceptions.asm — Older/duplicate ISR stubs for exceptions 0–13 (same pattern as exception_stubs.asm)
    │   ├── exceptions.c — Thin dispatcher: exception_handler() calls bugcheck_ex() with the saved frame
    │   ├── exceptions.h — Declares exception_handler() taking exception_frame_t
    │   ├── idt.c — Builds Interrupt Descriptor Table (256 entries); maps ISR stubs (0–14) and IRQ stubs (0, 1, 4, 12, spurious); initializes PIC, loads IDT via LIDT
    │   ├── idt.h — Declares idt_init()
    │   ├── irq.asm — IRQ handler stubs: IRQ0→timer_tick, IRQ1→keyboard_irq_handler, IRQ4→serial_irq_handler, IRQ12→mouse_irq_handler; saves/restores registers
    │   ├── pic.c — Programmable Interrupt Controller: remaps PIC1 to 0x20, PIC2 to 0x28; masks all IRQs initially; provides send_eoi, set_mask, clear_mask
    │   ├── pic.h — Declares pic_init(), pic_send_eoi(), pic_set_mask(), pic_clear_mask()
    │   ├── ports.h — Inline assembly for I/O port access: outb/inb, outw/inw, outl/inl, io_wait (port 0x80), cli/sti/hlt CPU instructions
    │   ├── timer.c — PIT channel 0 driver: programs square-wave mode at configurable Hz (default 100); tick counter; auto-flushes vtconsole every 2 ticks; uptime calculation
    │   └── timer.h — Declares timer_init(hz), timer_tick(), timer_get_ticks(), timer_uptime_secs()
    │
    ├── crypto/ — Cryptographic primitives
    │   ├── aes.c — AES-128: S-box, inverse S-box, round constants; encrypt/decrypt 16-byte blocks; AES key unwrap (RFC 3394)
    │   ├── aes.h — Declares aes128_encrypt(), aes128_decrypt(), aes_key_unwrap()
    │   ├── sha1.c — SHA-1 hash; HMAC-SHA1 (single and vector variants); PBKDF2-SHA1 (key derivation); PRF-SHA1 (WPA pseudo-random function)
    │   └── sha1.h — Declares sha1(), hmac_sha1(), hmac_sha1_vector(), pbkdf2_sha1(), prf_sha1()
    │
    ├── diagnostics/ — Boot-time diagnostic tests
    │   ├── diagnostics.c — Test runner: smoke test (dispatches help/osinfo), temp write test (create/delete file via VFS), keyboard info test, force bugcheck test; reports pass/fail
    │   └── diagnostics.h — Defines diagnostic_test_mode_t enum (NONE, SMOKE, TEMP_WRITE, KEYBOARD, FORCE_BUGCHECK); declares diagnostics_run_test()
    │
    ├── drivers/ — Hardware drivers
    │   ├── ata.c — ATA/IDE PIO disk driver: reads/writes sectors via ports 0x1F0–0x1F7; LBA28 addressing; busy/DRQ wait loops; partition start offset
    │   ├── ata.h — Declares ata_init(), ata_available(), ata_partition_start(), ata_read_sectors(), ata_write_sectors()
    │   ├── bga.c — Bochs Graphics Adapter: detects via PCI (vendor 0x1234, device 0x1111); programs VBE DISPI registers (0x01CE/0x01CF) for resolution/BPP; gets framebuffer address and pitch
    │   ├── bga.h — Declares bga_detect() and bga_set_mode() with bga_mode_info_t output
    │   ├── disk.c — Unified disk abstraction: supports ATA backend and BIOS trampoline backend (real-mode INT 13h via trampoline for USB-booted systems); tracks last operation, retries, error state
    │   ├── disk.h — Declares disk_init(), disk_available(), disk_read_sectors(), disk_write_sectors(), disk_backend(), diagnostic getters
    │   ├── fat32.c — Full FAT32 driver: reads BPB, FAT cache, clusters, directory entries (8.3 + long names); CRUD ops; move/copy (file + recursive dir); mkdir/rmdir; soft-delete to RECYCLE.BIN; DOS timestamps; disk usage stats
    │   ├── fat32.h — Declares fat32_entry_t, fat32_info_t, fat32_usage_t; full API: list, find, read, write, delete, move, copy, mkdir, rmdir, usage, root cluster, bytes-per-sector, partition start
    │   ├── font.c — Standard 8×16 VGA ROM font bitmap (128 ASCII chars); wraps font_assets API for UI/mono font roles; glyph lookup and text measurement
    │   ├── font.h — Declares font metrics, glyph structures, font_role_t enum (UI, MONO); provides g_font_8x16 array; functions for metrics, glyph lookup, text measurement
    │   ├── gfx.c — Graphics subsystem: backbuffer-based rendering with dirty-rect optimization and partial present; multiple resolution modes (1366×768, 1280×800, etc.); init from multiboot framebuffer or BGA; draw pixels, rects, gradients, text, icons, overlay cursor; FPS stats
    │   ├── gfx.h — Declares gfx_mode_t, gfx_display_profile_t, gfx_frame_stats_t; full drawing API: init, dimensions, pixel/rect/gradient/text drawing, present/flip, FPS, overlay pixel/cursor
    │   ├── icon.c — Icon renderer: finds best-size variant from asset table; blends RGBA pixels with tint color onto backbuffer using alpha compositing
    │   ├── icon.h — Declares icon_asset_id_t enum (power, user, search, all app icons); declares icon_draw() and icon_best_variant_size()
    │   ├── i8042.c — PS/2 controller driver (i8042 chip): wait for input/output ready; read/write data and command ports; read/write controller config; enable/disable keyboard and mouse ports
    │   ├── i8042.h — Declares i8042 I/O primitives: wait_input_clear, wait_output_full, flush_output, write_command, write_data, read_data, read_typed, read_config, write_config, disable_ports, enable_first/second_port, write_device
    │   ├── keyboard.c — PS/2 keyboard driver: scancode-to-ASCII translation (US layout); ring buffer; handles shift, ctrl, E0 prefix (extended keys); supports USB HID keyboard injection and BIOS keyboard buffer fallback
    │   ├── keyboard.h — Declares special key codes (KEY_UP, KEY_DOWN, etc.), keyboard_init(), getchar, try_getchar, ps2_ready, push_char (USB injection), irq_handler
    │   ├── mouse.c — PS/2 mouse driver with IntelliMouse wheel probing (4-byte packets); ring buffer; absolute position tracking; buttons, wheel delta; accepts USB HID mouse events via push_usb_event; bounds-aware screen coordinates
    │   ├── mouse.h — Declares mouse_event_t (dx, dy, x, y, buttons, wheel, source); init, push_usb_event, set_bounds, irq_handler, poll, position/button getters, diagnostic counters
    │   ├── pci.c — PCI configuration space via ports 0x0CF8/0x0CFC; enumerates all buses/devices/functions; reads/writes 8/16/32-bit config; enables bus mastering; visitor pattern for device enumeration; USB controller detection
    │   ├── pci.h — Declares pci_device_t, PCI class constants; full read/write API, busmaster enable, enumerate/visitor, device count/lookup, has_usb_controller
    │   ├── serial.c — COM1 (0x3F8) serial driver: interrupt-driven RX ring buffer (256 bytes); blocking TX with ready polling; serial IRQ handler for incoming characters
    │   ├── serial.h — Declares serial_init(), serial_is_enabled(), serial_write(), serial_write_char(), serial_try_getchar(), serial_irq_handler()
    │   ├── speaker.c — PC speaker via PIT channel 2 (port 0x42/0x43) and port 0x61; generates beep tones at specified frequency/duration; boot chime with multi-note melody
    │   ├── speaker.h — Declares speaker_beep(freq_hz, ms) and speaker_boot_chime()
    │   ├── vbe.c — VESA BIOS Extensions: real-mode trampoline to call INT 0x10 VBE functions (get VBE info, get mode info, set video mode); result via shared memory block
    │   ├── vbe.h — Declares vbe_info_t and vbe_mode_info_t packed structures with all VBE 2.0+ fields
    │   ├── vga.c — Direct VGA text-mode driver (0xB8000): scrolling, cursor control, colored output, script-mode formatting; delegates cursor to vtconsole when in graphics mode
    │   ├── vga.h — Declares vga_init, clear, putchar, print, println, colored print, script line print, put_char_at, fill_row, scroll_region, cursor pos setters
    │   ├── vtconsole.c — Virtual text console: shadow buffer (80×25) rendered onto graphics backbuffer using font glyphs; per-cell color attributes; cursor; dirty tracking; auto-flush
    │   └── vtconsole.h — Declares VTC_COLS/ROWS (80×25); init, put_char_at, fill_row, clear, set_cursor, auto_flush, shadow buffer accessor
    │
    ├── editor/ — TUI text editor core
    │   ├── editor.c — Full-screen TUI editor: line-based buffer (512 lines × 160 chars); insert/overwrite modes; line numbers; cursor navigation; save/load via VFS; dirty tracking; workspace-aware paths; quit-with-confirmation
    │   └── editor.h — Declares editor_open(const char *name)
    │
    ├── explorer/ — Legacy TUI application hub
    │   ├── explorer.c — 80×25 text-mode layout: header, app tiles (terminal, files, editor, settings, etc.), power options (shutdown, reboot), keyboard navigation (arrows, Enter), status bar descriptions
    │   └── explorer.h — Declares explorer_run()
    │
    ├── fs/ — Filesystem abstraction
    │   ├── vfs.c — Virtual filesystem over FAT32: CWD tracking with path resolution; workspace (/ROOT) abstraction; recycle bin (soft-delete with rename fallback); ls, cd, cat, write, rm (with recycle), rm_force, mv, cp (recursive), mkdir, rmdir
    │   └── vfs.h — Declares vfs_init(), vfs_available(), cwd access, all file operations
    │
    ├── gui/ — GUI desktop, window manager, and all applications
    │   ├── gui.c — Core desktop shell: window management (up to 8 windows); taskbar with start menu; system tray (clock, network, USB status); desktop icon grid; window drag/resize; Z-ordering; title bar buttons (minimize/maximize/close); start menu search; app launching; power options; background themes
    │   ├── gui.h — Declares GUI_MAX_WINDOWS (8), GUI_TITLE_MAX (32); gui_rect_t; gui_shell_metrics_t; background theme and icon enums; gui_window_t, gui_app_t structs; window/app management API; shell metrics; launch functions
    │   ├── theme.c — Theme rendering: rounded rectangles with alpha; buttons (normal/hot/disabled); text boxes; checkboxes; scrollbars; list rows (alternating colors); card panels; toolbar; header; status bar; density-aware metrics
    │   ├── theme.h — Theme color constants (backgrounds, accents, text, borders, fields, status, selection); th_metrics_t (gap sizes, hit areas, button/field heights, font sizes); layout bucket enum; all draw functions for UI components
    │   ├── toast.c — Toast notification system: queue of up to 8 messages (300-tick lifetime); deduplication (extends lifetime of duplicates); auto-expiry; draws as overlay pills above the taskbar
    │   ├── toast.h — Declares toast_push(), toast_tick(), toast_draw()
    │   ├── context_menu.c — Right-click popup menu with icon support; hover highlighting; normal/danger styles; auto-dismiss on escape/right-click; pointer click handling
    │   ├── context_menu.h — Declares context_menu_item_t (label, icon, style, action callback); show, measure, dismiss, active, paint, handle_pointer API
    │   ├── winconsole.c — Windowed VGA-compatible console: pool of up to 8; each has cell/attribute buffer (192×64); cursor; scroll region; color state; dirty tracking; renders via font glyphs into graphics backbuffer
    │   ├── winconsole.h — Declares winconsole_t struct; full console API plus pool management (alloc/free)
    │   ├── dev_tools.c — Developer diagnostics panel: smoke test, temp write test, keyboard info, framebuffer stats (present pixels, FPS, coalesced frames), force bugcheck
    │   ├── dev_tools.h — Declares dev_tools_launch()
    │   ├── taskmgr.c — Task Manager: lists open windows, USB devices, user session info; allows closing windows; scrollable list with selection highlighting
    │   ├── taskmgr.h — Declares taskmgr_launch()
    │   ├── osinfo_gui.c — OS Information: version, build info, CPU brand, memory usage (from multiboot), storage usage (from FAT32), changelog entries, uptime, system details in a scrollable panel
    │   ├── osinfo_gui.h — Declares osinfo_gui_launch()
    │   ├── settings_gui.c — System Settings with 5 tabs: Display (desktop theme swatches), System (CPU, memory, uptime, test mode), Devices (mouse info, USB controllers), Users (create/switch users), Network (WiFi scan/connect, site allowlist)
    │   ├── settings_gui.h — Declares settings_gui_launch(), control_panel_launch(), control_panel_open_users()
    │   ├── shell_gui.c — Terminal emulator: embedded winconsole with shell prompt ("aswd> "); line editing; command history (16 entries); dispatches to shell command handler
    │   ├── shell_gui.h — Declares shell_gui_launch()
    │   ├── files_gui.c — File Manager: sidebar with navigation shortcuts; toolbar (back/forward/up); address bar; file/folder list with icon, name, size, date columns; double-click to open/enter; context menu actions
    │   ├── files_gui.h — Declares files_gui_launch()
    │   ├── editor_gui.c — GUI text editor: toolbar (new/open/save/run/docs); line-number gutter; cursor; syntax-aware editing; VFS file save/load; AX docs integration; dirty tracking; status bar with line/col
    │   ├── editor_gui.h — Declares editor_gui_launch() and editor_gui_open(path)
    │   ├── notes_gui.c — Notepad: toolbar (new/save); multi-line text editor with cursor; VFS save/load; name dialog for new notes; dirty tracking; status bar
    │   ├── notes_gui.h — Declares notes_gui_launch()
    │   ├── calc_gui.c — Calculator: 4×5 button grid (numbers, operators, clear, equals); dark display with expression preview and result; basic arithmetic
    │   ├── calc_gui.h — Declares calc_gui_launch()
    │   ├── browser_gui.c — HTTP browser: URL bar; back/forward/refresh; HTML text renderer (headings, paragraphs, links, code blocks, tables, rules); history stack (12 entries); status messages for errors/loading
    │   ├── browser_gui.h — Declares browser_gui_launch()
    │   ├── snake_gui.c — Snake game: 20×20 grid; alternating snake body colors; apple with glow/leaf; score HUD; speed increases with length; game-over detection
    │   ├── snake_gui.h — Declares snake_gui_launch()
    │   ├── appstore_gui.c — App Store: scrollable list of all installed apps with icons and launch buttons; permission prompt for admin-requiring actions
    │   ├── appstore_gui.h — Declares appstore_gui_launch()
    │   ├── axdocs_gui.c — AX language documentation viewer: scrollable window with built-in docs (quick start, variables, operators, control flow, functions, I/O, file ops, network, UI, sys commands)
    │   ├── axdocs_gui.h — Declares axdocs_gui_launch()
    │   ├── axstudio_gui.c — AX Studio IDE: visual UI builder for AX apps; toolbar (add/delete controls); WYSIWYG canvas; control palette (button/label/textbox/checkbox); properties panel (position, size, text); scene tabs; logic editor (event-to-action wiring); file save/load via VFS
    │   ├── axstudio_gui.h — Declares axstudio_gui_launch()
    │   ├── axapp_gui.c — AX App Runner: executes .ax "visual app" projects with multiple scenes; UI controls (buttons, labels, textboxes, checkboxes); event handling; state management (visibility, enabled, checked, textbox values); multi-instance pool
    │   ├── axapp_gui.h — Declares ax_ctrl_type_t enum, ax_ctrl_t, ax_scene_t, ax_project_t structures; declares axapp_gui_launch() and axapp_gui_load()
    │   ├── work_gui.c — Work180 office suite: document editor (rich text blocks: headings, paragraphs, lists, images); spreadsheet (26 cols × 50 rows with cell editing and formula bar); presentation builder (slides with text, shapes, images); home screen with card-based launcher; file save/load via VFS
    │   ├── work_gui.h — Declares work_mode_t (HOME, DOCS, SHEETS, SLIDES); work_gui_launch(), work_gui_open(mode, path)
    │   └── permission_gui.c — UAC-style permission prompt: darkened overlay; centered PIN entry dialog; validates against admin credentials; blocks until correct PIN or cancel
    │       └── permission_gui.h — Declares permission_prompt_run(action_desc) returning 1 if authorized
    │
    ├── input/ — Unified input abstraction
    │   ├── input.c — Polls serial, keyboard, and mouse; generates input_event_t (key or pointer events); readline with history (16 entries, arrow key navigation)
    │   └── input.h — Declares input_event_type_t, input_event_t (key/pointer union), input_key_event_t; declares getchar, try_getchar, try_get_event, readline, history_print
    │
    ├── lang/ — "Ax" interpreted language
    │   ├── lang.c — Global interpreter state: string pool, token array, AST node array, error flag; orchestrates lex → parse → eval pipeline; lang_run_file() loads from VFS, lang_run_str() executes from memory
    │   ├── lang.h — Public API: declares lang_run_file(path) and lang_run_str(src, len)
    │   ├── lang_priv.h — Internal shared types: string pool constants, token enum (keywords, operators, literals), lang_token_t, AST node types, lang_node_t, max limits
    │   ├── lexer.c — Ax tokenizer: identifiers (with dots), integers, strings, keywords (let, if, else, while, fn, return, print, sys, input, true, false), operators, newlines; line tracking for errors
    │   ├── parser.c — Recursive-descent parser: builds AST from tokens; expression parsing by precedence (primary, unary, multiplicative, additive, comparison, logical); statement parsing (let, if/else, while, fn, return, print, sys, input, expressions, blocks)
    │   └── eval.c — Evaluator: dynamically typed values (nil, int, string, bool); variable table with scoping; recursive AST evaluation (arithmetic, comparison, logic, if/else, while, function calls: print, sys, input, file.read/write/list, net.get, ui.alert, sys.exec); dotted identifiers
    │
    ├── lib/ — Minimal C library replacements (freestanding)
    │   ├── string.c — mem_set, mem_copy, str_len, str_cmp, str_ncmp, str_eq, str_copy, str_cat, split_args (shell-style arg splitting), u32_to_dec, u32_to_hex
    │   ├── string.h — Declares all memory and string utility functions
    │   └── ctype.h — Inline to_lower() for ASCII character conversion
    │
    ├── net/ — Full TCP/IP networking stack
    │   ├── net.c — Network orchestrator: PCI NIC detection; driver abstraction (RTL8139/RTL8168/e1000); packet receive loop; ARP/IP/TCP/UDP/DHCP dispatch; connection state tracking; NIC info reporting
    │   ├── net.h — Declares net_info_t (NIC name, MAC, IP, gateway, netmask, DNS, WiFi state); net_transport_t enum; net_connection_state_t enum; net_init(), net_poll(), net_get_info()
    │   ├── ethernet.c — Ethernet helpers: broadcast MAC fill, address comparison, address copy
    │   ├── ethernet.h — Declares eth_header_t, ETHERTYPE constants, ETH sizes, byte-swap helpers, eth_broadcast(), eth_addr_eq(), eth_addr_copy()
    │   ├── arp.c — ARP protocol: cache (8 entries); resolve IP-to-MAC (sends request if miss); process incoming requests/replies; cache update with LRU eviction
    │   ├── arp.h — Declares arp_resolve(), arp_rx(), arp_update()
    │   ├── ip.c — IPv4 layer: packet building with checksum; next-hop resolution (direct or via gateway); fragmentation awareness; dispatch to TCP/UDP/ICMP on receive
    │   ├── ip.h — Declares ip_header_t, IP_PROTO constants, ip_checksum(), ip_send(), ip_rx()
    │   ├── icmp.c — ICMP echo (ping): send echo request; receive echo reply; track sequence numbers for reply matching
    │   ├── icmp.h — Declares icmp_rx(), icmp_ping_send(), icmp_ping_reply()
    │   ├── udp.c — UDP layer: send/receive via IP; one-shot receive callback registration per port; checksum computation
    │   ├── udp.h — Declares udp_header_t, udp_send(), udp_rx(), udp_register(), udp_unregister()
    │   ├── tcp.c — TCP client: state machine (closed→syn_sent→established→fin_wait→close_wait); 3-way handshake; sequence/ack tracking; RX ring buffer (8 KB); retransmission with timeout; blocking send/recv; FIN/RST handling; checksum
    │   ├── tcp.h — Declares tcp_connect(), tcp_send_data(), tcp_recv_data(), tcp_close(), tcp_connected(), tcp_rx(), tcp_check_retransmit()
    │   ├── dhcp.c — DHCP client: state machine (idle→discover→offer→request→bound); sends discover/request via UDP; parses offers/ACKs; extracts IP/netmask/gateway/DNS from options
    │   ├── dhcp.h — Declares dhcp_start(), dhcp_poll(), dhcp_bound()
    │   ├── dns.c — DNS resolver: sends UDP queries; parses responses; caches up to 10 results with MRU promotion; blocks up to ~3 seconds for reply
    │   ├── dns.h — Declares dns_resolve(hostname, ip_out) returning 1 on success
    │   ├── http.c — HTTP client: URL parser; DNS resolution; TCP connection; GET/POST with headers; redirect following (301–308); response body extraction; CONNECT tunnel for proxies; site allowlist checking; error reporting
    │   ├── http.h — Declares http_error_t enum, http_get(), http_post(), http_proxy_connect_open(), http_last_error(), http_error_string()
    │   ├── site_allow.c — TLS site allowlist: load/save from TLSALLOW.CFG; host normalization (lowercase, dot-collapsing); add/remove/match; persistence via VFS
    │   ├── site_allow.h — Declares site_allow_result_t enum; init, count, host_at, add, remove, matches, enabled, persistent_available API
    │   ├── rtl8139.c — Realtek RTL8139 (PCI) Fast Ethernet: I/O port-based; TX buffer ring; RX circular buffer; interrupt handling; MAC read; link status
    │   ├── rtl8139.h — Declares rtl8139_init(), rtl8139_send(), rtl8139_recv(), rtl8139_get_mac()
    │   ├── rtl8168.c — Realtek RTL8168/8111 (PCIe) Gigabit Ethernet: MMIO-based; descriptor ring TX/RX; chip reset; PHY status; interrupt handling
    │   ├── rtl8168.h — Declares rtl8168_init(), rtl8168_send(), rtl8168_recv(), rtl8168_get_mac()
    │   ├── e1000.c — Intel e1000 (PCI) Gigabit Ethernet: MMIO-based; descriptor ring TX/RX; EEPROM read for MAC; PHY link status; interrupt handling
    │   ├── e1000.h — Declares e1000_init(), e1000_send(), e1000_recv(), e1000_get_mac()
    │   ├── wifi.c — WiFi manager: PCI/USB device detection by vendor/device ID tables (Intel 2200/3945, Atheros AR5xxx, Broadcom BCM43xx, Realtek RTL8187); backend dispatch; scan/connect/disconnect; saved network persistence (WIFI.CFG); state reporting
    │   ├── wifi.h — Declares wifi_family_t, wifi_security_t, wifi_state_t, wifi_adapter_info_t, wifi_network_t, wifi_saved_network_t, wifi_status_t; full WiFi API
    │   ├── mac80211.c — IEEE 802.11 MAC layer: probe request/response; beacon parsing; association request/response; authentication; RSN/WPA IE construction; management frame helpers
    │   ├── mac80211.h — Declares 802.11 frame type/subtype constants, IE tags, mac80211_network_t, frame build/parse API
    │   ├── wpa.c — WPA/WPA2-PSK: 4-way handshake state machine; PMK/PTK/GTK derivation via PBKDF2-SHA1; EAPOL-Key frame parsing/generation; MIC verification; CCMP encrypt/decrypt; replay counter management
    │   ├── wpa.h — Declares EAPOL key flags, wpa_state_t enum; wpa_init(), wpa_rx_eapol(), wpa_state(), wpa_ccmp_encrypt(), wpa_ccmp_decrypt()
    │   ├── wifi_ath5k.c — Atheros AR5xxx (PCI) WiFi: MMIO-based; descriptor ring TX/RX; reset; PHY calibration; channel selection; beacon/probe handling; mac80211 backend
    │   ├── wifi_ath5k.h — Declares ath5k_probe() and ath5k_backend_ops
    │   ├── wifi_ath9k.c — Atheros AR9xxx (PCIe) WiFi: MMIO-based; RTC power management; descriptor ring TX/RX; reset; beacon/probe handling; mac80211 backend
    │   ├── wifi_ath9k.h — Declares ath9k_probe() and ath9k_backend_ops
    │   ├── wifi_bcm43xx.c — Broadcom BCM43xx (PCI) WiFi: MMIO-based; DMA descriptor rings; MAC control; channel selection; scan/connect; mac80211 backend
    │   ├── wifi_intel.c — Intel PRO/Wireless 2200BG/3945ABG (PCI) WiFi: command/event queue; RXON configuration; scan; association; power management; mac80211 backend
    │   ├── wifi_intel.h — Declares intel_wifi_probe(), intel_wifi_init(), intel_wifi_backend_ops
    │   └── wifi_rtl8187.c — Realtek RTL8187 (USB) WiFi: USB control transfers for register access; bulk IN/OUT endpoints; reset; TX/RX config; scan/connect; mac80211 backend
    │       └── wifi_rtl8187.h — Declares rtl8187_probe() and rtl8187_backend_ops
    │
    ├── script/ — Legacy .aswd scripting system
    │   ├── script.c — .aswd interpreter: variable expansion ($VAR); set/echo/run/osinfo/sysinfo/confirm/iflast commands; line-by-line execution with conditional flow based on confirm results
    │   ├── script.h — Declares script_run(name)
    │   ├── vars.c — Variable store: up to 16 named variables (16-char name, 128-char value); set/get/reset with linear search
    │   ├── vars.h — Declares script_vars_reset(), script_vars_set(), script_vars_get()
    │   ├── builtin_scripts.c — Built-in script table: "demo" script (runs osinfo, confirm, sysinfo, set/echo variable demonstration)
    │   └── builtin_scripts.h — Declares builtin_script_t (name + text); get/count accessors
    │
    ├── settings/ — TUI settings/about screen
    │   ├── settings.c — 3-tab TUI screen (About, System, Diagnostics): OS version, CPU/memory info, smoke tests; keyboard navigation
    │   └── settings.h — Declares settings_run()
    │
    ├── shell/ — Interactive command-line shell
    │   ├── shell.c — REPL: reads input, splits args, dispatches to commands; prints storage summary (backend type, partition); manages normal vs raw mode
    │   ├── shell.h — Declares shell_mode_t (NORMAL, RAW), shell_run(), shell_get_mode(), shell_is_raw_mode()
    │   ├── commands.c — Command dispatcher with 30+ commands: help, osinfo, sysinfo, clear, echo, run, confirm, pwd, ls, df, cd, cat, write, rm, mkdir, cp, mv, find, grep, history, ln, inspect, test, compile, logs, power (shutdown/reboot), ping, http, ax (run .ax script), settings, vfstest, bootlog, and more
    │   ├── commands.h — Declares commands_init() and commands_dispatch(argc, argv) — returns 1 to exit shell
    │   ├── sysinfo.c — Prints CPU brand string (via CPUID) and RAM info (lower/upper KB from multiboot)
    │   └── sysinfo.h — Declares sysinfo_print()
    │
    ├── tui/ — Text-mode UI primitives
    │   ├── tui.c — Draw boxes, write colored text at positions, fill rectangles, header bar, status bar, shell frame with cwd display; all using VGA direct access
    │   └── tui.h — Declares tui_draw_box(), tui_write_at(), tui_fill_rect(), tui_header_bar(), tui_status_bar(), tui_shell_frame()
    │
    ├── usb/ — USB host stack
    │   ├── usb.c — USB subsystem manager: PCI enumeration of controllers (UHCI/OHCI/EHCI/xHCI); attach/poll dispatch; controller ranking (companions before EHCI); device tracking; status reporting (controller count, device count, HID keyboard/mouse counts)
    │   ├── usb.h — Declares usb_controller_kind_t, usb_controller_t, usb_device_t, usb_status_t; controller/device enumeration API, status getters, kind name helper
    │   ├── uhci.c — UHCI (USB 1.1) controller: I/O port-based; frame list setup; queue heads and transfer descriptors; port scanning/reset; control/interrupt/bulk transfers; HID mouse and keyboard polling; descriptor parsing
    │   ├── uhci.h — Declares uhci_attach() and uhci_poll()
    │   ├── ohci.c — OHCI (USB 1.1) controller: stub implementation (marks controller not ready, no transfer support yet)
    │   ├── ohci.h — Declares ohci_attach()
    │   ├── ehci.c — EHCI (USB 2.0 High-Speed) controller: capability/operational register access; port scanning/reset; periodic/asynchronous schedule; queue heads and transfer descriptors; control/bulk/interrupt transfer execution; HID keyboard enumeration on FS/LS companion ports
    │   ├── ehci.h — Declares ehci_attach() and ehci_poll()
    │   ├── xhci.c — xHCI (USB 3.0) controller: stub implementation (marks controller not ready, no transfer support yet)
    │   ├── xhci.h — Declares xhci_attach()
    │   ├── hid.c — USB HID parser: boot-protocol mouse report parsing (buttons, dx, dy, wheel delta)
    │   ├── hid.h — Declares usb_hid_init(), usb_hid_parse_boot_mouse(), usb_hid_boot_keyboard_process()
    │   └── hid_kbd.c — USB HID boot keyboard processor: usage-to-ASCII translation tables (normal and shifted); modifier detection (shift, ctrl); key press/release diffing; injects into PS/2 keyboard buffer
    │
    ├── usbboot/ — Custom USB bootloader chain (16-bit real mode)
    │   ├── mbr.asm — Master Boot Record (ORG 0x7C00): reads VBR from partition start using INT 13h LBA extended read (DAP); prints "MBR" marker; jumps to 0x0000:0x0600
    │   ├── vbr.asm — Volume Boot Record (ORG 0x0600): FAT32 BPB header; reads stage2 from disk using INT 13h; jumps to stage2 at 0x8000
    │   ├── stage2.asm — Stage 2 bootloader (ORG 0x8000): enables A20; reads kernel from FAT32 in 8-sector chunks to 0x100000 (1 MiB); saves boot drive/partition LBA/PIC masks at 0x0500–0x0509; sets up GDT for protected mode transition; jumps to 32-bit PM at kernel entry
    │   ├── trampoline.asm — Real-mode trampoline for INT 13h from protected mode: saves ESP/IDT; switches to 16-bit PM; clears PE bit for real mode; calls INT 13h (read/write/EDD probe/reset); restores PIC masks; returns to 32-bit PM
    │   └── trampoline/ — (subdirectory) Additional trampoline-related build artifacts
    │
    └── users/ — User management
        ├── users.c — Up to 8 users; admin designation; current user tracking; session persistence (USERS/ACTIVE.USR/ADMIN.USR files on disk, currently disabled for stability); name normalization (uppercase alphanumeric); create/switch/logout; home path resolution
        └── users.h — Declares users_init(), users_current(), users_count(), users_name_at(), users_has_active(), users_needs_setup(), users_current_is_admin(), users_create(), users_switch(), users_create_next(), users_logout(), users_home_path()
```

---

## Boot Process

### Two boot paths

**Path 1: ISO (GRUB multiboot)**
```
GRUB loads kernel.elf → boot.asm (_start) → sets up stack, saves PIC masks, flushes GDT → calls kernel_main(magic, ebx)
```

**Path 2: USB (custom BIOS bootloader)**
```
BIOS loads MBR (sector 0) → MBR reads VBR (sector 2048) → VBR reads stage2 (sector 2049+) →
stage2 enables A20, reads kernel from FAT32 to 1 MiB, sets up GDT, enters protected mode →
bios.asm (bios_start) → clears BSS, sets up stack, flushes GDT, calls kernel_main(0, 0)
```

### kernel_main() flow

1. `idt_init()` — set up interrupt descriptor table
2. `serial_init()` — initialize COM1 serial output
3. `multiboot_init()` — parse GRUB memory/framebuffer info (ISO path only)
4. `gfx_init()` — initialize graphics (multiboot framebuffer or BGA)
5. `vga_init()` — initialize VGA text mode
6. `console_init()` — initialize text console
7. `boot_launcher_run()` — show boot splash, let user pick target (GUI/TUI/shell/FS-lab)
8. If **FS-lab**: minimal boot (storage + timer + keyboard → raw shell)
9. If **normal**: full boot (storage → VFS → users → timer → keyboard → PCI → USB → network → auth screen)
10. After auth: GUI main loop (if GUI target) or explorer/shell (if TUI target)

---

## GUI System

### Window Management
- Up to 8 concurrent windows (`GUI_MAX_WINDOWS`)
- Each window has: title, frame rect, content rect, focus state, drag/resize state, minimize/maximize state, restore frame, event callbacks (on_paint, on_tick, on_key, on_mouse, on_close)
- Z-ordering: focused window is on top
- Desktop icon grid with configurable metrics

### Theme System (`src/gui/theme.h`)
- Design tokens for colors, spacing, typography
- Layout buckets: compact, comfortable, wide (auto-selected by screen width)
- UI component draw functions: cards, dialogs, panels, buttons, fields, checkboxes, scrollbars, list rows, tabs, toolbars, status bars, page headers, info strips, empty states, auth cards, sidebars, badges, badges, table headers
- Animation helpers: ease, progress, lerp (int and color)

### Adding a New GUI App
1. Create `src/gui/myapp_gui.c` and `src/gui/myapp_gui.h`
2. Implement a `void myapp_gui_launch(void)` function that calls `gui_window_create()` and sets up callbacks
3. Add the app to the app registry in `src/gui/gui.c` (the `gui_app_t` table)
4. Add `myapp_gui.c` to `C_SOURCES` in the `Makefile`
5. Add an icon variant to `src/assets/icon_assets.c` (or use an existing one)

### GUI Applications (15 apps)

| App | File | Description |
|---|---|---|
| Terminal | `shell_gui.c` | Embedded shell with line editing and 16-entry history |
| Files | `files_gui.c` | File manager with sidebar, address bar, columns |
| Editor | `editor_gui.c` | Text editor with toolbar, line numbers, syntax editing |
| OS Info | `osinfo_gui.c` | System info: version, CPU, RAM, storage, changelog, uptime |
| Settings | `settings_gui.c` | 5-tab settings: Display, System, Devices, Users, Network |
| Task Manager | `taskmgr.c` | Lists windows, USB devices, sessions; can close windows |
| Snake | `snake_gui.c` | Snake game on 20×20 grid with scoring |
| Notes | `notes_gui.c` | Simple notepad with save/load |
| App Store | `appstore_gui.c` | Browse and launch installed apps |
| Calculator | `calc_gui.c` | Basic arithmetic with expression preview |
| Browser | `browser_gui.c` | HTTP browser with HTML rendering, history stack |
| AX Docs | `axdocs_gui.c` | Ax language documentation viewer |
| AX Studio | `axstudio_gui.c` | Visual IDE for building AX apps |
| AX App Runner | `axapp_gui.c` | Runs .ax visual app projects |
| Work180 | `work_gui.c` | Office suite: documents, spreadsheets, presentations |

---

## Networking Stack

### Layers
```
Application:  HTTP client
Transport:    TCP (state machine, 8KB RX buffer, retransmission) / UDP (one-shot port callbacks)
Network:      IPv4 (checksum, next-hop, fragmentation awareness) / ICMP (ping)
Link:         ARP (8-entry cache, LRU) / Ethernet
Physical:     RTL8139 (PCI Fast Ethernet) / RTL8168 (PCIe Gigabit) / e1000 (PCI Gigabit)
```

### WiFi Support
- **Manager** (`wifi.c`): detects WiFi adapters by PCI/USB vendor/device IDs, dispatches to backends, manages saved networks (WIFI.CFG)
- **MAC layer** (`mac80211.c`): probe, beacon, association, authentication, RSN/WPA IE construction
- **Security** (`wpa.c`): WPA/WPA2-PSK 4-way handshake, PMK/PTK/GTK via PBKDF2-SHA1, CCMP encrypt/decrypt, EAPOL-Key handling

### WiFi Backends

| Backend | Hardware | Bus | Status |
|---|---|---|---|
| `wifi_intel.c` | Intel PRO/Wireless 2200BG / 3945ABG | PCI | Implemented |
| `wifi_ath5k.c` | Atheros AR5xxx | PCI | Implemented |
| `wifi_ath9k.c` | Atheros AR9xxx | PCIe | Implemented |
| `wifi_bcm43xx.c` | Broadcom BCM43xx | PCI | Implemented |
| `wifi_rtl8187.c` | Realtek RTL8187 | USB | Implemented |

### Site Allowlist
- `site_allow.c` manages a TLS/HTTPS site allowlist stored in `TLSALLOW.CFG` on disk
- Host normalization: lowercase, dot-collapsing
- Used by the HTTP client to block or allow specific domains

---

## USB Subsystem

### Host Controllers

| Controller | USB Version | Status |
|---|---|---|
| UHCI (`uhci.c`) | USB 1.1 | Fully implemented: frame list, queue heads, transfer descriptors, HID keyboard/mouse polling |
| OHCI (`ohci.c`) | USB 1.1 | Stub: detected but no transfer support yet |
| EHCI (`ehci.c`) | USB 2.0 High-Speed | Implemented: port scanning, schedules, transfers, HID keyboard on companion ports |
| xHCI (`xhci.c`) | USB 3.0 | Stub: detected but no transfer support yet |

### HID Subsystem
- `hid.c` — parses boot-protocol mouse reports (buttons, dx, dy, wheel)
- `hid_kbd.c` — processes boot-protocol keyboard reports; translates usage codes to ASCII; injects into PS/2 keyboard buffer
- USB keyboard and mouse events are fed into the same input pipeline as PS/2 devices

### USB Initialization
- PCI enumeration finds USB controllers
- Controllers ranked: UHCI/OHCI (companions) before EHCI
- Each controller is attached and polled in the main loop
- Status reporting: controller count, device count, HID keyboard/mouse counts

---

## Scripting & Language

### Legacy .aswd Scripts
- One command per line
- `#` for comments
- `set var=value` / `$var` substitution
- Commands: `echo`, `run`, `osinfo`, `sysinfo`, `confirm`, `iflast` (conditional on last confirm result)
- Built-in script: `run demo`
- Variables: up to 16 named variables (16-char name, 128-char value)
- Stored in `src/script/`

### Ax Interpreted Language
- File extension: `.ax`
- Run with: `ax <file.ax>` in the shell
- Pipeline: lexer → parser → evaluator
- **Lexer** (`lexer.c`): identifiers (with dots), integers, strings, keywords (`let`, `if`, `else`, `while`, `fn`, `return`, `print`, `sys`, `input`, `true`, `false`), operators, newlines
- **Parser** (`parser.c`): recursive-descent, precedence-based expression parsing, AST generation
- **Evaluator** (`eval.c`): dynamically typed values (nil, int, string, bool), variable scoping, built-in functions (`print`, `sys`, `input`, `file.read`, `file.write`, `file.list`, `net.get`, `ui.alert`, `sys.exec`)
- Documentation: `src/AX_docs.md` and viewed in-app via AX Docs app

---

## Shell Commands

| Command | Description |
|---|---|
| `help` | List all commands |
| `osinfo` | Show OS version and build info |
| `sysinfo` | Show CPU brand and RAM info |
| `clear` | Clear screen |
| `echo <text>` | Print text |
| `confirm <message>` | Ask for confirmation |
| `run <script>` | Run a built-in script |
| `pwd` | Print working directory |
| `ls` | List directory contents |
| `df` | Show disk usage |
| `cd <path>` | Change directory |
| `cat <file>` | Print file contents |
| `write <file>` | Write to a file |
| `rm <file>` | Remove file (soft-delete to RECYCLE.BIN) |
| `mkdir <dir>` | Create directory |
| `cp <src> <dst>` | Copy file or directory (recursive) |
| `mv <src> <dst>` | Move file or directory |
| `find <pattern>` | Search for files |
| `grep <pattern>` | Search file contents |
| `history` | Show command history |
| `ln <target> <link>` | Create link |
| `inspect` | Inspect filesystem details |
| `test` | Run diagnostics |
| `compile` | Compile a script |
| `logs` | Show boot logs |
| `power` | Shutdown or reboot |
| `ping <host>` | Send ICMP echo request |
| `http <url>` | Fetch URL via HTTP |
| `ax <file>` | Run an Ax script |
| `settings` | Open settings |
| `vfstest` | Test VFS operations |
| `bootlog` | Dump boot log |

### Confirmation Keywords
- **YES**: `acknowledge`, `ack`, `yes`, `y`
- **NO**: `veto`, `v`, `no`, `n`
- Invalid input is treated as veto (prints red error)

---

## Key Architectural Patterns

### No Heap
- All buffers are statically allocated at compile time
- No `malloc`, no `free`
- Maximum limits are hardcoded (e.g., `GUI_MAX_WINDOWS = 8`, DNS cache = 10, ARP cache = 8)

### Dual Text Output
- **VGA text mode** (`0xB8000`): direct hardware text buffer, used during boot and in shell/TUI mode
- **Virtual console** (`vtconsole.c`): 80×25 shadow buffer rendered onto the graphics backbuffer using font glyphs, used when GUI is running
- **Console redirection** (`console.c`): output can be routed to VGA or to a winconsole (GUI terminal)

### Serial Debugging
- COM1 (0x3F8) is initialized early in `kernel_main`
- All `serial_write()` calls mirror output to the serial port
- Use `run-serial` or `run-usb-debug` to see serial output on host

### Boot Logging
- `boot_log.c` maintains a ring buffer (24 lines × 72 chars) of boot progress messages
- Messages added via `boot_log_line("boot: ...")`
- Dumped to VGA console on request; skipped in quiet mode

### FAT32 Soft Delete
- `rm` moves files to `RECYCLE.BIN` directory instead of permanent deletion
- `rm_force` bypasses the recycle bin
- Rename fallback if `RECYCLE.BIN` name conflicts

---

## Development Workflow

### Debugging
- **Serial output**: `build.bat -run-serial` — shows kernel serial output on host terminal
- **USB debug**: `make run-usb-debug` — captures debugcon output to `dbg.log`
- **QEMU direct**: `qemu-system-i386 -cdrom dist/aswd.iso -serial stdio`
- **FAT tracing**: compile with `-DFAT_DEBUG_SERIAL=1` for verbose FAT flush/write tracing on serial

### Testing
- **Smoke test**: dispatches `help` and `osinfo` commands to verify shell works
- **Temp write test**: creates and deletes a random-named file via VFS
- **Keyboard test**: displays keyboard driver info
- **Force bugcheck**: triggers a bugcheck screen for visual verification
- Run with: `build.bat -run-serial` then `test smoke` / `test temp` / `test keyboard` / `test bugcheck` in the shell

### Flashing to Real USB
- `build.bat -flash` — requires Administrator, lists USB disks, prompts for confirmation, writes image
- Or use Rufus with `dist/aswd-usb.img` in **DD Image mode**
- UEFI machines must enable **Legacy Boot / CSM** in firmware

---

## Release Workflow

- After every completed feature, fix, or UI update, write a short plain-language summary of what changed.
- Append that summary to the built-in changelog in `src/common/changelog.c`, because the `OS Info` app reads its release notes from that file.
- Keep the newest changelog entry aligned with `src/common/config.h` when a version bump happens.
- Mention the changelog update in the final handoff so the release trail stays visible.
