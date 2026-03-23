#pragma once

#include <stdint.h>

void     timer_init(uint32_t hz);
void     timer_tick(void);
uint32_t timer_get_ticks(void);
uint32_t timer_uptime_secs(void);
