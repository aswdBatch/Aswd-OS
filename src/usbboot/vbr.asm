BITS 16
ORG 0x0600

%ifndef PARTITION_START
%define PARTITION_START 2048
%endif

%ifndef PARTITION_SECTORS
%define PARTITION_SECTORS 0x0001F800
%endif

%ifndef SECTORS_PER_CLUSTER
%define SECTORS_PER_CLUSTER 8
%endif

%ifndef RESERVED_SECTORS
%define RESERVED_SECTORS 32
%endif

%ifndef FAT_SECTORS
%define FAT_SECTORS 128
%endif

%ifndef STAGE2_LBA
%define STAGE2_LBA 0
%endif

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 8
%endif

jmp short start
nop
db 'MSWIN4.1'
dw 512
db SECTORS_PER_CLUSTER
dw RESERVED_SECTORS
db 2
dw 0
dw 0
db 0xF8
dw 0
dw 63
dw 255
dd PARTITION_START
dd PARTITION_SECTORS
dd FAT_SECTORS
dw 0
dw 0
dd 2
dw 1
dw 6
times 12 db 0
db 0x80
db 0
db 0x29
dd 0x12345678
db 'ASWDOS     '
db 'FAT32   '

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7B00

    mov [boot_drive], dl
    mov ax, 0x0003
    int 0x10
    call show_marker

    call enable_a20

    mov dl, [boot_drive]
    mov si, dap
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp 0x0000:0x8000

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
    mov al, 'V'
    int 0x10
    mov al, 'B'
    int 0x10
    mov al, 'R'
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

boot_drive db 0

dap:
    db 0x10
    db 0x00
    dw STAGE2_SECTORS
    dw 0x0000
    dw 0x0800
    dq STAGE2_LBA

times 510 - ($ - $$) db 0
dw 0xAA55
