BITS 16
ORG 0x8000

%ifndef KERNEL_LBA
%define KERNEL_LBA 5
%endif

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1
%endif

%ifndef PARTITION_START
%define PARTITION_START 2048
%endif

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7B00

    mov [boot_drive], dl
    ; Save boot drive and partition start LBA for kernel's disk driver
    mov byte [0x0500], dl
    mov dword [0x0504], PARTITION_START
    ; Save the BIOS PIC masks so the kernel-side disk helper can restore
    ; the same interrupt mask state when it drops back to real mode.
    in al, 0x21
    mov [0x0508], al
    in al, 0xA1
    mov [0x0509], al
    call show_marker

    call enable_a20

    ; Read kernel in 64-sector (32 KB) chunks to stay within BIOS limits
    mov word [sectors_left], KERNEL_SECTORS
.read_loop:
    cmp word [sectors_left], 0
    je .read_done
    mov ax, [sectors_left]
    cmp ax, 64
    jbe .chunk_ok
    mov ax, 64
.chunk_ok:
    mov [dap_count], ax
    sub [sectors_left], ax

    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; Advance buffer segment: +count*32 (count*512/16)
    mov ax, [dap_count]
    shl ax, 5
    add [dap_seg], ax

    ; Advance LBA
    mov ax, [dap_count]
    add [dap_lba], ax
    adc word [dap_lba+2], 0

    jmp .read_loop
.read_done:

    ; ================================================================
    ; VBE: Set 800x600x32 while still in real mode (no trampoline needed)
    ; Buffers: 0x2000 = VBE info (512 B), 0x2200 = mode info (256 B)
    ; Results saved to 0x0510-0x051D for kernel
    ; ================================================================

    ; Step 1 — Get VBE controller info
    xor ax, ax
    mov es, ax
    mov di, 0x2000
    mov dword [es:di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    ; Step 2 — Walk the mode list (far ptr at VBE info + 14)
    mov si, [0x200E]            ; offset  of mode list
    mov dx, [0x2010]            ; segment of mode list

.vbe_scan:
    mov es, dx
    mov cx, [es:si]             ; read mode number
    xor ax, ax
    mov es, ax                  ; restore ES = 0

    cmp cx, 0xFFFF
    je .no_vbe                  ; end of list

    add si, 2

    ; Step 3 — Get mode info for this mode
    push si
    push dx
    push cx

    mov di, 0x2200
    mov ax, 0x4F01
    int 0x10

    pop cx
    pop dx
    pop si

    cmp ax, 0x004F
    jne .vbe_scan

    ; LFB available? (bit 7 of mode attributes)
    test byte [0x2200], 0x80
    jz .vbe_scan

    ; 800 x 600?
    cmp word [0x2212], 800
    jne .vbe_scan
    cmp word [0x2214], 600
    jne .vbe_scan

    ; 32 bpp?
    cmp byte [0x2219], 32
    jne .vbe_scan

    ; Direct-color memory model (6)?
    cmp byte [0x221B], 6
    jne .vbe_scan

    ; Framebuffer address non-zero?
    cmp dword [0x2228], 0
    je .vbe_scan

    ; Step 4 — Set the mode (bit 14 = LFB enable)
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    ; Step 5 — Save video info for kernel at 0x0510
    mov eax, [0x2228]
    mov [0x0510], eax           ; framebuffer address

    xor eax, eax
    mov ax, [0x2210]
    mov [0x0514], eax           ; pitch

    mov ax, [0x2212]
    mov [0x0518], ax            ; width

    mov ax, [0x2214]
    mov [0x051A], ax            ; height

    mov al, [0x2219]
    mov [0x051C], al            ; bpp

    mov byte [0x051D], 0x01     ; valid flag
    jmp .vbe_done

.no_vbe:
    mov byte [0x051D], 0x00

.vbe_done:
    xor ax, ax
    mov es, ax

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

disk_error:
    cli
    hlt
    jmp disk_error

show_marker:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    mov al, '2'
    int 0x10
    mov al, 'S'
    int 0x10
    mov al, 'T'
    int 0x10
    pop bx
    pop ax
    ret

enable_a20:
    ; Method 1: BIOS INT 15h AX=2401h (most compatible)
    mov ax, 0x2401
    int 0x15
    jnc .a20_done

    ; Method 2: Port 0x92 Fast A20 (fallback)
    ; AND with 0xFE first to clear bit 0 (fast-reset pin on some chipsets)
    in al, 0x92
    and al, 0xFE
    or al, 0x02
    out 0x92, al

.a20_done:
    ret

[BITS 32]
protected_entry:
    cld
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9F000

    ; Initialize COM1: 9600 baud, 8N1, no interrupts
    mov dx, 0x3F9           ; IER: disable all interrupts
    mov al, 0x00
    out dx, al
    mov dx, 0x3FB           ; LCR: set DLAB=1 to access divisor
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8           ; DLL: divisor low byte (115200/12 = 9600)
    mov al, 12
    out dx, al
    mov dx, 0x3F9           ; DLM: divisor high byte
    mov al, 0x00
    out dx, al
    mov dx, 0x3FB           ; LCR: 8N1, clear DLAB
    mov al, 0x03
    out dx, al

    ; Verify A20: write canary to 0x100000, different value to 0x000000
    ; If A20 is off, 0x100000 aliases to 0x000000 and [0x100000] reads back the second write
    mov dword [0x100000], 0xCAFEBABE
    mov dword [0x000000], 0xDEADBEEF
    mov eax, [0x100000]
    cmp eax, 0xDEADBEEF
    je .a20_fail

    ; A20 OK — send 'S' to COM1 to confirm protected mode entry
    mov dx, 0x3FD
.wait_s:
    in al, dx
    and al, 0x20
    jz .wait_s
    mov dx, 0x3F8
    mov al, 'S'
    out dx, al
    jmp .copy_kernel

.a20_fail:
    ; A20 not enabled — send '!' and hang
    mov dx, 0x3FD
.wait_fail:
    in al, dx
    and al, 0x20
    jz .wait_fail
    mov dx, 0x3F8
    mov al, '!'
    out dx, al
.hang:
    hlt
    jmp .hang

.copy_kernel:
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    mov eax, 0x00100000
    jmp eax

[BITS 16]

boot_drive db 0

dap:
    db 0x10
    db 0x00
dap_count:
    dw 0
    dw 0x0000
dap_seg:
    dw 0x1000
dap_lba:
    dq KERNEL_LBA

sectors_left:
    dw 0

gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
