#include "gui/snake_gui.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "lib/string.h"

#define GRID_W 20
#define GRID_H 20
#define CELL 15
#define MAX_SNAKE (GRID_W * GRID_H)

#define HUD_H 52
#define SNAKE_BASE_TICKS 11u
#define SNAKE_MIN_TICKS 4u
#define SNAKE_SPEED_STEP 4

#define COL_BG        gfx_rgb(17, 21, 30)
#define COL_GRID_A    gfx_rgb(30, 36, 48)
#define COL_GRID_B    gfx_rgb(34, 40, 54)
#define COL_BORDER    gfx_rgb(63, 72, 94)
#define COL_SNAKE     gfx_rgb(34, 197, 94)
#define COL_SNAKE_ALT gfx_rgb(22, 163, 74)
#define COL_SNAKE_HD  gfx_rgb(134, 239, 172)
#define COL_SNAKE_EYE gfx_rgb(11, 18, 27)
#define COL_APPLE     gfx_rgb(239, 68, 68)
#define COL_APPLE_GLO gfx_rgb(254, 202, 202)
#define COL_APPLE_LEF gfx_rgb(74, 222, 128)
#define COL_APPLE_STM gfx_rgb(120, 77, 44)
#define COL_HUD       gfx_rgb(23, 29, 42)
#define COL_TXT       gfx_rgb(233, 241, 255)
#define COL_DIM       gfx_rgb(147, 161, 186)
#define COL_WARN      gfx_rgb(248, 113, 113)
#define COL_GOOD      gfx_rgb(110, 231, 183)

static int g_win_id = -1;

/* grid: 0=empty, 1=snake, 2=apple */
static uint8_t g_grid[GRID_H][GRID_W];
static uint16_t g_snake[MAX_SNAKE];
static int g_head_i;
static int g_tail_i;
static int g_len;
static int g_dx;
static int g_dy;
static int g_ndx;
static int g_ndy;
static int g_score;
static int g_best_score;
static int g_dead;
static int g_started;
static int g_paused;
static int g_won;
static uint32_t g_last_tick;
static uint32_t g_rng;

static void draw_centered(int x, int w, int y, const char *text, uint32_t fg, uint32_t bg) {
    int len = (int)str_len(text);
    int tx = x + (w - len * 8) / 2;
    if (tx < x) tx = x;
    gfx_draw_string(tx, y, text, fg, bg);
}

static void snake_seed_rng(void) {
    uint32_t seed = g_rng ^ timer_get_ticks() ^ ((uint32_t)(g_best_score + 1) * 2654435761u);
    if (seed == 0) seed = 0xA53C9E17u;
    g_rng = seed;
}

static uint32_t snake_rand_next(void) {
    if (g_rng == 0) snake_seed_rng();
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng;
}

static uint32_t snake_step_ticks(void) {
    uint32_t boost = (uint32_t)(g_score / SNAKE_SPEED_STEP);
    uint32_t step = (boost >= (SNAKE_BASE_TICKS - SNAKE_MIN_TICKS))
        ? SNAKE_MIN_TICKS
        : (SNAKE_BASE_TICKS - boost);
    if (step < SNAKE_MIN_TICKS) step = SNAKE_MIN_TICKS;
    return step;
}

static int snake_speed_level(void) {
    return 1 + g_score / SNAKE_SPEED_STEP;
}

static int place_apple(void) {
    int start;

    if (g_len >= MAX_SNAKE) return 0;

    start = (int)(snake_rand_next() % (uint32_t)MAX_SNAKE);
    for (int i = 0; i < MAX_SNAKE; i++) {
        int idx = (start + i) % MAX_SNAKE;
        int ay = idx / GRID_W;
        int ax = idx % GRID_W;
        if (g_grid[ay][ax] == 0) {
            g_grid[ay][ax] = 2;
            return 1;
        }
    }

    return 0;
}

