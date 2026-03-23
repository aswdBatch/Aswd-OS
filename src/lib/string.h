#pragma once

#include <stddef.h>
#include <stdint.h>

void *mem_set(void *dst, int value, size_t count);
void *mem_copy(void *dst, const void *src, size_t count);

size_t str_len(const char *s);
int str_cmp(const char *a, const char *b);
int str_ncmp(const char *a, const char *b, size_t n);
int str_eq(const char *a, const char *b);

void str_copy(char *dst, const char *src, size_t dst_size);
void str_cat(char *dst, const char *src, size_t dst_size);

int split_args(char *input, char *argv[], int argv_max);

void u32_to_dec(uint32_t value, char *out, size_t out_size);

