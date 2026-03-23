#pragma once

#include <stddef.h>

void        users_init(void);
const char *users_current(void);
int         users_count(void);
const char *users_name_at(int index);
int         users_has_active(void);
int         users_needs_setup(void);
int         users_current_is_admin(void);
int         users_create(const char *name, int make_admin);
int         users_switch(const char *name);
int         users_create_next(void);
void        users_logout(void);
void        users_home_path(char *out, size_t out_size);
