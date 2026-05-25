#include "miku_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static int          g_log_level  = MK_LOG_INFO;
static FILE        *g_log_file   = NULL;
static bool         g_initialized = false;
static char         g_log_path[512] = {0};
static size_t       g_max_bytes  = 10 * 1024 * 1024;
static int          g_max_files  = 5;
static size_t       g_written    = 0;

static const char *level_strings[] = {
    "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char *level_colors[] = {
    "\033[37m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[35m"
};

static void rotate_log(void) {
    if (!g_log_file || g_max_files <= 0) return;
    fclose(g_log_file);
    g_log_file = NULL;
    for (int i = g_max_files - 1; i >= 1; i--) {
        char old_path[520], new_path[520];
        snprintf(old_path, sizeof(old_path), "%s.%d", g_log_path, i);
        snprintf(new_path, sizeof(new_path), "%s.%d", g_log_path, i + 1);
        rename(old_path, new_path);
    }
    char backup[520];
    snprintf(backup, sizeof(backup), "%s.1", g_log_path);
    rename(g_log_path, backup);
    g_log_file = fopen(g_log_path, "a");
    g_written = 0;
}

void miku_log_init(const char *log_dir, int min_level) {
    if (g_initialized) return;
    g_log_level = min_level;
    if (log_dir && log_dir[0]) {
        snprintf(g_log_path, sizeof(g_log_path), "%s/miku.log", log_dir);
        g_log_file = fopen(g_log_path, "a");
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

void miku_log_set_rotation(size_t max_bytes, int max_files) {
    g_max_bytes = max_bytes;
    g_max_files = max_files;
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
        g_written += 256;
        if (g_written >= g_max_bytes) rotate_log();
    }

    va_end(ap);
}
