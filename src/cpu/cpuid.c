#include "cpu/cpuid.h"

#include <stdint.h>

#include "lib/string.h"

void cpuid_get_vendor(char out[13]) {
  uint32_t eax, ebx, ecx, edx;
  eax = 0;
  __asm__ volatile("cpuid"
                   : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                   : "a"(eax));

  mem_copy(out + 0, &ebx, 4);
  mem_copy(out + 4, &edx, 4);
  mem_copy(out + 8, &ecx, 4);
  out[12] = '\0';
}

void cpuid_get_brand(char out[49]) {
  uint32_t regs[4];

  /* Check if extended CPUID brand string is supported (need >= 0x80000004) */
  regs[0] = 0x80000000u;
  __asm__ volatile("cpuid"
                   : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
                   : "a"(regs[0]));

  if (regs[0] < 0x80000004u) {
    /* Fallback: use vendor string (12 chars + null) */
    cpuid_get_vendor(out);
    return;
  }

  /* Leaves 0x80000002, 0x80000003, 0x80000004 each return 16 bytes */
  char *p = out;
  for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
    regs[0] = leaf;
    __asm__ volatile("cpuid"
                     : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3])
                     : "a"(regs[0]));
    mem_copy(p,      &regs[0], 4);
    mem_copy(p +  4, &regs[1], 4);
    mem_copy(p +  8, &regs[2], 4);
    mem_copy(p + 12, &regs[3], 4);
    p += 16;
  }
  out[48] = '\0';
}
