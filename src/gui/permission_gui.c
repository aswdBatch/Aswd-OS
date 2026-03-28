#include "gui/permission_gui.h"

#include <stdint.h>

#include "auth/auth_store.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/theme.h"
#include "lib/string.h"

/* ── Layout ───────────────────────────────────────────────────────── */

#define DLG_W        420
#define DLG_H        270
#define PIN_MAX        8
#define BTN_W         90
#define BTN_H         28

/* ── Colors ───────────────────────────────────────────────────────── */

#define COL_BODY     gfx_rgb(244, 247, 251)
#define COL_TXT      gfx_rgb(24,  35,  50)
#define COL_TXT_DIM  gfx_rgb(100, 116, 139)
#define COL_ERR      gfx_rgb(180, 52,  72)
#define COL_WARN_BG  gfx_rgb(254, 249, 237)
#define COL_WARN_BR  gfx_rgb(212, 172, 60)

/* ── State ────────────────────────────────────────────────────────── */

typedef struct {
    char pin[PIN_MAX + 1];
    int  pin_len;
    char error[64];
    int  has_error;
} perm_state_t;

/* ── Draw ─────────────────────────────────────────────────────────── */

static void draw_dialog(const perm_state_t *s, const char *action_desc,
                        int dx, int dy)
{
    const th_metrics_t *tm = th_metrics();
    int bw = gfx_width();
    int bh = gfx_height();

    /* Darken is called once by the caller before the first draw. */
    (void)bw; (void)bh;

    /* Dialog box */
    th_draw_dialog(dx, dy, DLG_W, DLG_H, "Permission Required");

    int body_x = dx + 16;
    int body_y = dy + tm->header_h + 6;
    int body_w = DLG_W - 32;

    /* Warning-tinted banner */
    gfx_fill_rect(dx + 3, body_y, DLG_W - 6, 22, COL_WARN_BG);
    gfx_fill_rect(dx + 3, body_y + 22, DLG_W - 6, 1, COL_WARN_BR);
    th_draw_text(body_x, body_y + 3, "Do you want to allow this action?",
                 gfx_rgb(120, 80, 0), COL_WARN_BG, tm->font_body);
    body_y += 30;

    /* Action description */
    if (action_desc && action_desc[0]) {
        th_draw_text(body_x, body_y, "Action:", COL_TXT_DIM, COL_BODY, tm->font_body);
        th_draw_text(body_x + 64, body_y, action_desc, COL_TXT, COL_BODY, tm->font_body);
    }
    body_y += tm->font_body + 10;

    /* Separator */
    th_draw_separator(dx + 3, body_y, DLG_W - 6);
    body_y += 10;

    /* PIN prompt */
    th_draw_text(body_x, body_y, "Enter admin PIN to continue:", COL_TXT, COL_BODY, tm->font_body);
    body_y += tm->font_body + 6;

    th_draw_field(body_x, body_y, body_w, s->pin, 1 /* focused */, 1 /* masked */);
    body_y += tm->field_h + 8;

    /* Error message */
    if (s->has_error) {
        th_draw_text(body_x, body_y, s->error, COL_ERR, COL_BODY, tm->font_small);
    }

    /* Buttons at bottom of dialog */
    int btn_y  = dy + DLG_H - 3 - BTN_H - 10;
    int btn_ok = dx + DLG_W - 3 - BTN_W - 8 - BTN_W - 8;
    int btn_no = dx + DLG_W - 3 - BTN_W - 8;

    th_draw_button(btn_ok, btn_y, BTN_W, BTN_H, "Confirm", 0);
    /* Cancel button — grey */
    gfx_fill_rect(btn_no, btn_y, BTN_W, BTN_H, gfx_rgb(100, 116, 139));
    {
        int llen = 6; /* "Cancel" */
        int lx = btn_no + (BTN_W - llen * FONT_WIDTH) / 2;
        int ly = btn_y  + (BTN_H - tm->font_body) / 2;
        th_draw_text(lx, ly, "Cancel", gfx_rgb(255, 255, 255),
                     gfx_rgb(100, 116, 139), tm->font_body);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

int permission_prompt_run(const char *action_desc) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int dx = (sw - DLG_W) / 2;
    int dy = (sh - DLG_H) / 2;

    perm_state_t s;
    mem_set(&s, 0, sizeof(s));

    /* Darken the current backbuffer once, then draw dialog on top */
    gfx_darken_screen();
    draw_dialog(&s, action_desc, dx, dy);
    gfx_swap();

    for (;;) {
        char key;
        if (!keyboard_try_getchar(&key)) continue;

        if (key == 27) { /* Escape — cancel */
            return 0;
        }

        if (key == '\r' || key == '\n') { /* Enter — verify */
            s.pin[s.pin_len] = '\0';
            if (auth_verify_devacc(s.pin)) {
                return 1;
            }
            str_copy(s.error, "Incorrect PIN. Try again.", sizeof(s.error));
            s.has_error = 1;
            s.pin_len   = 0;
            mem_set(s.pin, 0, sizeof(s.pin));
            /* Redraw dialog only (backbuffer already darkened) */
            gfx_fill_rect(dx + 3, dy + 3, DLG_W - 6, DLG_H - 6, COL_BODY);
            draw_dialog(&s, action_desc, dx, dy);
            gfx_swap();
            continue;
        }

        if (key == '\b' || key == 127) { /* Backspace */
            if (s.pin_len > 0) {
                s.pin_len--;
                s.pin[s.pin_len] = '\0';
            }
        } else if (key >= 0x20 && key < 127 && s.pin_len < PIN_MAX) {
            s.pin[s.pin_len++] = key;
            s.pin[s.pin_len]   = '\0';
        } else {
            continue; /* ignore other keys without redraw */
        }

        s.has_error = 0;
        s.error[0]  = '\0';

        /* Redraw just the dialog body area */
        gfx_fill_rect(dx + 3, dy + 3, DLG_W - 6, DLG_H - 6, COL_BODY);
        draw_dialog(&s, action_desc, dx, dy);
        gfx_swap();
    }
}
