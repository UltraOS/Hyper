#include "log.h"
#include "format.h"
#include "string.h"
#include "video_services.h"

static enum log_level current_level = LOG_LEVEL_INFO;

enum log_level logger_set_level(enum log_level level)
{
    enum log_level prev = current_level;
    current_level = level;
    return prev;
}

static int extract_message_level(const char **msg_ptr)
{
    const char *msg = *msg_ptr;

    if (msg[0] != LOG_LEVEL_PREFIX[0] || !msg[1])
        return LOG_LEVEL_INFO;

    switch (msg[1]) {
    case '0' ... '3':
        *msg_ptr += 2;
        return msg[1] - '0';
    }

    return LOG_LEVEL_INFO;
}

static enum color get_color_for_level(enum log_level level)
{
    switch (level) {
    default:
    case LOG_LEVEL_INFO:
        return COLOR_GRAY;
    case LOG_LEVEL_WARN:
        return COLOR_YELLOW;
    case LOG_LEVEL_ERR:
        return COLOR_RED;
    }
}

void vprintlvl(enum log_level level, const char *msg, va_list vlist)
{
    static char log_buf[256];
    int chars;
    enum color col;

    if (unlikely(!msg))
        return;

    if (level < current_level)
        return;

    col = get_color_for_level(level);

    chars = vscnprintf(log_buf, sizeof(log_buf), msg, vlist);
    vs_write_tty(log_buf, chars, col);
}

void vprint(const char *msg, va_list vlist)
{
    enum log_level level;
    level = extract_message_level(&msg);

    vprintlvl(level, msg, vlist);
}

void print(const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprint(msg, vlist);
    va_end(vlist);
}

void printlvl(enum log_level level, const char *msg, ...)
{
    va_list vlist;
    va_start(vlist, msg);
    vprintlvl(level, msg, vlist);
    va_end(vlist);
}
