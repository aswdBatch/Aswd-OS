#include "drivers/mouse.h"

#include <stdint.h>

#include "cpu/pic.h"
#include "cpu/ports.h"
#include "drivers/gfx.h"
#include "drivers/i8042.h"
#include "drivers/serial.h"

#define MOUSE_RING_SIZE 32

static volatile int g_abs_x, g_abs_y;
static volatile uint8_t g_btns;
static volatile uint8_t g_ps2_detected;
static volatile uint8_t g_last_source;
static volatile uint8_t g_first_irq_logged;
static volatile uint32_t g_irq_count;
static volatile uint32_t g_packet_errors;
static int g_screen_w = 800;
static int g_screen_h = 600;

/* 3-byte packet assembly */
static volatile uint8_t g_packet[3];
static volatile int g_packet_idx;

/* Ring buffer for events */
static volatile mouse_event_t g_ring[MOUSE_RING_SIZE];
static volatile int g_ring_head, g_ring_tail;

static void mouse_log(const char *msg) {
    serial_write("[mouse] ");
    serial_write(msg);
    serial_write("\n");
}

static void mouse_log_hex8(uint8_t value) {
    static const char k_hex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = k_hex[(value >> 4) & 0x0Fu];
    buf[1] = k_hex[value & 0x0Fu];
    buf[2] = '\0';
    serial_write(buf);
}

static int mouse_expect_ack(uint8_t cmd) {
    uint8_t reply = 0xFFu;

    if (!i8042_write_device(1, cmd)) {
        mouse_log("controller write timeout");
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!i8042_read_typed(1, &reply, 50000u)) {
            break;
        }
        if (reply == 0xFAu) {
            return 1;
        }
        if (reply == 0xFEu) {
            if (!i8042_write_device(1, cmd)) {
                mouse_log("controller resend write timeout");
                return 0;
            }
            continue;
        }
    }

    serial_write("[mouse] unexpected ack for cmd 0x");
    mouse_log_hex8(cmd);
    serial_write(": 0x");
    mouse_log_hex8(reply);
    serial_write("\n");
    return 0;
}

void mouse_set_bounds(int width, int height) {
    if (width > 0) g_screen_w = width;
    if (height > 0) g_screen_h = height;

    if (g_abs_x < 0) g_abs_x = 0;
    if (g_abs_y < 0) g_abs_y = 0;
    if (g_abs_x >= g_screen_w) g_abs_x = g_screen_w - 1;
    if (g_abs_y >= g_screen_h) g_abs_y = g_screen_h - 1;
}

int mouse_init(void) {
    uint8_t config = 0;

    if (gfx_width() > 0 && gfx_height() > 0) {
        mouse_set_bounds((int)gfx_width(), (int)gfx_height());
    }
    g_abs_x = g_screen_w / 2;
    g_abs_y = g_screen_h / 2;
    g_btns = 0;
    g_ps2_detected = 0;
    g_last_source = 0;
    g_first_irq_logged = 0;
    g_irq_count = 0;
    g_packet_errors = 0;
    g_packet_idx = 0;
    g_ring_head = 0;
    g_ring_tail = 0;

    __asm__ volatile("cli");
    i8042_disable_ports();
    i8042_flush_output();
    mouse_log("initializing PS/2 mouse");

    if (!i8042_read_config(&config)) {
        mouse_log("controller config read failed");
        i8042_enable_first_port();
        __asm__ volatile("sti");
        return 0;
    }

    config |= 0x03u;
    if (!i8042_write_config(config)) {
        mouse_log("controller config write failed");
        i8042_enable_first_port();
        __asm__ volatile("sti");
        return 0;
    }

    i8042_enable_first_port();
    i8042_enable_second_port();
    i8042_flush_output();

    if (!mouse_expect_ack(0xF6u)) {
        mouse_log("set defaults failed");
        i8042_enable_first_port();
        i8042_enable_second_port();
        __asm__ volatile("sti");
        return 0;
    }

    if (!mouse_expect_ack(0xF4u)) {
        mouse_log("enable reporting failed");
        i8042_enable_first_port();
        i8042_enable_second_port();
        __asm__ volatile("sti");
        return 0;
    }

    pic_clear_mask(2);
    pic_clear_mask(12);
    g_ps2_detected = 1;
    mouse_log("PS/2 mouse detected and IRQ12 enabled");
    __asm__ volatile("sti");
    return 1;
}

