#include "Logger.h"

namespace logger {
    static VideoServices* g_backend = nullptr;
    static SerializeMode g_mode = SerializeMode::DEC;
    static Color g_color = Color::GRAY;

    VideoServices* set_backend(VideoServices* backend)
    {
        auto* previous = g_backend;
        g_backend = backend;
        return previous;
    }

    SerializeMode set_mode(SerializeMode m)
    {
        auto previous = g_mode;
        g_mode = m;
        return previous;
    }

    SerializeMode get_mode()
    {
        return g_mode;
    }

    Color set_color(Color c)
    {
        auto previous = g_color;
        g_color = c;
        return  previous;
    }

    void write(StringView string)
    {
        for (char c : string)
            asm volatile("outb %0, %1" ::"a"(c), "Nd"(0xE9));

        if (g_backend)
            g_backend->tty_write(string, g_color);
    }
}
