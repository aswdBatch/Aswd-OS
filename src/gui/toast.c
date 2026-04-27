#include "gui/toast.h"

#include <stdint.h>

#include "drivers/gfx.h"
#include "gui/theme.h"
#include "lib/string.h"

#define TOAST_MAX         8
#define TOAST_MSG_MAX     64
#define TOAST_LIFETIME    300u
#define TOAST_H           28
#define TOAST_PAD_X       12
#define TOAST_PAD_Y       6
#define TOAST_MARGIN      8

typedef struct {
    char     msg[TOAST_MSG_MAX];
    uint32_t expire_tick;
} toast_t;

static toast_t g_toasts[TOAST_MAX];
static int     g_count = 0;

void toast_push(const char *msg) {
    int slot = -1;

    for (int i = 0; i < g_count; i++) {
        if (str_ncmp(g_toasts[i].msg, msg, TOAST_MSG_MAX) == 0) {
            g_toasts[i].expire_tick += TOAST_LIFETIME;
            return;
        }
    }

    if (g_count < TOAST_MAX) {
        slot = g_count++;
    } else {

        for (int i = 0; i < TOAST_MAX - 1; i++) {
            g_toasts[i] = g_toasts[i + 1];
        }
        slot = TOAST_MAX - 1;
    }

    str_copy(g_toasts[slot].msg, msg, TOAST_MSG_MAX);

    g_toasts[slot].expire_tick = 0;
}

int toast_tick(uint32_t now) {

    for (int i = 0; i < g_count; i++) {
        if (g_toasts[i].expire_tick == 0) {
            g_toasts[i].expire_tick = now + TOAST_LIFETIME;
        }
    }

    int new_count = 0;
    for (int i = 0; i < g_count; i++) {
        if ((int32_t)(g_toasts[i].expire_tick - now) > 0) {
            if (new_count != i) g_toasts[new_count] = g_toasts[i];
            new_count++;
        }
    }
    g_count = new_count;
    return g_count > 0;
}

void toast_draw(int screen_w, int screen_h, int taskbar_h) {
    if (g_count <= 0) return;

    const th_metrics_t *tm = th_metrics();
    int font_px  = tm->font_small;
    int char_w   = gfx_font_char_width(FONT_ROLE_UI, font_px);
    int base_y   = screen_h - taskbar_h - TOAST_MARGIN;

    for (int i = g_count - 1; i >= 0; i--) {
        int msg_len = (int)str_len(g_toasts[i].msg);
        int tw = msg_len * char_w + TOAST_PAD_X * 2;
        if (tw > screen_w - TOAST_MARGIN * 2) tw = screen_w - TOAST_MARGIN * 2;

        int tx = screen_w - tw - TOAST_MARGIN;
        int ty = base_y - TOAST_H;

        gfx_fill_rect(tx + 2, ty + 2, tw, TOAST_H, gfx_rgb(0, 0, 0));

        gfx_fill_rect(tx, ty, tw, TOAST_H, gfx_rgb(28, 36, 52));

        gfx_fill_rect(tx, ty, 3, TOAST_H, gfx_rgb(56, 148, 245));

        gfx_draw_string_role(tx + TOAST_PAD_X, ty + TOAST_PAD_Y,
                             g_toasts[i].msg, FONT_ROLE_UI, font_px,
                             gfx_rgb(236, 242, 255), gfx_rgb(28, 36, 52));

        base_y = ty - TOAST_MARGIN;
    }
}
