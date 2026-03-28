#pragma once

#include <stdint.h>

#include "drivers/icon.h"

#define CONTEXT_MENU_MAX_ITEMS 8

typedef enum {
    CONTEXT_MENU_STYLE_NORMAL = 0,
    CONTEXT_MENU_STYLE_DANGER,
} context_menu_style_t;

typedef struct {
    const char *label;
    icon_asset_id_t icon_id;
    context_menu_style_t style;
    void (*action)(void *userdata);
    void *userdata;
} context_menu_item_t;

void context_menu_show(int x, int y,
                       const context_menu_item_t *items, int count);
void context_menu_measure(const context_menu_item_t *items, int count,
                          int *out_w, int *out_h);
void context_menu_dismiss(void);
int  context_menu_active(void);
void context_menu_paint(void);
/* Returns 1 and calls action if an item was clicked, -1 if dismissed via
   right-click/escape.  mx/my are screen coords; pressed = left-button mask. */
int  context_menu_handle_pointer(int mx, int my, uint8_t pressed,
                                 uint8_t released);
