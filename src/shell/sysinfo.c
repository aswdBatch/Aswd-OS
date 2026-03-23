#include "shell/sysinfo.h"

#include "boot/multiboot.h"
#include "common/colors.h"
#include "console/console.h"
#include "cpu/cpuid.h"
#include "lib/string.h"

void sysinfo_print(void) {
  char brand[49];
  cpuid_get_brand(brand);

  console_write("CPU: ");
  console_writeln(brand);

  if (multiboot_has_mem_info()) {
    char num[16];
    console_write("RAM lower KB: ");
    u32_to_dec(multiboot_mem_lower_kb(), num, sizeof(num));
    console_writeln(num);

    console_write("RAM upper KB: ");
    u32_to_dec(multiboot_mem_upper_kb(), num, sizeof(num));
    console_writeln(num);
  } else {
    console_writeln("RAM: unknown");
  }
}

