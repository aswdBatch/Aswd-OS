#include "cpu/bugcheck.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/gfx.h"

#define VGA_BASE  ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH 80

enum {
    ATTR_LEGACY = 0x1F, /* white on blue */
    ATTR_MODERN = 0x4F, /* white on red  */
};

static bugcheck_style_t g_style = BUGCHECK_STYLE_MODERN;

void bugcheck_set_style(bugcheck_style_t style) {
    g_style = style;
}

bugcheck_style_t bugcheck_get_style(void) {
    return g_style;
}

static void bg_putc_attr(int col, int row, char c, uint8_t attr) {
    VGA_BASE[row * VGA_WIDTH + col] = ((uint16_t)attr << 8) | (uint8_t)c;
}

static void bg_puts_attr(int col, int row, const char *s, uint8_t attr) {
    while (*s) {
        bg_putc_attr(col++, row, *s++, attr);
    }
}

static void bg_fill_row_attr(int row, uint8_t attr) {
    for (int x = 0; x < VGA_WIDTH; x++) {
        bg_putc_attr(x, row, ' ', attr);
    }
}

static void bg_fill_screen_attr(uint8_t attr) {
    for (int y = 0; y < 25; y++) {
        bg_fill_row_attr(y, attr);
    }
}

static void bg_center_text(int row, const char *text, uint8_t attr) {
    int len = 0;
    for (const char *p = text; p && *p; p++) {
        len++;
    }
    int pad = (VGA_WIDTH - len) / 2;
    if (pad < 0) {
        pad = 0;
    }
    bg_puts_attr(pad, row, text, attr);
}

static void com1_putc_raw(char c) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x3FD) & 0x20u) {
            break;
        }
    }
    outb(0x3F8, (uint8_t)c);
}

static void com1_puts_raw(const char *s) {
    while (*s) {
        com1_putc_raw(*s++);
    }
}

static void fmt_hex32(char *buf, uint32_t v) {
    static const char hex_digits[] = "0123456789ABCDEF";

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex_digits[(v >> (28 - i * 4)) & 0xFu];
    }
    buf[10] = '\0';
}

static void fmt_dec(char *buf, uint32_t v) {
    char tmp[16];
    int n = 0;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    for (int i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
}

static inline void spin_wait_one_second(void) {
    for (volatile int i = 0; i < 200000000; i++) {
    }
}

static void do_reboot_and_halt(void) {
    int use_fb = (gfx_get_mode() == GFX_MODE_GRAPHICS);
    int sw     = use_fb ? (int)gfx_width()  : 0;
    int sh     = use_fb ? (int)gfx_height() : 0;
    uint32_t BC_BLUE = gfx_rgb(10,  60, 130);
    uint32_t BC_DIM  = gfx_rgb(160, 180, 220);
    uint32_t BC_TEXT = gfx_rgb(235, 240, 255);
    int msg_x = 40;
    int msg_w = sw > 80 ? sw - 80 : sw;
    int msg_y = sh - 52;
    int i;

    /* Drain any keys already in the buffer */
    for (i = 0; i < 32; i++) {
        if (inb(0x64) & 0x01u) inb(0x60);
    }

    /* Show "waiting" notice while holding 3 seconds */
    if (use_fb) {
        gfx_fill_rect(msg_x, msg_y, msg_w, 16, BC_BLUE);
        gfx_draw_string(msg_x, msg_y, "Waiting 3 seconds...", BC_DIM, BC_BLUE);
        gfx_swap();
    } else {
        bg_fill_row_attr(22, ATTR_MODERN);
        bg_puts_attr(2, 22, "Waiting 3 seconds...", ATTR_MODERN);
    }

    spin_wait_one_second();
    spin_wait_one_second();
    spin_wait_one_second();

    /* Drain keys pressed during the wait so they don't skip the prompt */
    for (i = 0; i < 32; i++) {
        if (inb(0x64) & 0x01u) inb(0x60);
    }

    /* Prompt the user */
    if (use_fb) {
        gfx_fill_rect(msg_x, msg_y, msg_w, 16, BC_BLUE);
        gfx_draw_string(msg_x, msg_y, "Press any key to reboot.", BC_TEXT, BC_BLUE);
        gfx_swap();
    } else {
        bg_fill_row_attr(22, ATTR_MODERN);
        bg_puts_attr(2, 22, "Press any key to reboot.            ", ATTR_MODERN);
    }

    /* Block until a scancode arrives from the keyboard controller */
    for (;;) {
        uint8_t status = inb(0x64);
        if ((status & 0x01u) && !(status & 0x20u)) {
            (void)inb(0x60);
            break;
        }
        __asm__ volatile("pause");
    }

    /* Reboot via PS/2 reset line */
    for (i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02u)) break;
    }
    outb(0x64, 0xFE);

    spin_wait_one_second();

    /* Fallback: QEMU/Bochs shutdown ports */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);

    if (use_fb) {
        gfx_fill_rect(msg_x, msg_y, msg_w, 16, BC_BLUE);
        gfx_draw_string(msg_x, msg_y, "Auto-reboot failed. Restart manually.", BC_DIM, BC_BLUE);
        gfx_swap();
    } else {
        bg_fill_row_attr(22, ATTR_MODERN);
        bg_puts_attr(2, 22, "Auto-reboot failed. Restart manually.", ATTR_MODERN);
    }
    for (;;) { __asm__ volatile("hlt"); }
}

