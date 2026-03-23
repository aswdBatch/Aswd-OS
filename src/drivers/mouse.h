#pragma once

#include <stdint.h>

typedef enum {
    MOUSE_SOURCE_PS2 = 1,
    MOUSE_SOURCE_USB = 2,
} mouse_source_t;

typedef struct {
    int16_t dx, dy;
    int16_t x, y;
    uint8_t buttons;  /* bit 0=left, 1=right, 2=middle */
    uint8_t changed;
    uint8_t pressed;
    uint8_t released;
    uint8_t source;
} mouse_event_t;

int     mouse_init(void);  /* returns 1 if mouse found, 0 if not */
void    mouse_push_usb_event(int dx, int dy, uint8_t buttons);
void    mouse_set_bounds(int width, int height);
void    mouse_irq_handler(void);
int     mouse_poll(mouse_event_t *out);
int     mouse_x(void);
int     mouse_y(void);
uint8_t mouse_buttons(void);
int      mouse_ps2_detected(void);
uint32_t mouse_irq_count(void);
uint32_t mouse_packet_error_count(void);
uint8_t  mouse_last_source(void);
