; INT 13h real-mode trampoline
; Assembled as flat binary (-f bin), ORG 0x8000
; Called from 32-bit protected mode as a C function pointer.
;
; Parameter block at 0x1400:
;   +0  uint8_t  drive
;   +1  uint8_t  op      (0=read, 1=write, 6=EDD probe, 7=disk reset)
;   +2  uint16_t count   (sectors)
;   +4  uint32_t lba
;   +8  uint32_t (unused - buf always 0x10000)
;   +12 uint32_t result  (0=ok, else AH error)
;   +16 uint32_t saved_esp (internal use)
;   0x1420+ unused by the current minimal path
;
; GDT descriptor saved at 0x1460 by disk_init() before copying trampoline.
; Bounce buffer at 0x10000 (matches the stage2 loader and must be < 1 MB).
; Stage2 also leaves the original BIOS PIC masks at 0x0508/0x0509 so BIOS
; helper calls can restore the same real-mode mask state on demand.

BITS 32
ORG 0x8000

trampoline_entry:
    ; Save ESP so we can restore it when returning to 32-bit PM
    mov [0x1410], esp
    sidt [0x1440]
    pusha
    cli
    mov al, 'A'
    out 0xE9, al

    ; Far jump to 16-bit code selector (0x18) to enter 16-bit protected mode
    jmp 0x18:pm16_entry

BITS 16
pm16_entry:
    mov al, 'B'
    out 0xE9, al
    ; Load data segment registers with flat 32-bit data descriptor (base=0)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Clear PE to leave protected mode
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax

    ; Far jump flushes CS pipeline and completes transition to real mode
    jmp 0x0000:rm_code

rm_code:
    mov al, 'C'
    out 0xE9, al
    ; We are now in real mode.  Set up segments and stack.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7B00
    lidt [rm_ivt_descriptor]

    ; Match the bootloader path: make sure A20 is on before BIOS disk I/O.
    call enable_a20

    ; Build DAP at 0x1480
    mov al, 'D'
    out 0xE9, al
    mov byte [0x1480], 0x10       ; packet size = 16
    mov byte [0x1481], 0x00       ; reserved
    mov ax, [0x1402]              ; sector count
    mov [0x1482], ax
    mov word [0x1484], 0x0000     ; buffer offset
    mov word [0x1486], 0x1000     ; buffer segment
    mov eax, [0x1404]             ; LBA low 32 bits
    mov [0x1488], eax
    mov dword [0x148C], 0         ; LBA high 32 bits

    ; Call INT 13h
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    mov dl, [0x1400]              ; drive number
    mov si, 0x1480                ; DS:SI -> DAP
    cmp byte [0x1401], 0          ; op: 0=read, 1=write, 2=keyboard
    je .do_read
    cmp byte [0x1401], 1
    je .do_write
    cmp byte [0x1401], 2
    je .do_keyboard
    cmp byte [0x1401], 3
    je .do_vbe_info
    cmp byte [0x1401], 4
    je .do_vbe_mode_info
    cmp byte [0x1401], 5
    je .do_vbe_set_mode
    cmp byte [0x1401], 6
    je .do_edd_probe
    cmp byte [0x1401], 7
    je .do_disk_reset
    jmp .fail

.do_read:
    call pic_to_bios_disk_state
    mov ah, 0x42
    mov al, 'R'
    out 0xE9, al
    sti
    int 0x13
    cli
    jnc .do_read_ok
    jmp .fail
.do_read_ok:
    jmp .success

.do_write:
    call pic_to_bios_disk_state
    mov al, 'W'
    out 0xE9, al                  ; debug BEFORE setting AL for INT 13h
    mov ah, 0x43
    xor al, al                    ; AL=0: no verify
    sti
    int 0x13
    cli
    jnc .do_write_ok
    jmp .fail
.do_write_ok:
    jmp .success

.do_edd_probe:
    mov al, 'P'
    out 0xE9, al
    call pic_to_bios_disk_state
    mov bx, 0x55AA
    mov ah, 0x41
    sti
    int 0x13
    cli
    jc .fail
    cmp bx, 0xAA55
    jne .edd_unsupported
    test cx, 0x0001
    jz .edd_unsupported
    jmp .success

