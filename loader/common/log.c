#include "common/log.h"
#include "common/string.h"
#include "common/format.h"

#include "video_services.h"

static enum log_level current_level = LOG_LEVEL_INFO;

enum log_level logger_set_level(enum log_level level)
{
    enum log_level prev = current_level;
    current_level = level;
    return prev;
}

#ifdef HYPER_SERIAL_LOG
static inline void out8(u16 port, u8 data)
{
    asm volatile("outb %0, %1" :: "a"(data), "Nd"(port));
}

static inline u8 in8(u16 port)
{
    u8 out_value = 0;
    asm volatile("inb %1, %0" : "=a"(out_value) : "Nd"(port) :);
    return out_value;
}
#endif

#ifdef HYPER_SERIAL_LOG
#define SERIAL_COM1 0x3F8
#endif

static void serial_init(void)
{
#ifdef HYPER_SERIAL_LOG
    // 115200 / 9600
    #define BAUD_DIVISOR 12
    #define INTERRUPT_ENABLE_REGISTER 1
    #define LINE_CONTROL_REGISTER 3
    #define DATA_REGISTER_BAUD_LO 0
    #define DATA_REGISTER_BAUD_HI 1

    #define SET_BAUD (1 << 7)
    // 00: 5; 01: 6; 10: 7; 11: 8
    #define DATA_WIDTH (0b11)
    // 0: 1; 1: 1.5/2
    #define STOP_BIT (0b0 << 2)
    // xx0: None; 001: Odd; 011: Even; 101: Mark; 111: Space
    #define PARITY_MODE (0b000 << 3)
    // No interrupts
    #define INTERRUPT_MODE (0b0000)

    out8(SERIAL_COM1 + LINE_CONTROL_REGISTER, SET_BAUD);
    out8(SERIAL_COM1 + DATA_REGISTER_BAUD_LO, (BAUD_DIVISOR << 8) >> 8);
    out8(SERIAL_COM1 + DATA_REGISTER_BAUD_HI, BAUD_DIVISOR >> 8);

    out8(SERIAL_COM1 + LINE_CONTROL_REGISTER, DATA_WIDTH | STOP_BIT | PARITY_MODE);

    out8(SERIAL_COM1 + INTERRUPT_ENABLE_REGISTER, INTERRUPT_MODE);
#endif
}

void write_serial(const char *msg, size_t len)
{
#ifdef HYPER_SERIAL_LOG
    #define LINE_STATUS_REGISTER 5
    #define STATUS_BUSY (1 << 5)
    while (len--) {
        // Poll busy flag
        while ((in8(SERIAL_COM1 + LINE_STATUS_REGISTER) & STATUS_BUSY) == 0)
            ;

        out8(SERIAL_COM1, *msg++);
    }
#else
    UNUSED(msg);
    UNUSED(len);
#endif
}

void logger_init(void)
{
    serial_init();
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


static void write_0xe9(const char *msg, size_t len)
{
#ifdef HYPER_E9_LOG
    while (len--)
        asm volatile("outb %0, %1" ::"a"(*msg++), "Nd"(0xE9));
#else
    UNUSED(msg);
    UNUSED(len);
#endif
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
    write_0xe9(log_buf, chars);
    write_serial(log_buf, chars);
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
