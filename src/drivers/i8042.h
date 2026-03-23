#pragma once

#include <stdint.h>

int  i8042_wait_input_clear(uint32_t spins);
int  i8042_wait_output_full(uint32_t spins, uint8_t *status);
void i8042_flush_output(void);

int  i8042_write_command(uint8_t cmd);
int  i8042_write_data(uint8_t data);
int  i8042_read_data(uint8_t *out, uint8_t *status, uint32_t spins);
int  i8042_read_typed(uint8_t want_aux, uint8_t *out, uint32_t spins);

int  i8042_read_config(uint8_t *config);
int  i8042_write_config(uint8_t config);

void i8042_disable_ports(void);
void i8042_enable_first_port(void);
void i8042_enable_second_port(void);
int  i8042_write_device(uint8_t second_port, uint8_t data);
