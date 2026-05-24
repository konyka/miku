#include "miku_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static int          g_log_level  = MK_LOG_INFO;
static FILE        *g_log_file   = NULL;
static bool         g_initialized = false;

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\033[37m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[35m"
};

void miku_log_init(const char *log_dir, int min_level) {
    if (g_initialized) return;
    g_log_level = min_level;
    if (log_dir && log_dir[0]) {
        char path[512];
        snprintf(path, sizeof(path), "%s/miku.log", log_dir);
        g_log_file = fopen(path, "a");
    }
    g_initialized = true;
}

void miku_log_shutdown(void) {
    if (g_log_file) { fclose(g_log_file); g_log_file = NULL; }
    g_initialized = false;
}

void miku_log_set_level(int level) {
    g_log_level = level;
}

void miku_log_write(int level, const char *file, int line,
                     const char *fmt, ...) {
    if (level < g_log_level) return;

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *basename = strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    va_list ap;
    va_start(ap, fmt);

    fprintf(stderr, "%s[%s]" "\033[0m" " [%s] %s:%d: ",
            level_colors[level], level_strings[level], timebuf, basename, line);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);

    if (g_log_file) {
        fprintf(g_log_file, "[%s] [%s] %s:%d: ",
                level_strings[level], timebuf, basename, line);
        va_end(ap);
        va_start(ap, fmt);
        vfprintf(g_log_file, fmt, ap);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
    }

    va_end(ap);
}
