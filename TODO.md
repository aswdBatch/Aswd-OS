# TODO - Aswd OS

## Phase 1: Boot + VGA output

- [x] Multiboot header + GRUB boot
- [x] Direct BIOS USB FAT32 boot image
- [x] 16 KB stack
- [x] VGA text mode (80x25) output
- [x] Boot banner: `Aswd OS v0.test` + `Hello :D`

## Phase 2: Keyboard input + shell

- [x] PIC remap + IDT setup
- [x] IRQ1 keyboard handler
- [x] Scancode to ASCII with Shift support
- [x] Ring buffer (256) + blocking getchar
- [x] COM1 serial mirror + IRQ4 input
- [x] Shell prompt `aswd>` with backspace + enter

## Phase 3: Commands

- [x] Command registry + dispatcher
- [x] `help`
- [x] `osinfo`
- [x] `sysinfo`
- [x] `clear`
- [x] `echo`
- [x] `run <script>`
- [x] `confirm <message>`

## Phase 4: Script system

- [x] `.aswd` runner
- [x] One command per line + `#` comments
- [x] `confirm <message>` and stored last result
- [x] `iflast ack`
- [x] `set var=value` + `$var` substitution
- [x] Script output prefixes every line with a block marker

## Phase 5: Enhancements

- [x] Filesystem support and reading `.aswd` from disk
- [x] Shell history + basic line editing (16-entry ring buffer, left/right cursor, insert)
- [ ] Timer IRQ0 + timekeeping
- [ ] More system info
- [ ] Text editor rewrite
- [ ] UEFI boot target for UEFI-only PCs
- [ ] Framebuffer console for UEFI GOP
- [ ] USB HID keyboard support
