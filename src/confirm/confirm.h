#pragma once

typedef enum {
  CONFIRM_VETO = 0,
  CONFIRM_ACK = 1,
} confirm_result_t;

confirm_result_t confirm_prompt(const char *message);
confirm_result_t confirm_last_result(void);
int confirm_last_was_ack(void);

