#include "miku_error.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

miku_error_t miku_error_ok(void) {
    miku_error_t err;
    err.code = MK_OK;
    err.msg[0] = '\0';
    return err;
}

miku_error_t miku_error_new(int code, const char *fmt, ...) {
    miku_error_t err;
    err.code = code;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err.msg, MK_ERR_MSG_SIZE, fmt, ap);
        va_end(ap);
    } else {
        err.msg[0] = '\0';
    }
    return err;
}

bool miku_error_is_ok(miku_error_t err) {
    return err.code == MK_OK;
}

const char *miku_error_msg(miku_error_t err) {
    return err.msg[0] ? err.msg : "ok";
}

int miku_error_code(miku_error_t err) {
    return err.code;
}
