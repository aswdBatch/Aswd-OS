#include "drivers/keyboard.h"

#include <stdint.h>

#include "drivers/i8042.h"
#include "cpu/pic.h"
#include "cpu/ports.h"
#include "drivers/serial.h"
#include "lib/string.h"

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdtr_t;

enum {
    KBD_TRAMPOLINE_ADDR = 0x8000u,
    KBD_TRAMPOLINE_GDT = 0x1460u,
    KBD_TRAMPOLINE_PARAM = 0x1400u,
};

extern uint8_t _binary_obj_usbboot_trampoline_bin_start[];
extern uint8_t _binary_obj_usbboot_trampoline_bin_end[];

static volatile char g_buf[256];
static volatile uint8_t g_head = 0;
static volatile uint8_t g_tail = 0;
static volatile uint8_t g_shift = 0;
static volatile uint8_t g_ctrl = 0;
static volatile uint8_t g_e0_pending = 0;
static volatile uint8_t g_ps2_ready = 0;

enum {
    BIOS_KBD_HEAD = 0x041Au,
    BIOS_KBD_TAIL = 0x041Cu,
    BIOS_KBD_BUF = 0x041Eu,
    BIOS_KBD_FIRST = 0x001Eu,
    BIOS_KBD_LAST = 0x003Cu,
    BIOS_KBD_SENTINEL = 0x003Eu,
};

static const char kbdus[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',  '\b',
    '\t','q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,  '\\', 'z',
    'x', 'c',  'v', 'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,
};

static const char kbdus_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',  '\b',
    '\t','Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S',  'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,  '|',  'Z',
    'X', 'C',  'V', 'B', 'N', 'M', '<', '>', '?',  0,  '*',  0,  ' ',  0,
};

static void kbd_log(const char *msg) {
    serial_write("[kbd] ");
    serial_write(msg);
    serial_write("\n");
}

static void buf_push(char c) {
    uint8_t next = (uint8_t)(g_head + 1);
    if (next == g_tail) {
        return;
    }
    g_buf[g_head] = c;
    g_head = next;
}

static int buf_pop(char *out) {
    if (g_tail == g_head) {
        return 0;
    }
    *out = g_buf[g_tail];
    g_tail = (uint8_t)(g_tail + 1);
    return 1;
}

void keyboard_push_char(char c) {
    buf_push(c);
}

static uint32_t trampoline_size(void) {
    return (uint32_t)(_binary_obj_usbboot_trampoline_bin_end -
                      _binary_obj_usbboot_trampoline_bin_start);
}

static void trampoline_copy(void) {
    mem_copy((void *)KBD_TRAMPOLINE_ADDR,
             _binary_obj_usbboot_trampoline_bin_start,
             trampoline_size());
}

static void trampoline_save_gdtr(void) {
    gdtr_t gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    mem_copy((void *)KBD_TRAMPOLINE_GDT, &gdtr, sizeof(gdtr));
}

