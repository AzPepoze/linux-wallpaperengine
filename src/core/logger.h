#ifndef WALLPAPER_LOGGER_H
#define WALLPAPER_LOGGER_H

#include <stdint.h>

typedef enum { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR } log_level_t;

#ifdef __cplusplus
#include <string>
#include <vector>

struct RuntimeLogEntry {
    log_level_t level = LOG_LEVEL_INFO;
    std::string tag;
    std::string message;
};

class Logger {
   public:
    Logger(const std::string& tag) : tag(tag) {}

    void debug(const char* fmt, ...);
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void error(const char* fmt, ...);

   private:
    std::string tag;
    void log(log_level_t level, const char* fmt, va_list args);
};

// Global core logger
extern Logger core_log;
extern Logger effect_log;

std::vector<RuntimeLogEntry> logger_recent_entries();
void logger_clear_recent_entries();
#endif

#ifdef __cplusplus
extern "C" {
#endif

void logger_init(log_level_t level);
void log_msg(log_level_t level, const char* tag, const char* fmt, ...);

// Legacy macros for C++ files (mapped to core_log)
#define LOG_D(fmt, ...) core_log.debug(fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) core_log.info(fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) core_log.warn(fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) core_log.error(fmt, ##__VA_ARGS__)

// Tagged macros
#define LOG_TAG_D(tag, fmt, ...) log_msg(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOG_TAG_I(tag, fmt, ...) log_msg(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define LOG_TAG_W(tag, fmt, ...) log_msg(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define LOG_TAG_E(tag, fmt, ...) log_msg(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif  // WALLPAPER_LOGGER_H
