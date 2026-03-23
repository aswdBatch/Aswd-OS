#pragma once

#include <stdint.h>

typedef struct __attribute__((packed)) {
    char     signature[4];       /* "VESA" */
    uint16_t version;
    uint32_t oem_string;         /* far pointer */
    uint32_t capabilities;
    uint32_t mode_list_ptr;      /* far pointer to uint16_t[] terminated by 0xFFFF */
    uint16_t total_memory;       /* in 64 KB blocks */
    uint8_t  reserved[492];
} vbe_info_t;

typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t  win_a_attr;
    uint8_t  win_b_attr;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t pitch;              /* bytes per scan line */
    uint16_t width;
    uint16_t height;
    uint8_t  char_width;
    uint8_t  char_height;
    uint8_t  planes;
    uint8_t  bpp;
    uint8_t  banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  image_pages;
    uint8_t  reserved0;
    /* Direct color fields */
    uint8_t  red_mask_size;
    uint8_t  red_field_pos;
    uint8_t  green_mask_size;
    uint8_t  green_field_pos;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_pos;
    uint8_t  rsvd_mask_size;
    uint8_t  rsvd_field_pos;
    uint8_t  direct_color_info;
    /* VBE 2.0+ */
    uint32_t framebuffer;        /* physical address of LFB */
    uint32_t off_screen_mem;
    uint16_t off_screen_mem_size;
    uint8_t  reserved1[206];
} vbe_mode_info_t;

int vbe_get_info(vbe_info_t *out);
int vbe_get_mode_info(uint16_t mode, vbe_mode_info_t *out);
int vbe_set_mode(uint16_t mode);
int vbe_find_mode(uint16_t w, uint16_t h, uint8_t bpp,
                  uint16_t *out_mode, vbe_mode_info_t *out_info);
