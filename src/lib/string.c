#include "lib/string.h"

void *mem_set(void *dst, int value, size_t count) {
  unsigned char *p = (unsigned char *)dst;
  for (size_t i = 0; i < count; i++) {
    p[i] = (unsigned char)value;
  }
  return dst;
}

void *mem_copy(void *dst, const void *src, size_t count) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < count; i++) {
    d[i] = s[i];
  }
  return dst;
}

size_t str_len(const char *s) {
  size_t n = 0;
  while (s && s[n]) {
    n++;
  }
  return n;
}

int str_cmp(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return (int)((unsigned char)a[i] - (unsigned char)b[i]);
    }
    i++;
  }
  return (int)((unsigned char)a[i] - (unsigned char)b[i]);
}

int str_ncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (!a[i] || !b[i]) {
      return (int)((unsigned char)a[i] - (unsigned char)b[i]);
    }
    if (a[i] != b[i]) {
      return (int)((unsigned char)a[i] - (unsigned char)b[i]);
    }
  }
  return 0;
}

int str_eq(const char *a, const char *b) {
  return str_cmp(a, b) == 0;
}

void str_copy(char *dst, const char *src, size_t dst_size) {
  if (dst_size == 0) {
    return;
  }
  size_t i = 0;
  for (; i + 1 < dst_size && src[i]; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
}

void str_cat(char *dst, const char *src, size_t dst_size) {
  size_t dlen = str_len(dst);
  if (dlen >= dst_size) {
    return;
  }
  str_copy(dst + dlen, src, dst_size - dlen);
}

int split_args(char *input, char *argv[], int argv_max) {
  int argc = 0;
  char *p = input;

  while (*p) {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (!*p) {
      break;
    }
    if (argc >= argv_max) {
      break;
    }
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }
    if (*p) {
      *p = '\0';
      p++;
    }
  }
  return argc;
}

void u32_to_dec(uint32_t value, char *out, size_t out_size) {
  if (out_size == 0) {
    return;
  }
  char tmp[16];
  size_t idx = 0;

  if (value == 0) {
    out[0] = '0';
    if (out_size > 1) {
      out[1] = '\0';
    }
    return;
  }

  while (value > 0 && idx < sizeof(tmp)) {
    tmp[idx++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  size_t o = 0;
  while (idx > 0 && o + 1 < out_size) {
    out[o++] = tmp[--idx];
  }
  out[o] = '\0';
}

