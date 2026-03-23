BITS 16
ORG 0x7C00

%ifndef PARTITION_START
%define PARTITION_START 2048
%endif

%ifndef PARTITION_SECTORS
%define PARTITION_SECTORS 0x0001F800
%endif

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl
    mov ax, 0x0003
    int 0x10
    call show_marker

    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp 0x0000:0x0600

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
    mov al, 'M'
    int 0x10
    mov al, 'B'
    int 0x10
    mov al, 'R'
    int 0x10
    pop bx
    pop ax
    ret

boot_drive db 0

dap:
    db 0x10
    db 0x00
    dw 0x0001
    dw 0x0600
    dw 0x0000
    dq PARTITION_START

times 446 - ($ - $$) db 0

; Standard bootable FAT32-style primary partition layout.
db 0x80
db 0x20, 0x21, 0x00
db 0x0C
db 0xFE, 0xFF, 0xFF
dd PARTITION_START
dd PARTITION_SECTORS

times 64 - ($ - $$ - 446) db 0
dw 0xAA55
