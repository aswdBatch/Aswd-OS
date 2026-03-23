CC := i686-elf-gcc
AS := nasm
OBJCOPY := i686-elf-objcopy
POWERSHELL := powershell
OBJDIR := obj

CFLAGS := -std=c99 -O2 -ffreestanding -Wall -Wextra -fno-pic -fno-pie -fno-stack-protector -fno-builtin -Isrc
LDFLAGS := -ffreestanding -nostdlib -fno-pie

C_SOURCES := \
	src/kernel.c \
	src/auth/auth_store.c \
	src/auth/auth_gui.c \
	src/boot/bootui.c \
	src/boot/multiboot.c \
	src/diagnostics/diagnostics.c \
	src/explorer/explorer.c \
	src/console/console.c \
	src/confirm/confirm.c \
	src/cpu/cpuid.c \
	src/cpu/timer.c \
	src/cpu/bugcheck.c \
	src/cpu/exceptions.c \
	src/cpu/idt.c \
	src/cpu/pic.c \
	src/drivers/ata.c \
	src/drivers/disk.c \
	src/drivers/i8042.c \
	src/drivers/keyboard.c \
	src/drivers/fat32.c \
	src/drivers/serial.c \
	src/drivers/vga.c \
	src/drivers/vbe.c \
	src/drivers/bga.c \
	src/drivers/gfx.c \
	src/drivers/font.c \
	src/drivers/vtconsole.c \
	src/drivers/mouse.c \
	src/drivers/pci.c \
	src/common/changelog.c \
	src/common/palette.c \
	src/gui/appstore_gui.c \
	src/gui/axdocs_gui.c \
	src/gui/context_menu.c \
	src/gui/gui.c \
	src/gui/editor_gui.c \
	src/gui/files_gui.c \
	src/gui/dev_tools.c \
	src/gui/notes_gui.c \
	src/gui/osinfo_gui.c \
	src/gui/settings_gui.c \
	src/gui/shell_gui.c \
	src/gui/snake_gui.c \
	src/gui/theme.c \
	src/gui/taskmgr.c \
	src/gui/winconsole.c \
	src/editor/editor.c \
	src/fs/vfs.c \
	src/input/input.c \
	src/lib/string.c \
	src/settings/settings.c \
	src/script/builtin_scripts.c \
	src/script/script.c \
	src/script/vars.c \
	src/shell/commands.c \
	src/shell/shell.c \
	src/shell/sysinfo.c \
	src/tui/tui.c \
	src/users/users.c \
	src/usb/usb.c \
	src/usb/uhci.c \
	src/usb/ohci.c \
	src/usb/ehci.c \
	src/usb/xhci.c \
	src/usb/hid.c \
	src/drivers/speaker.c \
	src/gui/calc_gui.c \
	src/gui/browser_gui.c \
	src/net/net.c \
	src/net/ethernet.c \
	src/net/rtl8139.c \
	src/net/rtl8168.c \
	src/net/e1000.c \
	src/net/arp.c \
	src/net/ip.c \
	src/net/icmp.c \
	src/net/udp.c \
	src/net/dhcp.c \
	src/net/dns.c \
	src/net/tcp.c \
	src/net/http.c \
	src/lang/lang.c \
	src/lang/lexer.c \
	src/lang/parser.c \
	src/lang/eval.c \
	src/gui/axstudio_gui.c \
	src/gui/axapp_gui.c

COMMON_ASM_SOURCES := \
	src/boot/gdt.asm \
	src/cpu/exception_stubs.asm \
	src/cpu/irq.asm

GRUB_ASM_SOURCES := \
	src/boot/boot.asm

BIOS_ASM_SOURCES := \
	src/boot/bios.asm

