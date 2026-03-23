ENTRY(_start)

SECTIONS
{
  . = 1M;

  .multiboot ALIGN(4K) : {
    *(.multiboot)
  }

  .text ALIGN(4K) : {
    *(.text*)
  }

  .rodata ALIGN(4K) : {
    *(.rodata*)
  }

  .data ALIGN(4K) : {
    *(.data*)
  }

  .bss ALIGN(4K) : {
    __bss_start = .;
    *(COMMON)
    *(.bss*)
    __bss_end = .;
  }

  __bss_size = __bss_end - __bss_start;
  __bss_dwords = __bss_size / 4;
  __kernel_end = .;
}