static const char *exception_name(uint32_t vector) {
    static const char *names[20] = {
        "Divide Error",
        "Debug",
        "NMI",
        "Breakpoint",
        "Overflow",
        "Bound Range Exceeded",
        "Invalid Opcode",
        "No Math Coprocessor",
        "Double Fault",
        "(reserved)",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection Fault",
        "Page Fault",
        "(reserved)",
        "Math Fault",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
    };

    return (vector < 20) ? names[vector] : "Unknown Exception";
}

static int text_len(const char *s) {
    int len = 0;
    while (s && s[len]) {
        len++;
    }
    return len;
}

static void legacy_display(const char *stop_code, const char *error_code_str, const char *hint) {
    __asm__ volatile("cli");

    bg_fill_screen_attr(ATTR_LEGACY);
    for (int x = 0; x < VGA_WIDTH; x++) {
        bg_putc_attr(x, 0, (char)0xCD, ATTR_LEGACY);
    }
    bg_center_text(0, " KERNEL BUGCHECK ", ATTR_LEGACY);

    bg_puts_attr(2, 2,  "[!] The system has encountered a fatal error and stopped.", ATTR_LEGACY);
    bg_puts_attr(2, 4,  "STOP CODE:    ", ATTR_LEGACY);
    bg_puts_attr(16, 4, stop_code ? stop_code : "(unknown)", ATTR_LEGACY);
    bg_puts_attr(2, 5,  "ERROR CODE:   ", ATTR_LEGACY);
    bg_puts_attr(16, 5, error_code_str ? error_code_str : "", ATTR_LEGACY);
    if (hint && hint[0]) {
        bg_puts_attr(2, 7, hint, ATTR_LEGACY);
    }
    bg_puts_attr(2, 9,  "If this keeps happening, note the STOP CODE and check", ATTR_LEGACY);
    bg_puts_attr(2, 10, "your hardware or recent changes.", ATTR_LEGACY);

    com1_puts_raw("\r\n*** KERNEL BUGCHECK ***\r\nSTOP CODE: ");
    com1_puts_raw(stop_code ? stop_code : "(unknown)");
    com1_puts_raw("\r\nERROR CODE: ");
    com1_puts_raw(error_code_str ? error_code_str : "");
    com1_puts_raw("\r\n");
    if (hint && hint[0]) {
        com1_puts_raw(hint);
        com1_puts_raw("\r\n");
    }
}

