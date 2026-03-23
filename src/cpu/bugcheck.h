#pragma once

#include <stdint.h>

typedef enum {
  BUGCHECK_STYLE_LEGACY = 0,
  BUGCHECK_STYLE_MODERN = 1,
} bugcheck_style_t;

typedef struct __attribute__((packed)) {
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;
  uint32_t ebx;
  uint32_t edx;
  uint32_t ecx;
  uint32_t eax;
  uint32_t vector;
  uint32_t error_code;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
} exception_frame_t;

void bugcheck_set_style(bugcheck_style_t style);
bugcheck_style_t bugcheck_get_style(void);

__attribute__((noreturn)) void bugcheck(const char *code, const char *msg);
__attribute__((noreturn)) void bugcheck_ex(const exception_frame_t *frame);
__attribute__((noreturn)) void panic(const char *code, const char *msg);
