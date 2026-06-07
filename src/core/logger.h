#ifndef WALLPAPER_LOGGER_H
#define WALLPAPER_LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR } log_level_t;

void logger_init(log_level_t level);
void log_msg(log_level_t level, const char* fmt, ...);

#define LOG_D(fmt, ...) log_msg(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) log_msg(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) log_msg(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) log_msg(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif  // WALLPAPER_LOGGER_H