static void snake_prepare_round(void) {
    for (int i = 0; i < GRID_H * GRID_W; i++) {
        ((uint8_t *)g_grid)[i] = 0;
    }

    g_head_i = 2;
    g_tail_i = 0;
    g_len = 3;
    g_snake[0] = (uint16_t)(10 * GRID_W + 8);
    g_snake[1] = (uint16_t)(10 * GRID_W + 9);
    g_snake[2] = (uint16_t)(10 * GRID_W + 10);
    g_grid[10][8] = 1;
    g_grid[10][9] = 1;
    g_grid[10][10] = 1;

    g_dx = 1;
    g_dy = 0;
    g_ndx = 1;
    g_ndy = 0;
    g_score = 0;
    g_dead = 0;
    g_paused = 0;
    g_won = 0;
    g_last_tick = timer_get_ticks();

    snake_seed_rng();
    place_apple();
}

static void snake_start_round(void) {
    snake_prepare_round();
    g_started = 1;
}

static void snake_head_eyes(int px, int py) {
    int ex1 = px + 4;
    int ey1 = py + 4;
    int ex2 = px + CELL - 7;
    int ey2 = py + CELL - 7;

    if (g_dx > 0) {
        ex1 = px + CELL - 6;
        ey1 = py + 4;
        ex2 = px + CELL - 6;
        ey2 = py + CELL - 7;
    } else if (g_dx < 0) {
        ex1 = px + 4;
        ey1 = py + 4;
        ex2 = px + 4;
        ey2 = py + CELL - 7;
    } else if (g_dy < 0) {
        ex1 = px + 4;
        ey1 = py + 4;
        ex2 = px + CELL - 7;
        ey2 = py + 4;
    } else if (g_dy > 0) {
        ex1 = px + 4;
        ey1 = py + CELL - 6;
        ex2 = px + CELL - 7;
        ey2 = py + CELL - 6;
    }

    gfx_fill_rect(ex1, ey1, 2, 2, COL_SNAKE_EYE);
    gfx_fill_rect(ex2, ey2, 2, 2, COL_SNAKE_EYE);
}

static void snake_draw_apple(int px, int py) {
    gfx_fill_rect(px + 6, py + 2, 2, 3, COL_APPLE_STM);
    gfx_fill_rect(px + 8, py + 3, 4, 2, COL_APPLE_LEF);

    gfx_fill_rect(px + 4, py + 5, 7, 2, COL_APPLE);
    gfx_fill_rect(px + 3, py + 7, 9, 4, COL_APPLE);
    gfx_fill_rect(px + 4, py + 11, 7, 2, COL_APPLE);

    gfx_fill_rect(px + 5, py + 7, 2, 2, COL_APPLE_GLO);
    gfx_fill_rect(px + 4, py + 9, 2, 1, COL_APPLE_GLO);
}

static void snake_step(void) {
    int hx;
    int hy;
    int nx;
    int ny;
    int tx;
    int ty;
    int ate_apple;

    if (!(g_ndx == -g_dx && g_ndy == -g_dy)) {
        g_dx = g_ndx;
        g_dy = g_ndy;
    }

    hx = g_snake[g_head_i] % GRID_W;
    hy = g_snake[g_head_i] / GRID_W;
    nx = hx + g_dx;
    ny = hy + g_dy;

    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        g_dead = 1;
        return;
    }

    tx = g_snake[g_tail_i] % GRID_W;
    ty = g_snake[g_tail_i] / GRID_W;
    ate_apple = (g_grid[ny][nx] == 2);

    if (g_grid[ny][nx] == 1 && (nx != tx || ny != ty)) {
        g_dead = 1;
        return;
    }

    g_grid[ny][nx] = 1;
    g_head_i = (g_head_i + 1) % MAX_SNAKE;
    g_snake[g_head_i] = (uint16_t)(ny * GRID_W + nx);
    g_len++;

    if (ate_apple) {
        g_score++;
        if (g_score > g_best_score) g_best_score = g_score;
        if (!place_apple()) {
            g_dead = 1;
            g_won = 1;
        }
        return;
    }

    g_grid[ty][tx] = 0;
    g_tail_i = (g_tail_i + 1) % MAX_SNAKE;
    g_len--;
}

