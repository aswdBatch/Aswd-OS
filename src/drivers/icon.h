#pragma once

#include <stdint.h>

typedef enum {
    ICON_NONE = 0,

    ICON_SYM_POWER,
    ICON_SYM_USER,
    ICON_SYM_SEARCH,
    ICON_SYM_LOGOUT,
    ICON_SYM_ADD_USER,
    ICON_SYM_CLOSE,
    ICON_SYM_MINIMIZE,
    ICON_SYM_MAXIMIZE,
    ICON_SYM_RESTORE,
    ICON_SYM_FOLDER,
    ICON_SYM_DOCUMENT,
    ICON_SYM_SETTINGS,

    ICON_APP_TERMINAL = 100,
    ICON_APP_FILES,
    ICON_APP_EDITOR,
    ICON_APP_OSINFO,
    ICON_APP_SETTINGS,
    ICON_APP_TASKMGR,
    ICON_APP_SNAKE,
    ICON_APP_NOTES,
    ICON_APP_STORE,
    ICON_APP_CALC,
    ICON_APP_BROWSER,
    ICON_APP_AXDOCS,
    ICON_APP_AXSTUDIO,
    ICON_APP_WORK180,
} icon_asset_id_t;

void icon_draw(int x, int y, int size, icon_asset_id_t id, uint32_t tint);
int  icon_best_variant_size(icon_asset_id_t id, int desired_size);
