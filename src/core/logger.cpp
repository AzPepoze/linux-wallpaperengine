#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static log_level_t min_level = LOG_LEVEL_DEBUG;

// Initialize global core logger
Logger core_log("CORE");

void logger_init(log_level_t level) {
    min_level = level;
}

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static void print_log(log_level_t level, const char* tag, const char* fmt, va_list args) {
    if (level < min_level) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", t);

    const char* level_strs[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    const char* level_colors[] = {"\033[94m", "\033[92m", "\033[93m", "\033[91m"};
    const char* reset = "\033[0m";

    const char* tag_colors[] = {"\033[91m", "\033[92m", "\033[93m", "\033[94m", "\033[95m", "\033[96m"};
    uint32_t hash = hash_string(tag);
    const char* tag_color = tag_colors[hash % 6];

    printf("%s [%s%s%s] [%s%s%s] ", time_str, level_colors[level], level_strs[level], reset, tag_color, tag, reset);
    vprintf(fmt, args);
    printf("\n");
}

void log_msg(log_level_t level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    print_log(level, tag, fmt, args);
    va_end(args);
}

// Logger Class Implementation
void Logger::log(log_level_t level, const char* fmt, va_list args) {
    print_log(level, tag.c_str(), fmt, args);
}

void Logger::debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LOG_LEVEL_DEBUG, fmt, args);
    va_end(args);
}

void Logger::info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LOG_LEVEL_INFO, fmt, args);
    va_end(args);
}

void Logger::warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LOG_LEVEL_WARN, fmt, args);
    va_end(args);
}

void Logger::error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}