static int snake_on_tick(int win_id, uint32_t now) {
    uint32_t step_ticks;
    int dirty = 0;

    (void)win_id;

    if (!g_started || g_dead || g_paused) return 0;

    step_ticks = snake_step_ticks();
    while ((uint32_t)(now - g_last_tick) >= step_ticks) {
        g_last_tick += step_ticks;
        snake_step();
        dirty = 1;
        if (g_dead || g_paused) break;
        step_ticks = snake_step_ticks();
    }

    return dirty;
}

static void snake_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int grid_ox = r.x + (r.w - GRID_W * CELL) / 2;
    int grid_oy = r.y + HUD_H + 6;
    int grid_w = GRID_W * CELL;
    int grid_h = GRID_H * CELL;
    int hx = g_snake[g_head_i] % GRID_W;
    int hy = g_snake[g_head_i] / GRID_W;
    char line[48];
    char num_a[12];
    char num_b[12];
    char num_c[12];

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);

    gfx_fill_rect(r.x, r.y, r.w, HUD_H, COL_HUD);
    gfx_fill_rect(r.x, r.y + HUD_H, r.w, 1, COL_BORDER);

    if (!g_started) {
        u32_to_dec((uint32_t)g_best_score, num_a, sizeof(num_a));
        draw_centered(r.x, r.w, r.y + 6, "SNAKE", COL_GOOD, COL_HUD);
        line[0] = '\0';
        str_copy(line, "Best ", sizeof(line));
        str_cat(line, num_a, sizeof(line));
        draw_centered(r.x, r.w, r.y + 20, line, COL_TXT, COL_HUD);
        draw_centered(r.x, r.w, r.y + 34, "Enter/Space start  WASD or arrows steer", COL_DIM, COL_HUD);
    } else if (g_paused) {
        u32_to_dec((uint32_t)g_score, num_a, sizeof(num_a));
        u32_to_dec((uint32_t)g_best_score, num_b, sizeof(num_b));
        line[0] = '\0';
        str_copy(line, "Score ", sizeof(line));
        str_cat(line, num_a, sizeof(line));
        str_cat(line, "  Best ", sizeof(line));
        str_cat(line, num_b, sizeof(line));
        draw_centered(r.x, r.w, r.y + 6, "PAUSED", COL_TXT, COL_HUD);
        draw_centered(r.x, r.w, r.y + 20, line, COL_DIM, COL_HUD);
        draw_centered(r.x, r.w, r.y + 34, "Space/P resume  R restart", COL_DIM, COL_HUD);
    } else if (g_dead) {
        u32_to_dec((uint32_t)g_score, num_a, sizeof(num_a));
        u32_to_dec((uint32_t)g_best_score, num_b, sizeof(num_b));
        draw_centered(r.x, r.w, r.y + 6, g_won ? "YOU WIN" : "GAME OVER",
                      g_won ? COL_GOOD : COL_WARN, COL_HUD);
        line[0] = '\0';
        str_copy(line, "Score ", sizeof(line));
        str_cat(line, num_a, sizeof(line));
        str_cat(line, "  Best ", sizeof(line));
        str_cat(line, num_b, sizeof(line));
        draw_centered(r.x, r.w, r.y + 20, line, COL_TXT, COL_HUD);
        draw_centered(r.x, r.w, r.y + 34, "Enter/Space/R play again", COL_DIM, COL_HUD);
    } else {
        u32_to_dec((uint32_t)g_score, num_a, sizeof(num_a));
        u32_to_dec((uint32_t)g_best_score, num_b, sizeof(num_b));
        u32_to_dec((uint32_t)snake_speed_level(), num_c, sizeof(num_c));
        line[0] = '\0';
        str_copy(line, "Score ", sizeof(line));
        str_cat(line, num_a, sizeof(line));
        str_cat(line, "  Best ", sizeof(line));
        str_cat(line, num_b, sizeof(line));
        str_cat(line, "  Lv ", sizeof(line));
        str_cat(line, num_c, sizeof(line));
        draw_centered(r.x, r.w, r.y + 10, line, COL_TXT, COL_HUD);
        draw_centered(r.x, r.w, r.y + 28, "WASD/arrows move  P pause  R restart", COL_DIM, COL_HUD);
    }

    gfx_fill_rect(grid_ox - 2, grid_oy - 2, grid_w + 4, grid_h + 4, COL_BORDER);
    gfx_fill_rect(grid_ox, grid_oy, grid_w, grid_h, COL_BG);

    for (int gy = 0; gy < GRID_H; gy++) {
        for (int gx = 0; gx < GRID_W; gx++) {
            int px = grid_ox + gx * CELL;
            int py = grid_oy + gy * CELL;
            uint8_t cell = g_grid[gy][gx];
            uint32_t base = ((gx + gy) & 1) ? COL_GRID_A : COL_GRID_B;

            gfx_fill_rect(px, py, CELL - 1, CELL - 1, base);

            if (cell == 1) {
                uint32_t body = ((gx + gy) & 1) ? COL_SNAKE_ALT : COL_SNAKE;
                int is_head = (gx == hx && gy == hy);
                gfx_fill_rect(px + 1, py + 1, CELL - 3, CELL - 3, is_head ? COL_SNAKE_HD : body);
                if (is_head) snake_head_eyes(px, py);
            } else if (cell == 2) {
                snake_draw_apple(px, py);
            }
        }
    }
}

