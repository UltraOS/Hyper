#include "Logger.h"

namespace logger {
    static VideoServices* g_backend = nullptr;
    static Mode g_mode = Mode::DEC;

    VideoServices* set_backend(VideoServices* backend)
    {
        auto* previous = g_backend;
        g_backend = backend;
        return previous;
    }

    void set_mode(Mode m)
    {
        g_mode = m;
    }

    Mode get_mode()
    {
        return g_mode;
    }

    void log(Color color, StringView string)
    {
        if (g_backend)
            g_backend->tty_write(string, color);
    }
}
