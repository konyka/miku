#ifndef MIKU_ERROR_H
#define MIKU_ERROR_H

#include "miku_common.h"

#define MK_ERR_MSG_SIZE 256

typedef struct {
    int   code;
    char  msg[MK_ERR_MSG_SIZE];
} miku_error_t;

MIKU_API miku_error_t  miku_error_ok(void);
MIKU_API miku_error_t  miku_error_new(int code, const char *fmt, ...) MIKU_FORMAT(2, 3);
MIKU_API bool          miku_error_is_ok(miku_error_t err);
MIKU_API const char   *miku_error_msg(miku_error_t err);
MIKU_API int           miku_error_code(miku_error_t err);

#define MK_RETURN_IF_ERROR(err) do { if (!miku_error_is_ok(err)) return err; } while(0)
#define MK_GOTO_IF_ERROR(err, label) do { if (!miku_error_is_ok(err)) goto label; } while(0)

#endif
