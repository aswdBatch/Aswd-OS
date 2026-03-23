BITS 32

SECTION .rodata
gdt_start:
    dq 0x0000000000000000   ; null
    dq 0x00CF9A000000FFFF   ; 0x08 kernel code 32-bit
    dq 0x00CF92000000FFFF   ; 0x10 kernel data 32-bit
    dq 0x00009A000000FFFF   ; 0x18 code 16-bit (for trampoline PM16 step)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

SECTION .text
global gdt_flush

gdt_flush:
    lgdt [gdt_descriptor]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.flush_cs
.flush_cs:
    ret