static char bios_translate(uint16_t word) {
    uint8_t ascii = (uint8_t)(word & 0xFFu);
    uint8_t scan = (uint8_t)(word >> 8);

    if (ascii != 0) {
        return (char)ascii;
    }

    if (scan == 0x48u) return KEY_UP;
    if (scan == 0x50u) return KEY_DOWN;
    if (scan == 0x4Bu) return KEY_LEFT;
    if (scan == 0x4Du) return KEY_RIGHT;
    if (scan == 0x53u) return KEY_DELETE;
    if (scan == 0x47u) return KEY_HOME;
    if (scan == 0x4Fu) return KEY_END;
    if (scan == 0x49u) return KEY_PAGEUP;
    if (scan == 0x51u) return KEY_PAGEDOWN;
    if (scan == 0x52u) return KEY_INSERT;
    return 0;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

static void bios_kbd_reset(void) {
    *(volatile uint16_t *)BIOS_KBD_HEAD = BIOS_KBD_FIRST;
    *(volatile uint16_t *)BIOS_KBD_TAIL = BIOS_KBD_FIRST;
}

static int bios_kbd_valid_ptr(uint16_t ptr) {
    if (ptr == BIOS_KBD_SENTINEL) {
        return 1;
    }
    return ptr >= BIOS_KBD_FIRST &&
           ptr <= BIOS_KBD_LAST &&
           ((ptr - BIOS_KBD_FIRST) % 2u) == 0u;
}

static int bios_kbd_pop(char *out) {
    volatile uint16_t *headp = (volatile uint16_t *)BIOS_KBD_HEAD;
    volatile uint16_t *tailp = (volatile uint16_t *)BIOS_KBD_TAIL;
    volatile uint16_t *buf = (volatile uint16_t *)BIOS_KBD_BUF;

    uint16_t head = *headp;
    uint16_t tail = *tailp;
    if (!bios_kbd_valid_ptr(head) || !bios_kbd_valid_ptr(tail)) {
        bios_kbd_reset();
        return 0;
    }

    if (head == tail) {
        return 0;
    }

    if (head == BIOS_KBD_SENTINEL) {
        head = BIOS_KBD_FIRST;
    }
    if (head > BIOS_KBD_LAST) {
        bios_kbd_reset();
        return 0;
    }

    uint16_t idx = (uint16_t)((head - BIOS_KBD_FIRST) / 2u);
    if (idx >= 16u) {
        bios_kbd_reset();
        return 0;
    }

    uint16_t word = buf[idx];
    uint16_t next = (uint16_t)(head + 2u);
    if (next > BIOS_KBD_SENTINEL) {
        next = BIOS_KBD_FIRST;
    }
    *headp = next;

    char c = bios_translate(word);
    if (!c) {
        return 0;
    }

    *out = c;
    return 1;
}

static int bios_kbd_trampoline_pop(char *out) {
    typedef void (*tramp_fn_t)(void);
    volatile uint8_t *op = (volatile uint8_t *)(KBD_TRAMPOLINE_PARAM + 1);
    volatile uint16_t *word = (volatile uint16_t *)(KBD_TRAMPOLINE_PARAM + 8);
    volatile uint32_t *result = (volatile uint32_t *)(KBD_TRAMPOLINE_PARAM + 12);
    tramp_fn_t tramp = (tramp_fn_t)KBD_TRAMPOLINE_ADDR;
    uint16_t raw;
    char c;

    *op = 2;
    *word = 0;
    *result = 0;
    tramp();
    if (*result == 0) {
        return 0;
    }

    raw = *word;
    c = bios_translate(raw);
    if (!c) {
        return 0;
    }

    *out = c;
    return 1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void handle_scancode(uint8_t sc) {
    if (sc == 0xE0) {
        g_e0_pending = 1;
        return;
    }
    if (g_e0_pending) {
        g_e0_pending = 0;
        if (sc == 0x48) { buf_push(KEY_UP); return; }
        if (sc == 0x50) { buf_push(KEY_DOWN); return; }
        if (sc == 0x4B) { buf_push(KEY_LEFT); return; }
        if (sc == 0x4D) { buf_push(KEY_RIGHT); return; }
        if (sc == 0x53) { buf_push(KEY_DELETE); return; }
        if (sc == 0x47) { buf_push(g_ctrl ? KEY_CTRL_HOME : KEY_HOME); return; }
        if (sc == 0x4F) { buf_push(g_ctrl ? KEY_CTRL_END : KEY_END); return; }
        if (sc == 0x49) { buf_push(KEY_PAGEUP); return; }
        if (sc == 0x51) { buf_push(KEY_PAGEDOWN); return; }
        if (sc == 0x52) { buf_push(KEY_INSERT); return; }
        if (sc == 0x1D) { g_ctrl = 1; return; }
        if (sc == 0x9D) { g_ctrl = 0; return; }
        return;
    }
    if (sc == 0x2A || sc == 0x36) {
        g_shift = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) {
        g_shift = 0;
        return;
    }
    if (sc == 0x1D) {
        g_ctrl = 1;
        return;
    }
    if (sc == 0x9D) {
        g_ctrl = 0;
        return;
    }
    if (sc & 0x80u) {
        return;
    }

    char c = g_shift ? kbdus_shift[sc] : kbdus[sc];
    if (!c) {
        return;
    }

    if (g_ctrl) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 1);
        } else if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 1);
        } else {
            return;
        }
    }

    buf_push(c);
}

void keyboard_irq_handler(void) {
    uint8_t st;
    if (!g_ps2_ready) {
        pic_send_eoi(1);
        return;
    }

    st = inb(0x64);
    if ((st & 0x01u) && !(st & 0x20u)) {
        handle_scancode(inb(0x60));
    }
    pic_send_eoi(1);
}

int keyboard_try_getchar(char *out) {
    if (buf_pop(out)) return 1;
    if (!g_ps2_ready) {
        if (bios_kbd_trampoline_pop(out)) return 1;
        return bios_kbd_pop(out);
    }
    return 0;
}

int keyboard_ps2_ready(void) {
    return (int)g_ps2_ready;
}

char keyboard_getchar(void) {
    for (;;) {
        char c;
        if (keyboard_try_getchar(&c)) {
            return c;
        }
        __asm__ volatile("sti; hlt");
    }
}

void keyboard_init(void) {
    uint8_t initial_cfg = 0;
    uint8_t config = 0;
    uint8_t ack = 0;
    uint8_t self_test = 0;
    int have_initial_cfg;

    g_head = 0;
    g_tail = 0;
    g_shift = 0;
    g_ctrl = 0;
    g_e0_pending = 0;
    g_ps2_ready = 0;

    trampoline_save_gdtr();
    trampoline_copy();

    bios_kbd_reset();
    i8042_flush_output();
    have_initial_cfg = i8042_read_config(&initial_cfg);
    i8042_disable_ports();
    i8042_flush_output();

    if (!i8042_write_command(0xAAu) ||
        !i8042_read_data(&self_test, 0, 0) ||
        self_test != 0x55u) {
        kbd_log("controller self-test failed; using BIOS fallback");
        i8042_enable_first_port();
        i8042_flush_output();
        return;
    }

    if (!i8042_read_config(&config)) {
        kbd_log("unable to read controller config; using BIOS fallback");
        i8042_enable_first_port();
        i8042_flush_output();
        return;
    }

    config &= (uint8_t)~0x03u;
    config |= 0x01u;
    if (have_initial_cfg) {
        config = (uint8_t)((config & (uint8_t)~0x40u) | (initial_cfg & 0x40u));
    }
    if (!i8042_write_config(config)) {
        kbd_log("unable to write controller config; using BIOS fallback");
        i8042_enable_first_port();
        i8042_flush_output();
        return;
    }

    i8042_enable_first_port();
    i8042_flush_output();

    if (!i8042_write_device(0, 0xF4u) ||
        !i8042_read_typed(0, &ack, 0) ||
        ack != 0xFAu) {
        kbd_log("keyboard enable failed; using BIOS fallback");
        i8042_enable_first_port();
        i8042_flush_output();
        return;
    }

    i8042_flush_output();
    g_ps2_ready = 1;
    pic_clear_mask(1);
    kbd_log("PS/2 keyboard ready");
}