.edd_unsupported:
    mov ah, 0x01
    jmp .fail

.do_disk_reset:
    mov al, 'Z'
    out 0xE9, al
    call pic_to_bios_disk_state
    xor ah, ah
    sti
    int 0x13
    cli
    jnc .success
    jmp .fail

.do_keyboard:
    mov al, 'K'
    out 0xE9, al
    call pic_to_bios_state
    mov ah, 0x01
    sti
    int 0x16
    cli
    jz .kbd_none
    mov ah, 0x00
    sti
    int 0x16
    cli
    mov [0x1408], ax
    mov dword [0x140C], 1
    jmp rm_return_to_pm

.kbd_none:
    mov dword [0x1408], 0
    mov dword [0x140C], 0
    jmp rm_return_to_pm

.do_vbe_info:               ; op=3: INT 10h AX=4F00h (get VBE controller info)
    mov al, 'V'
    out 0xE9, al
    call pic_to_bios_state
    push es
    mov bx, 0x1000
    mov es, bx
    xor di, di              ; ES:DI = 0x1000:0x0000 = phys 0x10000
    mov ax, 0x4F00
    sti
    int 0x10
    cli
    pop es
    mov al, '1'
    out 0xE9, al
    movzx ebx, ax
    mov [0x140C], ebx
    jmp rm_return_to_pm

.do_vbe_mode_info:          ; op=4: INT 10h AX=4F01h (get mode info)
    mov al, 'M'
    out 0xE9, al
    call pic_to_bios_state
    mov cx, [0x1402]        ; mode number from param block +2
    push es
    mov bx, 0x1000
    mov es, bx
    xor di, di
    mov ax, 0x4F01
    sti
    int 0x10
    cli
    pop es
    mov al, '2'
    out 0xE9, al
    movzx ebx, ax
    mov [0x140C], ebx
    jmp rm_return_to_pm

.do_vbe_set_mode:           ; op=5: INT 10h AX=4F02h (set VBE mode)
    mov al, 'S'
    out 0xE9, al
    call pic_to_bios_state
    mov bx, [0x1402]        ; mode number (caller sets bit 14 for LFB)
    mov ax, 0x4F02
    sti
    int 0x10
    cli
    mov al, '3'
    out 0xE9, al
    movzx ebx, ax
    mov [0x140C], ebx
    jmp rm_return_to_pm

.success:
    cli
    mov dword [0x140C], 0
    mov al, 'E'
    out 0xE9, al
    jmp rm_return_to_pm

.fail:
    cli
    xor ebx, ebx
    mov bl, ah
    mov [0x140C], ebx
    mov al, 'E'
    out 0xE9, al

rm_return_to_pm:
    call enable_a20               ; re-enable A20 in case INT 13h disabled it
    call pic_to_kernel_state      ; remap PIC back to kernel vectors (0x20/0x28)
    o32 lgdt [0x1460]             ; reload GDT (32-bit base)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump back to 32-bit protected mode (needs 32-bit far jump encoding)
    jmp dword 0x08:pm32_return

BITS 32
pm32_return:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    lidt [0x1440]
    ; Restore stack to the saved pusha frame, then pop the registers.
    ; [0x1410] holds ESP from before pusha, so we back up 32 bytes.
    mov esp, [0x1410]
    sub esp, 32
    popa
    ret

BITS 16
enable_a20:
    ; Keep this path BIOS-free. The trampoline may be entered repeatedly for
    ; disk, keyboard, and VBE calls, and some BIOSes/QEMU builds do not like
    ; repeated INT 15h A20 toggles once the kernel is already running.
    in al, 0x92
    and al, 0xFE
    or al, 0x02
    out 0x92, al
.done:
    ret

install_timeout_irq0:
    cli
    xor ax, ax
    mov ds, ax
    mov ax, [0x0020]
    mov [0x1426], ax
    mov ax, [0x0022]
    mov [0x1428], ax
    mov word [0x0020], rm_timeout_irq0
    mov word [0x0022], 0x0000
    mov word [0x1422], 0
    ; Force IRQ0 open so the watchdog can fire even if the BIOS left it masked.
    in al, 0x21
    and al, 0xFE
    out 0x21, al
    ret

