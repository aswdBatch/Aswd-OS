#include "usb/hid.h"

#include "drivers/keyboard.h"

static const char hid_usage_normal[128] = {
    0,    0,    0,    0,    'a',  'b',  'c',  'd',
    'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',
    'm',  'n',  'o',  'p',  'q',  'r',  's',  't',
    'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',
    '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',
    '\r', 0x1B, '\b', '\t', ' ',  '-',  '=',  '[',
    ']', '\\',  0,    ';', '\'',  '`',  ',',  '.',
    '/',  0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    '/',  '*',  '-',  '+',
    '\r', '1',  '2',  '3',  '4',  '5',  '6',  '7',
    '8',  '9',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
};

static const char hid_usage_shift[128] = {
    0,    0,    0,    0,    'A',  'B',  'C',  'D',
    'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',
    'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
    'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',
    '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',
    '\r', 0x1B, '\b', '\t', ' ',  '_',  '+',  '{',
    '}',  '|',  0,    ':',  '"',  '~',  '<',  '>',
    '?',  0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    '/',  '*',  '-',  '+',
    '\r', '1',  '2',  '3',  '4',  '5',  '6',  '7',
    '8',  '9',  '0',  '.',  0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
};

void usb_hid_boot_keyboard_process(const uint8_t *rpt, uint8_t prev[6]) {
    uint8_t mod;
    int shift;
    int ctrl;
    unsigned k;

    if (!rpt || !prev) {
        return;
    }

    mod   = rpt[0];
    shift = (mod & 0x22u) != 0;
    ctrl  = (mod & 0x11u) != 0;

    for (k = 2; k < 8; k++) {
        uint8_t code = rpt[k];
        int already = 0;
        unsigned j;

        if (code == 0 || code == 0x01) {
            continue;
        }

        for (j = 0; j < 6; j++) {
            if (prev[j] == code) {
                already = 1;
                break;
            }
        }
        if (already) {
            continue;
        }

        if (code == 0x4F) { keyboard_push_char(KEY_RIGHT);    continue; }
        if (code == 0x50) { keyboard_push_char(KEY_LEFT);     continue; }
        if (code == 0x51) { keyboard_push_char(KEY_DOWN);     continue; }
        if (code == 0x52) { keyboard_push_char(KEY_UP);       continue; }
        if (code == 0x4A) { keyboard_push_char(KEY_HOME);     continue; }
        if (code == 0x4D) { keyboard_push_char(KEY_END);      continue; }
        if (code == 0x4B) { keyboard_push_char(KEY_PAGEUP);   continue; }
        if (code == 0x4E) { keyboard_push_char(KEY_PAGEDOWN); continue; }
        if (code == 0x4C) { keyboard_push_char(KEY_DELETE);   continue; }
        if (code == 0x49) { keyboard_push_char(KEY_INSERT);   continue; }

        if (code >= 128) {
            continue;
        }
        {
            char c = shift ? hid_usage_shift[code] : hid_usage_normal[code];
            if (!c) {
                continue;
            }

            if (ctrl && c >= 'a' && c <= 'z') {
                c = (char)(c - 'a' + 1);
            } else if (ctrl && c >= 'A' && c <= 'Z') {
                c = (char)(c - 'A' + 1);
            }

            keyboard_push_char(c);
        }
    }

    for (k = 0; k < 6; k++) {
        prev[k] = rpt[k + 2];
    }
}
