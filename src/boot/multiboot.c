#include "boot/multiboot.h"

enum { MULTIBOOT_BOOTLOADER_MAGIC = 0x2BADB002 };

typedef struct __attribute__((packed)) {
  uint32_t flags;          /* +0 */
  uint32_t mem_lower;      /* +4 */
  uint32_t mem_upper;      /* +8 */
  uint32_t boot_device;    /* +12 */
  uint32_t cmdline;        /* +16 */
  uint32_t mods_count;     /* +20 */
  uint32_t mods_addr;      /* +24 */
  uint32_t syms[4];        /* +28..+40 */
  uint32_t mmap_length;    /* +44 */
  uint32_t mmap_addr;      /* +48 */
  uint32_t drives_length;  /* +52 */
  uint32_t drives_addr;    /* +56 */
  uint32_t config_table;   /* +60 */
  uint32_t boot_loader_name; /* +64 */
  uint32_t apm_table;      /* +68 */
  uint32_t vbe_control_info;  /* +72 */
  uint32_t vbe_mode_info;     /* +76 */
  uint16_t vbe_mode;          /* +80 */
  uint16_t vbe_interface_seg; /* +82 */
  uint16_t vbe_interface_off; /* +84 */
  uint16_t vbe_interface_len; /* +86 */
  uint64_t framebuffer_addr;  /* +88 */
  uint32_t framebuffer_pitch; /* +96 */
  uint32_t framebuffer_width; /* +100 */
  uint32_t framebuffer_height;/* +104 */
  uint8_t  framebuffer_bpp;   /* +108 */
  uint8_t  framebuffer_type;  /* +109 */
} multiboot_info_t;

static uint32_t g_magic = 0;
static uint32_t g_addr = 0;
static uint32_t g_mem_lower = 0;
static uint32_t g_mem_upper = 0;
static int g_has_mem = 0;

static int      g_has_fb = 0;
static uint32_t g_fb_addr = 0;
static uint32_t g_fb_pitch = 0;
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint8_t  g_fb_bpp = 0;

void multiboot_init(uint32_t magic, uint32_t addr) {
  g_magic = magic;
  g_addr = addr;
  g_has_mem = 0;
  g_mem_lower = 0;
  g_mem_upper = 0;
  g_has_fb = 0;

  if (g_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
    return;
  }

  const multiboot_info_t *info = (const multiboot_info_t *)(uintptr_t)g_addr;

  if (info->flags & 0x1u) {
    g_mem_lower = info->mem_lower;
    g_mem_upper = info->mem_upper;
    g_has_mem = 1;
  }

  /* Bit 12: framebuffer info present */
  if ((info->flags & (1u << 12)) && info->framebuffer_type == 1) {
    g_fb_addr   = (uint32_t)info->framebuffer_addr;
    g_fb_pitch  = info->framebuffer_pitch;
    g_fb_width  = info->framebuffer_width;
    g_fb_height = info->framebuffer_height;
    g_fb_bpp    = info->framebuffer_bpp;
    g_has_fb    = 1;
  }
}

uint32_t multiboot_mem_lower_kb(void) {
  return g_mem_lower;
}

uint32_t multiboot_mem_upper_kb(void) {
  return g_mem_upper;
}

int multiboot_has_mem_info(void) {
  return g_has_mem;
}

int multiboot_has_framebuffer(void) {
  return g_has_fb;
}

uint32_t multiboot_fb_addr(void) {
  return g_fb_addr;
}

uint32_t multiboot_fb_pitch(void) {
  return g_fb_pitch;
}

uint32_t multiboot_fb_width(void) {
  return g_fb_width;
}

uint32_t multiboot_fb_height(void) {
  return g_fb_height;
}

uint8_t multiboot_fb_bpp(void) {
  return g_fb_bpp;
}