install_debug_watchdog:
    ; Single-step watchdog: this still fires if the BIOS masks IRQ0.
    cli
    xor ax, ax
    mov ds, ax
    mov ax, [0x0004]
    mov [0x142A], ax
    mov ax, [0x0006]
    mov [0x142C], ax
    mov word [0x0004], rm_db_handler
    mov word [0x0006], 0x0000
    mov dword [0x142E], 0
    ret

enable_single_step:
    pushf
    pop ax
    or ax, 0x0100
    push ax
    popf
    ret

clear_single_step:
    pushf
    pop ax
    and ax, 0xFEFF
    push ax
    popf
    ret

restore_timeout_state:
    cli
    call clear_single_step
    xor ax, ax
    mov ds, ax
    mov ax, [0x1426]
    mov [0x0020], ax
    mov ax, [0x1428]
    mov [0x0022], ax
    mov ax, [0x142A]
    mov [0x0004], ax
    mov ax, [0x142C]
    mov [0x0006], ax
    call pic_to_kernel_state
    ret

pic_to_bios_state:
    cli
    in  al, 0x21       ; read current master PIC IMR (live kernel mask)
    mov [0x1420], al
    in  al, 0xA1       ; read current slave PIC IMR
    mov [0x1421], al

    mov al, 0x11
    out 0x20, al
    out 0xA0, al

    mov al, 0x08
    out 0x21, al
    mov al, 0x70
    out 0xA1, al

    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al

    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    ; Restore the original BIOS-side masks saved by stage2, while forcing
    ; IRQ0 open so the watchdog timer can still fire during BIOS calls.
    mov al, [0x0508]
    and al, 0xFE
    out 0x21, al
    mov al, [0x0509]
    out 0xA1, al
    ret

pic_to_bios_disk_state:
    call pic_to_bios_state
    ; Disk I/O needs the BIOS PIC layout plus the slave cascade line (IRQ2)
    ; open on the master PIC so USB/BIOS storage interrupts can propagate.
    mov al, [0x0508]
    and al, 0xFA        ; unmask IRQ0 + IRQ2
    out 0x21, al
    xor al, al          ; unmask all slave IRQs so USB DMA interrupts can fire
    out 0xA1, al
    ret

pic_to_kernel_state:
    cli
    mov al, 0x11
    out 0x20, al
    out 0xA0, al

    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al

    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al

    mov al, 0x01
    out 0x21, al
    out 0xA1, al

    mov al, [0x1420]
    out 0x21, al
    mov al, [0x1421]
    out 0xA1, al
    ret

BITS 16
rm_timeout_irq0:
    push ax
    push bx
    push ds
    xor ax, ax
    mov ds, ax
    inc word [0x1422]
    mov ax, [0x1422]
    cmp ax, [0x1424]
    jb .ack
    mov bx, sp
    mov word [ss:bx + 6], timeout_stub
    mov word [ss:bx + 8], 0x0000
.ack:
    mov al, 0x20
    out 0x20, al
    pop ds
    pop bx
    pop ax
    iret

timeout_stub:
    cli
    mov al, 'X'
    out 0xE9, al
    mov dword [0x140C], 1
    call restore_timeout_state
    jmp rm_return_to_pm

BITS 16
rm_db_handler:
    ; Count debug traps while BIOS I/O is running; patch to timeout on limit.
    push ax
    push bx
    push ds
    xor ax, ax
    mov ds, ax
    inc dword [0x142E]
    mov eax, [0x142E]
    cmp eax, [0x1432]
    jb .ack
    mov bx, sp
    mov word [ss:bx + 6], timeout_stub
    mov word [ss:bx + 8], 0x0000
    and word [ss:bx + 10], 0xFEFF
.ack:
    pop ds
    pop bx
    pop ax
    iret

BITS 16
rm_ud_handler:
    cli
    mov word [0x140C], 0x0006
    jmp rm_return_to_pm

rm_ivt_descriptor:
    dw 0x03FF
    dd 0x00000000
