BITS 32

SECTION .text

; ISR stubs for CPU exceptions 0-13.
; Exceptions without a CPU-pushed error code get a fake 0 pushed first,
; so the stack layout at exception_common is always: vector, error_code.

%macro ISR_NO_ERR 1
global isr%1_stub
isr%1_stub:
    push dword 0    ; fake error code
    push dword %1   ; vector number
    jmp exception_common
%endmacro

%macro ISR_WITH_ERR 1
global isr%1_stub
isr%1_stub:
    ; CPU already pushed the error code
    push dword %1   ; vector number
    jmp exception_common
%endmacro

ISR_NO_ERR   0   ; Divide Error
ISR_NO_ERR   1   ; Debug
ISR_NO_ERR   2   ; NMI
ISR_NO_ERR   3   ; Breakpoint
ISR_NO_ERR   4   ; Overflow
ISR_NO_ERR   5   ; Bound Range Exceeded
ISR_NO_ERR   6   ; Invalid Opcode
ISR_NO_ERR   7   ; Device Not Available
ISR_WITH_ERR 8   ; Double Fault (error code always 0)
ISR_NO_ERR   9   ; Coprocessor Segment Overrun
ISR_WITH_ERR 10  ; Invalid TSS
ISR_WITH_ERR 11  ; Segment Not Present
ISR_WITH_ERR 12  ; Stack Fault
ISR_WITH_ERR 13  ; General Protection Fault

extern exception_handler

exception_common:
    ; Stack on entry: vector, error_code, eip, cs, eflags (from CPU)
    ; Set kernel data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Call C handler: cdecl expects first arg at [esp+4] after the call instruction
    ; pushes its return address. Stack is: [esp]=vector, [esp+4]=error_code, so
    ; after `call`: [esp+4]=vector, [esp+8]=error_code — correct for cdecl.
    call exception_handler
.hang:
    cli
    hlt
    jmp .hang