OBJ_COMMON := $(C_SOURCES:src/%.c=$(OBJDIR)/%.o) $(COMMON_ASM_SOURCES:src/%.asm=$(OBJDIR)/%.o)
OBJ_GRUB := $(GRUB_ASM_SOURCES:src/%.asm=$(OBJDIR)/%.o)
OBJ_BIOS := $(BIOS_ASM_SOURCES:src/%.asm=$(OBJDIR)/%.o)
OBJ_TRAMPOLINE := $(OBJDIR)/usbboot/trampoline.o

.PHONY: all iso run run-serial run-vga run-usb run-usb-debug clean usb usb-img

all: iso

$(OBJDIR)/%.o: src/%.c
	@cmd /c "if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: src/%.asm
	@cmd /c "if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))"
	$(AS) -f elf32 $< -o $@

$(OBJDIR)/usbboot/trampoline.bin: src/usbboot/trampoline.asm
	@cmd /c "if not exist $(subst /,\,$(@D)) mkdir $(subst /,\,$(@D))"
	$(AS) -f bin $< -o $@

$(OBJDIR)/usbboot/trampoline.o: $(OBJDIR)/usbboot/trampoline.bin
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

dist/kernel.elf: $(OBJ_COMMON) $(OBJ_TRAMPOLINE) $(OBJ_GRUB) linker.ld
	@cmd /c "if not exist dist mkdir dist"
	$(CC) -T linker.ld -o $@ $(LDFLAGS) $(OBJ_COMMON) $(OBJ_TRAMPOLINE) $(OBJ_GRUB) -lgcc

dist/kernel-bios.elf: $(OBJ_COMMON) $(OBJ_TRAMPOLINE) $(OBJ_BIOS) linker.ld
	@cmd /c "if not exist dist mkdir dist"
	$(CC) -T linker.ld -Wl,-e,bios_start -o $@ $(LDFLAGS) $(OBJ_BIOS) $(OBJ_COMMON) $(OBJ_TRAMPOLINE) -lgcc

dist/kernel-bios.bin: dist/kernel-bios.elf
	$(OBJCOPY) -O binary $< $@

iso: dist/aswd.iso

dist/aswd.iso: dist/kernel.elf grub.cfg
	@rm -rf dist/isodir
	@cmd /c "if not exist dist mkdir dist"
	@cmd /c "if not exist dist\\isodir\\boot\\grub mkdir dist\\isodir\\boot\\grub"
	@cp dist/kernel.elf dist/isodir/boot/kernel.elf
	@cp grub.cfg dist/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ dist/isodir >/dev/null 2>&1

usb: dist/aswd-usb.img

usb-img: usb

dist/aswd-usb.img: dist/kernel-bios.bin scripts/make_usb_image.ps1 src/usbboot/mbr.asm src/usbboot/vbr.asm src/usbboot/stage2.asm
	$(POWERSHELL) -ExecutionPolicy Bypass -File scripts/make_usb_image.ps1 -KernelBin dist/kernel-bios.bin -Output $@

run: dist/aswd.iso
	qemu-system-i386 -cdrom dist/aswd.iso -m 64M -vga std \
	    -netdev user,id=n0 -device rtl8139,netdev=n0

run-serial: dist/aswd.iso
	qemu-system-i386 -cdrom dist/aswd.iso -m 64M -vga std -serial stdio \
	    -netdev user,id=n0 -device rtl8139,netdev=n0

run-vga: dist/aswd.iso
	qemu-system-i386 -cdrom dist/aswd.iso -m 64M -vga std -serial stdio \
	    -netdev user,id=n0 -device rtl8139,netdev=n0

run-usb: dist/aswd-usb.img
	qemu-system-i386 -drive file=dist/aswd-usb.img,format=raw,if=ide -m 64M -serial stdio

run-usb-debug: dist/aswd-usb.img
	qemu-system-i386 -drive file=dist/aswd-usb.img,format=raw,if=ide \
	    -m 64M -serial stdio -debugcon file:dbg.log

clean:
	@cmd /c "if exist $(OBJDIR) rmdir /s /q $(OBJDIR) & if exist dist rmdir /s /q dist"
