#include "auth/auth_gui.h"

#include <stdint.h>

#include "auth/auth_store.h"
#include "boot/bootui.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "users/users.h"

/* ── Layout ──────────────────────────────────────────────────────── */

#define HERO_W   260
#define PANEL_W  380
#define HDR_H     40
#define TILE_H    34
#define TILE_GAP   8
#define FIELD_H   (FONT_HEIGHT + 10)
#define PAD_X     20
#define PIN_MAX    8
#define NAME_MAX   8

/* ── Palette (kept for local use in hero/background) ─────────────── */

#define COL_BG         gfx_rgb(17,  24,  39)
#define COL_HERO_TOP   gfx_rgb(15,  50,  90)
#define COL_HERO_MID   gfx_rgb(12,  40,  75)
#define COL_HERO_BOT   gfx_rgb(10,  30,  60)
#define COL_PANEL_EDGE gfx_rgb(50,  62,  82)
#define COL_PANEL_BG   gfx_rgb(244, 247, 251)
#define COL_HDR_BG     gfx_rgb(27,  104, 188)
#define COL_HDR_TXT    gfx_rgb(255, 255, 255)
#define COL_LABEL      gfx_rgb(98,  111, 134)
#define COL_ERROR      gfx_rgb(180, 52,  72)
#define COL_HINT       gfx_rgb(131, 149, 179)

/* ── Login state ─────────────────────────────────────────────────── */

typedef struct {
    int  selected;
    char pin[PIN_MAX + 1];
    int  pin_len;
    char error[64];
    int  has_error;
} login_state_t;

/* ── Setup state ─────────────────────────────────────────────────── */

typedef struct {
    char name[NAME_MAX + 1];
    int  name_len;
    char msg[64];
    int  has_error;
} setup_state_t;

/* ── Helpers ─────────────────────────────────────────────────────── */

static int tile_count(void) { return 2 + users_count(); }

static const char *tile_label(int idx) {
    if (idx == 0) return "Guest";
    if (idx == 1) return "devacc";
    return users_name_at(idx - 2);
}

static void draw_hero(int sh) {
    int b = sh / 3;
    gfx_fill_rect(0, 0,   HERO_W, b,        COL_HERO_TOP);
    gfx_fill_rect(0, b,   HERO_W, b,        COL_HERO_MID);
    gfx_fill_rect(0, b*2, HERO_W, sh - b*2, COL_HERO_BOT);
    gfx_draw_string(30, sh / 3,      "AswdOS",              gfx_rgb(255, 255, 255), COL_HERO_MID);
    gfx_draw_string(30, sh / 3 + 24, "Sign in to continue", gfx_rgb(180, 200, 230), COL_HERO_MID);
}

/* ── Login screen ─────────────────────────────────────────────────── */

static void draw_login(const login_state_t *s) {
    int sw     = (int)gfx_width();
    int sh     = (int)gfx_height();
    int count  = tile_count();
    int panel_h = HDR_H + 16 + count * (TILE_H + TILE_GAP) + 80;
    int px     = HERO_W + (sw - HERO_W - PANEL_W) / 2;
    int py     = (sh - panel_h) / 2;
    int fw     = PANEL_W - PAD_X * 2;
    int tiles_y = py + HDR_H + 16;
    int i;

    /* Backgrounds */
    gfx_fill_rect(HERO_W, 0, sw - HERO_W, sh, COL_BG);
    draw_hero(sh);

    /* Panel border + background */
    gfx_fill_rect(px,     py,     PANEL_W,     panel_h,     COL_PANEL_EDGE);
    gfx_fill_rect(px + 2, py + 2, PANEL_W - 4, panel_h - 4, COL_PANEL_BG);

    /* Header */
    gfx_fill_rect(px + 2, py + 2, PANEL_W - 4, HDR_H, COL_HDR_BG);
    gfx_draw_string(px + 14, py + 12, "Sign In", COL_HDR_TXT, COL_HDR_BG);

    /* User tiles (vertical list) */
    for (i = 0; i < count; i++) {
        int ty = tiles_y + i * (TILE_H + TILE_GAP);
        th_draw_list_row(px + PAD_X, ty, fw, TILE_H, tile_label(i), i == s->selected);
    }

    /* PIN field (devacc only) */
    if (s->selected == 1) {
        int fy = tiles_y + count * (TILE_H + TILE_GAP) + 8;
        gfx_draw_string(px + PAD_X, fy, "PIN", COL_LABEL, COL_PANEL_BG);
        th_draw_field(px + PAD_X, fy + FONT_HEIGHT + 4, fw, s->pin, 1, 1);
    }

    /* Error */
    if (s->has_error && s->error[0]) {
        int ey = py + panel_h - 36;
        gfx_draw_string(px + PAD_X, ey, s->error, COL_ERROR, COL_PANEL_BG);
    }

    /* Hint */
    gfx_draw_string(px + PAD_X, py + panel_h - 18,
                    "Up/Down: select   Enter: confirm",
                    COL_HINT, COL_PANEL_BG);

    gfx_swap();
}

