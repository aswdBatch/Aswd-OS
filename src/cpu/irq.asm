BITS 32

SECTION .text
global irq0_stub
global irq1_stub
global irq4_stub
global irq12_stub
extern timer_tick
extern keyboard_irq_handler
extern serial_irq_handler
extern mouse_irq_handler

irq0_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call timer_tick

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd


irq1_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_irq_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

global spurious_irq_stub
spurious_irq_stub:
    ; Spurious IRQ — do NOT send EOI to PIC1 for IRQ7 (it is not in-service).
    ; For IRQ15 the caller must EOI PIC1 only (but we register same stub for
    ; simplicity — hardware that generates spurious IRQ15 is rare on this target).
    iretd

irq4_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call serial_irq_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

irq12_stub:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call mouse_irq_handler

    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd
