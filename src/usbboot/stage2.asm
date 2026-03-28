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
    mov al, 'E'
    call show_char

    mov dword [copy_dest], 0x00100000

    ; Read kernel in 8-sector (4 KB) chunks for maximum real hardware compatibility
    mov word [sectors_left], KERNEL_SECTORS
.read_loop:
    cmp word [sectors_left], 0
    je .read_done
    mov ax, [sectors_left]
    cmp ax, 8
    jbe .chunk_ok
    mov ax, 8
.chunk_ok:
    mov [dap_count], ax
    sub [sectors_left], ax

    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13

    jc disk_error

    mov al, 'C'
    call show_char

    call copy_chunk_high
    mov al, '.'
    call show_char

    ; Advance LBA
    mov ax, [dap_count]
    add [dap_lba], ax
    adc word [dap_lba + 2], 0

    jmp .read_loop
.read_done:
    mov al, 'R'
    call show_char

    ; Try the preferred 32bpp VBE mode chain while still in real mode.
    mov al, 'V'
    call show_char
    xor ax, ax
    mov es, ax
    mov di, 0x2000
    mov dword [es:di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    mov bp, vbe_pref_modes

.pref_loop:
    mov ax, [bp]
    cmp ax, 0
    je .no_vbe

    ; Walk the mode list (far ptr at VBE info + 14)
    mov si, [0x200E]
    mov dx, [0x2010]

.vbe_scan:
    mov es, dx
    mov cx, [es:si]
    xor ax, ax
    mov es, ax

    cmp cx, 0xFFFF
    je .next_pref

    add si, 2

    ; Fetch mode info for this mode
    push si
    push dx
    push cx
    push bp

    mov di, 0x2200
    mov ax, 0x4F01
    int 0x10

    pop bp
    pop cx
    pop dx
    pop si

    cmp ax, 0x004F
    jne .vbe_scan

    ; LFB support, 32bpp direct color, and a valid framebuffer are required.
    test byte [0x2200], 0x80
    jz .vbe_scan

    mov ax, [bp]
    cmp word [0x2212], ax
    jne .vbe_scan
    mov ax, [bp + 2]
    cmp word [0x2214], ax
    jne .vbe_scan

    cmp byte [0x2219], 32
    jne .vbe_scan
    cmp byte [0x221B], 6
    jne .vbe_scan
    cmp dword [0x2228], 0
    je .vbe_scan

    ; Set the chosen mode with the linear framebuffer bit enabled.
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe

    ; Save video info for the kernel at 0x0510.
    mov eax, [0x2228]
    mov [0x0510], eax

    xor eax, eax
    mov ax, [0x2210]
    mov [0x0514], eax

    mov ax, [0x2212]
    mov [0x0518], ax

    mov ax, [0x2214]
    mov [0x051A], ax

    mov al, [0x2219]
    mov [0x051C], al

    mov byte [0x051D], 0x01
    jmp .vbe_done

.next_pref:
    add bp, 4
    jmp .pref_loop

.no_vbe:
    mov byte [0x051D], 0x00

.vbe_done:
    mov al, 'P'
    call show_char
    xor ax, ax
    mov es, ax

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

disk_error:
    mov al, 'D'
    call show_char
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

show_char:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    pop bx
    pop ax
    ret

enable_a20:
    ; Method 1: BIOS INT 15h AX=2401h
    mov ax, 0x2401
    int 0x15
    call .check_a20
    jc .a20_done

    ; Method 2: Port 0x92 Fast A20
    in al, 0x92
    and al, 0xFE
    or al, 0x02
    out 0x92, al
    call .check_a20
    jc .a20_done

    ; If none worked, spin and hang with error message
    mov al, 'A'
    call show_char
    cli
    hlt

.a20_done:
    ret

.check_a20:                     ; set carry if A20 is enabled
    push ds
    push es
    mov ax, 0xFFFF
    mov es, ax
    mov ax, 0x0000
    mov ds, ax
    mov word [ds:0x7DFE], 0xAA55
    cmp word [es:0x7E0E], 0xAA55
    je .a20_off
    stc
    pop es
    pop ds
    ret
.a20_off:
    clc
    pop es
    pop ds
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
    mov dx, 0x3F9
    mov al, 0x00
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 12
    out dx, al
    mov dx, 0x3F9
    mov al, 0x00
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al

    ; Verify A20 using a scratch word just past the loaded kernel image so the
    ; check does not clobber the kernel entry page after chunked copies.
    mov edi, [copy_dest]
    mov dword [edi], 0xCAFEBABE
    mov dword [0x000000], 0xDEADBEEF
    mov eax, [edi]
    cmp eax, 0xDEADBEEF
    je .a20_fail

    ; A20 OK - send 'S' to COM1 to confirm protected mode entry
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
    ; A20 not enabled - send '!' and hang
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
    mov eax, 0x00100000
    jmp eax

[BITS 16]
copy_chunk_high:
    ; Use "unreal mode": load 32-bit DS descriptor without setting PE bit
    ; This avoids the PE switch which hangs on this hardware

    push ds                         ; save real-mode DS

    cli
    lgdt [gdt_descriptor]

    ; Temporarily set PE to load 32-bit DS
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Load 32-bit data descriptor into DS (still in "protected mode" but will unset PE)
    mov ax, 0x10
    mov ds, ax

    ; Clear PE bit immediately (back to real mode, but DS is now 32-bit capable)
    mov eax, cr0
    and eax, 0xFFFFFFFE
    mov cr0, eax

    ; Now in unreal mode: real mode execution but 32-bit DS addressing
    ; Copy kernel chunk using 32-bit addressing
    xor ax, ax                      ; AX = 0 for segment calculations
    mov es, ax                      ; ES = 0 for any non-critical use

    mov esi, 0x00010000             ; ESI = 32-bit source address (DS:ESI)
    mov edi, [copy_dest]             ; EDI = 32-bit dest address

    movzx ecx, word [dap_count]
    shl ecx, 7                      ; Convert sectors to dwords
    cld

.copy_loop:
    mov eax, [esi]                  ; Read from DS:[ESI] with 32-bit DS
    mov [edi], eax                  ; Write to ES:[EDI]
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_loop

    ; Update copy_dest for next chunk
    mov eax, [dap_count]
    shl eax, 9
    add [copy_dest], eax

    ; Restore real-mode DS
    pop ds

    sti  ; Re-enable interrupts after copy

    ret

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

copy_dest:
    dd 0x00100000

vbe_pref_modes:
    dw 1366, 768
    dw 1280, 800
    dw 1280, 720
    dw 1024, 768
    dw 800, 600
    dw 0, 0

gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
