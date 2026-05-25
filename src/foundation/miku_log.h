#ifndef MIKU_LOG_H
#define MIKU_LOG_H

#include "miku_common.h"

typedef enum {
    MK_LOG_TRACE = 0,
    MK_LOG_DEBUG = 1,
    MK_LOG_INFO  = 2,
    MK_LOG_WARN  = 3,
    MK_LOG_ERROR = 4,
    MK_LOG_FATAL = 5
} miku_log_level_t;

MIKU_API void miku_log_init(const char *log_dir, int min_level);
MIKU_API void miku_log_write(int level, const char *file, int line,
                              const char *fmt, ...) MIKU_FORMAT(4, 5);
MIKU_API void miku_log_shutdown(void);
MIKU_API void miku_log_set_level(int level);
MIKU_API void miku_log_set_rotation(size_t max_bytes, int max_files);

#define MK_LOG_TRACE(fmt, ...) miku_log_write(MK_LOG_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define MK_LOG_DEBUG(fmt, ...) miku_log_write(MK_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define MK_LOG_INFO(fmt, ...)  miku_log_write(MK_LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define MK_LOG_WARN(fmt, ...)  miku_log_write(MK_LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define MK_LOG_ERROR(fmt, ...) miku_log_write(MK_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define MK_LOG_FATAL(fmt, ...) miku_log_write(MK_LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
