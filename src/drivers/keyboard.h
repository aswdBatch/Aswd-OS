#pragma once

#define KEY_UP    ((char)0x80)
#define KEY_DOWN  ((char)0x81)
#define KEY_LEFT  ((char)0x82)
#define KEY_RIGHT ((char)0x83)
#define KEY_DELETE ((char)0x84)
#define KEY_HOME   ((char)0x85)
#define KEY_END    ((char)0x86)
#define KEY_PAGEUP    ((char)0x87)
#define KEY_PAGEDOWN  ((char)0x88)
#define KEY_INSERT    ((char)0x89)
#define KEY_CTRL_HOME ((char)0x8A)
#define KEY_CTRL_END  ((char)0x8B)

void keyboard_init(void);
char keyboard_getchar(void);
int  keyboard_try_getchar(char *out);
int  keyboard_ps2_ready(void);
void keyboard_push_char(char c);   /* inject from USB HID keyboard */

void keyboard_irq_handler(void);