static void modern_display(const char *stop_code, const char *detail, const exception_frame_t *frame) {
    __asm__ volatile("cli");

    /* ── Framebuffer path (graphics mode only) ───────────────────── */
    if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
        int sw = (int)gfx_width(), sh = (int)gfx_height();
        char buf[32];
        uint32_t BC_BLUE = gfx_rgb(10,  60,  130);
        uint32_t BC_TEXT = gfx_rgb(235, 240, 255);
        uint32_t BC_DIM  = gfx_rgb(160, 180, 220);
        uint32_t BC_BOX  = gfx_rgb(8,   40,  90);

        gfx_fill_rect(0, 0, sw, sh, BC_BLUE);

        /* Header bar */
        gfx_fill_rect(0, 0, sw, 36, BC_BOX);
        gfx_draw_string(12, 10, "*** FATAL SYSTEM ERROR — AswdOS has crashed ***", BC_TEXT, BC_BOX);

        /* Error info box */
        {
            int bx = 32, by = 52;
            int bw = sw - 64;
            gfx_fill_rect(bx, by, bw, 108, BC_BOX);
            gfx_draw_string(bx+8, by+8,  "Stop code:", BC_DIM,  BC_BOX);
            gfx_draw_string(bx+128, by+8, stop_code ? stop_code : "(unknown)", BC_TEXT, BC_BOX);
            if (detail && detail[0]) {
                gfx_draw_string(bx+8, by+28, "Detail:   ", BC_DIM, BC_BOX);
                gfx_draw_string(bx+128, by+28, detail, BC_TEXT, BC_BOX);
            }
            if (frame) {
                fmt_hex32(buf, frame->eip);
                gfx_draw_string(bx+8, by+48, "EIP:      ", BC_DIM, BC_BOX);
                gfx_draw_string(bx+128, by+48, buf, BC_TEXT, BC_BOX);
                gfx_draw_string(bx+8, by+68, "Exception:", BC_DIM, BC_BOX);
                gfx_draw_string(bx+128, by+68, exception_name(frame->vector), BC_TEXT, BC_BOX);
                if (frame->vector == 14u) {
                    uint32_t cr2 = 0;
                    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
                    fmt_hex32(buf, cr2);
                    gfx_draw_string(bx+8, by+88, "Fault addr:", BC_DIM, BC_BOX);
                    gfx_draw_string(bx+128, by+88, buf, BC_TEXT, BC_BOX);
                }
            }
        }

        /* What to do */
        {
            int tx = 32, ty = 180;
            gfx_draw_string(tx, ty,      "What to do:", BC_TEXT, BC_BLUE);
            gfx_draw_string(tx, ty + 20, "1. Note the stop code and detail above.", BC_DIM, BC_BLUE);
            gfx_draw_string(tx, ty + 38, "2. Connect a serial cable (COM1) for the full crash dump.", BC_DIM, BC_BLUE);
            gfx_draw_string(tx, ty + 56, "3. Check for bad drivers, memory, or recent code changes.", BC_DIM, BC_BLUE);
        }

        gfx_swap();
    }
    /* else: fall through to VGA text path */

    /* ── VGA text path ───────────────────────────────────────────── */
    bg_fill_screen_attr(ATTR_MODERN);
    for (int x = 0; x < VGA_WIDTH; x++) {
        bg_putc_attr(x, 0, (char)0xCD, ATTR_MODERN);
    }
    bg_center_text(0, " ASWD OS CRASH REPORT ", ATTR_MODERN);

    bg_puts_attr(2, 2,  "STOP CODE:  ", ATTR_MODERN);
    bg_puts_attr(14, 2, stop_code ? stop_code : "(unknown)", ATTR_MODERN);
    bg_puts_attr(2, 3,  "DETAIL:     ", ATTR_MODERN);
    bg_puts_attr(14, 3, detail ? detail : "", ATTR_MODERN);

    if (frame) {
        char buf[32];
        char vecbuf[16];
        const char *name = exception_name(frame->vector);

        bg_puts_attr(2, 5,  "VECTOR:     ", ATTR_MODERN);
        fmt_dec(vecbuf, frame->vector);
        bg_puts_attr(14, 5, vecbuf, ATTR_MODERN);
        bg_puts_attr(18, 5, "(", ATTR_MODERN);
        bg_puts_attr(19, 5, name, ATTR_MODERN);
        bg_puts_attr(19 + text_len(name), 5, ")", ATTR_MODERN);

        bg_puts_attr(2, 6,  "ERROR CODE:  ", ATTR_MODERN);
        fmt_hex32(buf, frame->error_code);
        bg_puts_attr(14, 6, buf, ATTR_MODERN);

        if (frame->vector == 14u) {
            uint32_t cr2 = 0;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            bg_puts_attr(2, 7,  "CR2:        ", ATTR_MODERN);
            fmt_hex32(buf, cr2);
            bg_puts_attr(14, 7, buf, ATTR_MODERN);
        }

        bg_puts_attr(2, 9,  "EIP:        ", ATTR_MODERN);
        fmt_hex32(buf, frame->eip);
        bg_puts_attr(14, 9, buf, ATTR_MODERN);
        bg_puts_attr(26, 9, "CS: ", ATTR_MODERN);
        fmt_hex32(buf, frame->cs);
        bg_puts_attr(30, 9, buf, ATTR_MODERN);
        bg_puts_attr(42, 9, "EFLAGS: ", ATTR_MODERN);
        fmt_hex32(buf, frame->eflags);
        bg_puts_attr(50, 9, buf, ATTR_MODERN);

        bg_puts_attr(2, 11, "EAX: ", ATTR_MODERN);
        fmt_hex32(buf, frame->eax);
        bg_puts_attr(7, 11, buf, ATTR_MODERN);
        bg_puts_attr(20, 11, "EBX: ", ATTR_MODERN);
        fmt_hex32(buf, frame->ebx);
        bg_puts_attr(25, 11, buf, ATTR_MODERN);
        bg_puts_attr(38, 11, "ECX: ", ATTR_MODERN);
        fmt_hex32(buf, frame->ecx);
        bg_puts_attr(43, 11, buf, ATTR_MODERN);
        bg_puts_attr(56, 11, "EDX: ", ATTR_MODERN);
        fmt_hex32(buf, frame->edx);
        bg_puts_attr(61, 11, buf, ATTR_MODERN);

        bg_puts_attr(2, 12, "ESI: ", ATTR_MODERN);
        fmt_hex32(buf, frame->esi);
        bg_puts_attr(7, 12, buf, ATTR_MODERN);
        bg_puts_attr(20, 12, "EDI: ", ATTR_MODERN);
        fmt_hex32(buf, frame->edi);
        bg_puts_attr(25, 12, buf, ATTR_MODERN);
        bg_puts_attr(38, 12, "EBP: ", ATTR_MODERN);
        fmt_hex32(buf, frame->ebp);
        bg_puts_attr(43, 12, buf, ATTR_MODERN);
        bg_puts_attr(56, 12, "ESP: ", ATTR_MODERN);
        fmt_hex32(buf, frame->esp);
        bg_puts_attr(61, 12, buf, ATTR_MODERN);
    }

    bg_puts_attr(2, 15, "This crash report captures more machine state than the", ATTR_MODERN);
    bg_puts_attr(2, 16, "legacy screen and is intended to make debugging easier.", ATTR_MODERN);
    bg_puts_attr(2, 18, "If this keeps happening, note the stop code and serial log.", ATTR_MODERN);

    com1_puts_raw("\r\n*** ASWD OS CRASH REPORT ***\r\nSTOP CODE: ");
    com1_puts_raw(stop_code ? stop_code : "(unknown)");
    com1_puts_raw("\r\nDETAIL: ");
    com1_puts_raw(detail ? detail : "");
    com1_puts_raw("\r\n");
    if (frame) {
        char buf[32];
        com1_puts_raw("VECTOR: ");
        fmt_dec(buf, frame->vector);
        com1_puts_raw(buf);
        com1_puts_raw(" (");
        com1_puts_raw(exception_name(frame->vector));
        com1_puts_raw(")\r\nERROR CODE: ");
        fmt_hex32(buf, frame->error_code);
        com1_puts_raw(buf);
        com1_puts_raw("\r\n");
        if (frame->vector == 14u) {
            uint32_t cr2 = 0;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            com1_puts_raw("CR2: ");
            fmt_hex32(buf, cr2);
            com1_puts_raw(buf);
            com1_puts_raw("\r\n");
        }
        com1_puts_raw("EIP: ");
        fmt_hex32(buf, frame->eip);
        com1_puts_raw(buf);
        com1_puts_raw(" CS: ");
        fmt_hex32(buf, frame->cs);
        com1_puts_raw(" EFLAGS: ");
        fmt_hex32(buf, frame->eflags);
        com1_puts_raw(buf);
        com1_puts_raw("\r\n");
        com1_puts_raw("EAX: ");
        fmt_hex32(buf, frame->eax);
        com1_puts_raw(buf);
        com1_puts_raw(" EBX: ");
        fmt_hex32(buf, frame->ebx);
        com1_puts_raw(buf);
        com1_puts_raw(" ECX: ");
        fmt_hex32(buf, frame->ecx);
        com1_puts_raw(buf);
        com1_puts_raw(" EDX: ");
        fmt_hex32(buf, frame->edx);
        com1_puts_raw(buf);
        com1_puts_raw("\r\nESI: ");
        fmt_hex32(buf, frame->esi);
        com1_puts_raw(buf);
        com1_puts_raw(" EDI: ");
        fmt_hex32(buf, frame->edi);
        com1_puts_raw(buf);
        com1_puts_raw(" EBP: ");
        fmt_hex32(buf, frame->ebp);
        com1_puts_raw(buf);
        com1_puts_raw(" ESP: ");
        fmt_hex32(buf, frame->esp);
        com1_puts_raw(buf);
        com1_puts_raw("\r\n");
    }
}

