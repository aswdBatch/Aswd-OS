#include "cpu/exceptions.h"

#include "cpu/bugcheck.h"

void exception_handler(const exception_frame_t *frame) {
    bugcheck_ex(frame);
}