void mouse_irq_handler(void) {
    uint8_t st = inb(0x64);
    if (!(st & 0x20u)) {
        pic_send_eoi(12);
        return;
    }

    uint8_t data = inb(0x60);
    g_irq_count++;
    if (!g_first_irq_logged) {
        g_first_irq_logged = 1;
        mouse_log("first IRQ12 packet received");
    }

    if (g_packet_idx == 0 && !(data & 0x08u)) {
        g_packet_errors++;
        pic_send_eoi(12);
        return;
    }

    g_packet[g_packet_idx++] = data;

    if (g_packet_idx < 3) {
        pic_send_eoi(12);
        return;
    }

    g_packet_idx = 0;

    uint8_t flags = g_packet[0];
    if (flags & 0xC0u) {
        g_packet_errors++;
        pic_send_eoi(12);
        return;
    }

    int dx = (int)g_packet[1];
    int dy = (int)g_packet[2];
    if (flags & 0x10u) dx |= ~0xFF;
    if (flags & 0x20u) dy |= ~0xFF;
    dy = -dy;

    int nx = g_abs_x + dx;
    int ny = g_abs_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= g_screen_w) nx = g_screen_w - 1;
    if (ny >= g_screen_h) ny = g_screen_h - 1;
    g_abs_x = nx;
    g_abs_y = ny;

    uint8_t prev_btns = g_btns;
    uint8_t next_btns = flags & 0x07u;
    g_btns = next_btns;

    int next = (g_ring_head + 1) % MOUSE_RING_SIZE;
    if (next != g_ring_tail) {
        g_ring[g_ring_head].dx = (int16_t)dx;
        g_ring[g_ring_head].dy = (int16_t)dy;
        g_ring[g_ring_head].x = (int16_t)g_abs_x;
        g_ring[g_ring_head].y = (int16_t)g_abs_y;
        g_ring[g_ring_head].buttons = next_btns;
        g_ring[g_ring_head].changed = (uint8_t)(prev_btns ^ next_btns);
        g_ring[g_ring_head].pressed = (uint8_t)(next_btns & (uint8_t)~prev_btns);
        g_ring[g_ring_head].released = (uint8_t)(prev_btns & (uint8_t)~next_btns);
        g_ring[g_ring_head].source = MOUSE_SOURCE_PS2;
        g_last_source = MOUSE_SOURCE_PS2;
        g_ring_head = next;
    } else {
        g_packet_errors++;
    }

    pic_send_eoi(12);
}

void mouse_push_usb_event(int dx, int dy, uint8_t next_btns) {
    int nx, ny, next;
    uint8_t prev_btns;

    nx = g_abs_x + dx;
    ny = g_abs_y + dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx >= g_screen_w) nx = g_screen_w - 1;
    if (ny >= g_screen_h) ny = g_screen_h - 1;
    g_abs_x = nx;
    g_abs_y = ny;

    prev_btns = g_btns;
    g_btns = next_btns;

    next = (g_ring_head + 1) % MOUSE_RING_SIZE;
    if (next != g_ring_tail) {
        g_ring[g_ring_head].dx       = (int16_t)dx;
        g_ring[g_ring_head].dy       = (int16_t)dy;
        g_ring[g_ring_head].x        = (int16_t)g_abs_x;
        g_ring[g_ring_head].y        = (int16_t)g_abs_y;
        g_ring[g_ring_head].buttons  = next_btns;
        g_ring[g_ring_head].changed  = (uint8_t)(prev_btns ^ next_btns);
        g_ring[g_ring_head].pressed  = (uint8_t)(next_btns & (uint8_t)~prev_btns);
        g_ring[g_ring_head].released = (uint8_t)(prev_btns & (uint8_t)~next_btns);
        g_ring[g_ring_head].source   = MOUSE_SOURCE_USB;
        g_last_source = MOUSE_SOURCE_USB;
        g_ring_head = next;
    }
}

int mouse_poll(mouse_event_t *out) {
    if (g_ring_tail == g_ring_head) return 0;
    *out = (mouse_event_t)g_ring[g_ring_tail];
    g_ring_tail = (g_ring_tail + 1) % MOUSE_RING_SIZE;
    return 1;
}

int mouse_x(void) { return g_abs_x; }
int mouse_y(void) { return g_abs_y; }
uint8_t mouse_buttons(void) { return g_btns; }
int mouse_ps2_detected(void) { return (int)g_ps2_detected; }
uint32_t mouse_irq_count(void) { return g_irq_count; }
uint32_t mouse_packet_error_count(void) { return g_packet_errors; }
uint8_t mouse_last_source(void) { return g_last_source; }