__attribute__((noreturn))
void bugcheck(const char *code, const char *msg) {
    if (g_style == BUGCHECK_STYLE_LEGACY) {
        legacy_display(code, msg, 0);
    } else {
        modern_display(code, msg, 0);
    }
    do_reboot_and_halt();
    __builtin_unreachable();
}

__attribute__((noreturn))
void bugcheck_ex(const exception_frame_t *frame) {
    static char stop_buf[64];
    char ec_buf[16];
    const char *name = frame ? exception_name(frame->vector) : "Unknown Exception";

    if (frame) {
        int pos = 0;
        const char *prefix = "EXCEPTION #";
        while (prefix[pos]) {
            stop_buf[pos] = prefix[pos];
            pos++;
        }
        if (frame->vector >= 10u) {
            stop_buf[pos++] = (char)('0' + (frame->vector / 10u));
        }
        stop_buf[pos++] = (char)('0' + (frame->vector % 10u));
        stop_buf[pos++] = ' ';
        stop_buf[pos++] = (char)0xF7;
        stop_buf[pos++] = ' ';
        for (int i = 0; name[i]; i++) {
            stop_buf[pos++] = name[i];
        }
        stop_buf[pos] = '\0';
        fmt_hex32(ec_buf, frame->error_code);
    } else {
        stop_buf[0] = '\0';
        ec_buf[0] = '\0';
    }

    if (g_style == BUGCHECK_STYLE_LEGACY) {
        legacy_display(stop_buf[0] ? stop_buf : "(unknown)", ec_buf, 0);
    } else {
        modern_display(stop_buf[0] ? stop_buf : "(unknown)", ec_buf, frame);
    }

    do_reboot_and_halt();
    __builtin_unreachable();
}

__attribute__((noreturn))
void panic(const char *code, const char *msg) {
    bugcheck(code, msg);
    __builtin_unreachable();
}