static void run_login_screen(void) {
    login_state_t s;
    char key;
    int count;

    mem_set(&s, 0, sizeof(s));
    s.selected = 0;
    draw_login(&s);

    for (;;) {
        if (!keyboard_try_getchar(&key)) continue;
        count = tile_count();

        if (key == KEY_UP && s.selected > 0) {
            s.selected--;
            s.pin[0] = '\0'; s.pin_len = 0; s.has_error = 0;
            draw_login(&s);
            continue;
        }
        if (key == KEY_DOWN && s.selected + 1 < count) {
            s.selected++;
            s.pin[0] = '\0'; s.pin_len = 0; s.has_error = 0;
            draw_login(&s);
            continue;
        }

        if (key == '\r' || key == '\n') {
            if (s.selected == 0) {
                auth_session_begin("Guest");
                return;
            }
            if (s.selected == 1) {
                if (s.pin_len == 0) {
                    str_copy(s.error, "Enter PIN.", sizeof(s.error));
                    s.has_error = 1;
                    draw_login(&s);
                    continue;
                }
                if (auth_verify_devacc(s.pin)) {
                    auth_session_begin(AUTH_DEVACC_NAME);
                    return;
                }
                str_copy(s.error, "Wrong PIN.", sizeof(s.error));
                s.has_error = 1;
                s.pin[0] = '\0'; s.pin_len = 0;
                draw_login(&s);
                continue;
            }
            /* Named user: instant login */
            {
                const char *name = tile_label(s.selected);
                if (name && users_switch(name)) {
                    auth_session_begin(name);
                    return;
                }
                str_copy(s.error, "Cannot open account.", sizeof(s.error));
                s.has_error = 1;
                draw_login(&s);
            }
            continue;
        }

        /* PIN editing (devacc only) */
        if (s.selected == 1) {
            if ((key == '\b' || key == (char)0x7F) && s.pin_len > 0) {
                s.pin[--s.pin_len] = '\0';
                s.has_error = 0;
                draw_login(&s);
            } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126
                       && s.pin_len < PIN_MAX) {
                s.pin[s.pin_len++] = key;
                s.pin[s.pin_len]   = '\0';
                s.has_error = 0;
                draw_login(&s);
            }
        }
    }
}

/* ── Setup screen (first boot: create admin) ─────────────────────── */

static void draw_setup(const setup_state_t *s) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int pw = 420, ph = 220;
    int px = (sw - pw) / 2;
    int py = (sh - ph) / 2;
    int fw = pw - PAD_X * 2;

    gfx_fill_rect(0, 0, sw, sh, COL_BG);

    /* Panel */
    gfx_fill_rect(px,     py,     pw,     ph,     COL_PANEL_EDGE);
    gfx_fill_rect(px + 2, py + 2, pw - 4, ph - 4, COL_PANEL_BG);

    /* Header */
    gfx_fill_rect(px + 2, py + 2, pw - 4, HDR_H, COL_HDR_BG);
    gfx_draw_string(px + 14, py + 12, "First-time Setup", COL_HDR_TXT, COL_HDR_BG);

    gfx_draw_string(px + PAD_X, py + HDR_H + 14,
                    "Create an admin account to continue.", COL_LABEL, COL_PANEL_BG);
    gfx_draw_string(px + PAD_X, py + HDR_H + 38, "Admin name", COL_LABEL, COL_PANEL_BG);
    th_draw_field(px + PAD_X, py + HDR_H + 56, fw, s->name, 1, 0);

    if (s->name_len > 0) {
        th_draw_button(px + PAD_X, py + HDR_H + 100, 80, 22, "Create", 0);
    }

    if (s->msg[0]) {
        uint32_t fg = s->has_error ? COL_ERROR : COL_HINT;
        gfx_draw_string(px + PAD_X, py + ph - 36, s->msg, fg, COL_PANEL_BG);
    }

    gfx_draw_string(px + PAD_X, py + ph - 18,
                    "A-Z / 0-9, max 8 chars   Enter: confirm",
                    COL_HINT, COL_PANEL_BG);

    gfx_swap();
}

static void run_setup_screen(void) {
    setup_state_t s;
    char key;

    mem_set(&s, 0, sizeof(s));
    draw_setup(&s);

    for (;;) {
        if (!keyboard_try_getchar(&key)) continue;

        if (key == '\b' || key == (char)0x7F) {
            if (s.name_len > 0) {
                s.name[--s.name_len] = '\0';
                s.msg[0] = '\0';
                draw_setup(&s);
            }
            continue;
        }

        if (key == '\r' || key == '\n') {
            if (s.name_len == 0) {
                str_copy(s.msg, "Enter a name first.", sizeof(s.msg));
                s.has_error = 1;
                draw_setup(&s);
                continue;
            }
            if (users_create(s.name, 1) && users_switch(s.name)) {
                return;
            }
            str_copy(s.msg, "Name invalid or already exists.", sizeof(s.msg));
            s.has_error = 1;
            s.name[0] = '\0'; s.name_len = 0;
            draw_setup(&s);
            continue;
        }

        if ((unsigned char)key >= 32 && (unsigned char)key <= 126 && s.name_len < NAME_MAX) {
            char ch = key;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
                s.name[s.name_len++] = ch;
                s.name[s.name_len]   = '\0';
                s.msg[0] = '\0'; s.has_error = 0;
                draw_setup(&s);
            }
        }
    }
}

/* ── Public entry point ──────────────────────────────────────────── */

void auth_gui_run(boot_target_t requested_target) {
    if (gfx_get_mode() != GFX_MODE_GRAPHICS) return;
    if (requested_target != BOOT_TARGET_NORMAL_GUI) return;
    if (users_needs_setup()) run_setup_screen();
    run_login_screen();
}