static void snake_set_direction(int dx, int dy) {
    g_ndx = dx;
    g_ndy = dy;
}

static void snake_key(int win_id, char key) {
    (void)win_id;

    if (key == 0x11) {
        gui_window_close(g_win_id);
        return;
    }

    if (!g_started) {
        if (key == ' ' || key == '\r' || key == '\n') {
            snake_start_round();
        }
        return;
    }

    if (g_dead) {
        if (key == ' ' || key == '\r' || key == '\n' || key == 'r' || key == 'R') {
            snake_start_round();
        }
        return;
    }

    if (key == 'r' || key == 'R') {
        snake_start_round();
        return;
    }

    if (key == 'p' || key == 'P' || key == ' ') {
        g_paused = !g_paused;
        g_last_tick = timer_get_ticks();
        return;
    }

    if (key == '\r' || key == '\n') {
        if (g_paused) {
            g_paused = 0;
            g_last_tick = timer_get_ticks();
        }
        return;
    }

    if (key == KEY_UP || key == 'w' || key == 'W') {
        snake_set_direction(0, -1);
        return;
    }
    if (key == KEY_DOWN || key == 's' || key == 'S') {
        snake_set_direction(0, 1);
        return;
    }
    if (key == KEY_LEFT || key == 'a' || key == 'A') {
        snake_set_direction(-1, 0);
        return;
    }
    if (key == KEY_RIGHT || key == 'd' || key == 'D') {
        snake_set_direction(1, 0);
        return;
    }
}

static void snake_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void snake_gui_launch(void) {
    gui_window_t *w;
    gui_rect_t rect;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(GRID_W * CELL + 24, GRID_H * CELL + HUD_H + 32, &rect);
    g_win_id = gui_window_create("Snake", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;

    g_started = 0;
    snake_prepare_round();

    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_SNAKE;
    w->on_paint = snake_paint;
    w->on_tick = snake_on_tick;
    w->on_key = snake_key;
    w->on_close = snake_close;
}
