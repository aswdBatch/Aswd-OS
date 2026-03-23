#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
  INPUT_EVENT_NONE = 0,
  INPUT_EVENT_KEY,
  INPUT_EVENT_POINTER,
} input_event_type_t;

typedef struct {
  char ch;
  uint8_t pressed;
} input_key_event_t;

typedef struct {
  input_event_type_t type;
  union {
    input_key_event_t key;
    struct {
      int16_t dx, dy;
      int16_t x, y;
      uint8_t buttons;
      uint8_t changed;
      uint8_t pressed;
      uint8_t released;
      uint8_t source;
    } pointer;
  };
} input_event_t;

char input_getchar(void);
int  input_try_getchar(char *out);
int  input_try_get_event(input_event_t *out);
void input_readline(char *buf, size_t buf_size);
