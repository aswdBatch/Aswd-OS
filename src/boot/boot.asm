BITS 32

SECTION .multiboot
align 4
dd 0x1BADB002              ; +0  magic
dd 0x00000007              ; +4  flags: align + meminfo + video
dd -(0x1BADB002 + 0x00000007)  ; +8  checksum
dd 0, 0, 0, 0, 0          ; +12..+28  address fields (unused, bit 16 not set)
dd 0                       ; +32 mode_type: 0 = linear graphics
dd 800                     ; +36 width
dd 600                     ; +40 height
dd 32                      ; +44 depth (bpp)

SECTION .bss
align 16
stack_bottom:
    resb 16384
stack_top:

SECTION .text
global _start
extern kernel_main
extern gdt_flush

_start:
    cli

    mov esp, stack_top
    xor ebp, ebp

    ; Save BIOS PIC masks for trampoline compatibility
    in al, 0x21
    mov [0x0508], al
    in al, 0xA1
    mov [0x0509], al

    call gdt_flush

    ; cdecl: kernel_main(magic, addr)
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang
