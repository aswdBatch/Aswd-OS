#pragma once

#include <stdint.h>

void toast_push(const char *msg);

int toast_tick(uint32_t now);

void toast_draw(int screen_w, int screen_h, int taskbar_h);
