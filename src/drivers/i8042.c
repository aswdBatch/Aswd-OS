#include "drivers/i8042.h"

#include <stdint.h>

#include "cpu/ports.h"

#define I8042_DATA_PORT   0x60u
#define I8042_STATUS_PORT 0x64u
#define I8042_CMD_PORT    0x64u
#define I8042_SPINS       100000u

static void i8042_pause(void) {
    outb(0x80, 0);
}

int i8042_wait_input_clear(uint32_t spins) {
    if (spins == 0) spins = I8042_SPINS;
    while (spins-- > 0) {
        if (!(inb(I8042_STATUS_PORT) & 0x02u)) {
            return 1;
        }
        i8042_pause();
    }
    return 0;
}

int i8042_wait_output_full(uint32_t spins, uint8_t *status) {
    if (spins == 0) spins = I8042_SPINS;
    while (spins-- > 0) {
        uint8_t st = inb(I8042_STATUS_PORT);
        if (st & 0x01u) {
            if (status) *status = st;
            return 1;
        }
        i8042_pause();
    }
    return 0;
}

void i8042_flush_output(void) {
    for (uint32_t i = 0; i < 65536u; i++) {
        if (!(inb(I8042_STATUS_PORT) & 0x01u)) {
            break;
        }
        (void)inb(I8042_DATA_PORT);
        i8042_pause();
    }
}

int i8042_write_command(uint8_t cmd) {
    if (!i8042_wait_input_clear(0)) {
        return 0;
    }
    outb(I8042_CMD_PORT, cmd);
    return 1;
}

int i8042_write_data(uint8_t data) {
    if (!i8042_wait_input_clear(0)) {
        return 0;
    }
    outb(I8042_DATA_PORT, data);
    return 1;
}

int i8042_read_data(uint8_t *out, uint8_t *status, uint32_t spins) {
    uint8_t st;
    if (!i8042_wait_output_full(spins, &st)) {
        return 0;
    }
    if (out) {
        *out = inb(I8042_DATA_PORT);
    } else {
        (void)inb(I8042_DATA_PORT);
    }
    if (status) *status = st;
    return 1;
}

int i8042_read_typed(uint8_t want_aux, uint8_t *out, uint32_t spins) {
    if (spins == 0) spins = I8042_SPINS;
    while (spins-- > 0) {
        uint8_t st = inb(I8042_STATUS_PORT);
        if (st & 0x01u) {
            uint8_t data = inb(I8042_DATA_PORT);
            if (((st & 0x20u) != 0) == (want_aux != 0)) {
                if (out) *out = data;
                return 1;
            }
        }
        i8042_pause();
    }
    return 0;
}

int i8042_read_config(uint8_t *config) {
    if (!i8042_write_command(0x20u)) {
        return 0;
    }
    return i8042_read_data(config, 0, 0);
}

int i8042_write_config(uint8_t config) {
    if (!i8042_write_command(0x60u)) {
        return 0;
    }
    return i8042_write_data(config);
}

void i8042_disable_ports(void) {
    (void)i8042_write_command(0xADu);
    (void)i8042_write_command(0xA7u);
}

void i8042_enable_first_port(void) {
    (void)i8042_write_command(0xAEu);
}

void i8042_enable_second_port(void) {
    (void)i8042_write_command(0xA8u);
}

int i8042_write_device(uint8_t second_port, uint8_t data) {
    if (second_port) {
        if (!i8042_write_command(0xD4u)) {
            return 0;
        }
    }
    return i8042_write_data(data);
}
