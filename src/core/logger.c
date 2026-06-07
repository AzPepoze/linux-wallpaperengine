#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static log_level_t min_level = LOG_LEVEL_DEBUG;

void logger_init(log_level_t level) {
    min_level = level;
}

void log_msg(log_level_t level, const char* fmt, ...) {
    if (level < min_level) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

    const char* level_strs[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    const char* level_colors[] = {"\033[94m", "\033[92m", "\033[93m", "\033[91m"};
    const char* reset = "\033[0m";

    printf("%s [%s%s%s] ", time_str, level_colors[level], level_strs[level], reset);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}
