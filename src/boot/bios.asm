BITS 32

SECTION .text
global bios_start
extern kernel_main
extern gdt_flush
extern __bss_start
extern __bss_dwords

bios_start:
    cli
    cld

    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_dwords
    rep stosd

    mov esp, stack_top
    xor ebp, ebp

    call gdt_flush

    ; Serial debug: write 'B' to COM1 to confirm bios_start reached
    mov dx, 0x3FD
.wait_b:
    in al, dx
    and al, 0x20
    jz .wait_b
    mov dx, 0x3F8
    mov al, 'B'
    out dx, al

    push dword 0
    push dword 0
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

SECTION .bss
align 16
stack_bottom:
    resb 16384
stack_top:
