#pragma once

#include "Services.h"

class BIOSTTYServices : public TTYServices {
public:
    static constexpr size_t columns = 80;
    static constexpr size_t rows = 25;

    static BIOSTTYServices create();

    bool write(StringView, Color) override;

    Resolution resolution() const override { return { columns, rows }; }
    bool is_available() const override { return m_available; }

    void disable() { m_available = false; }

private:
    BIOSTTYServices();

    u16 as_attribute(Color);
    void scroll();

private:
    static constexpr ptr_t vga_address = 0xB8000;

    size_t m_x { 0 };
    size_t m_y { 0 };
    bool m_available { true };
};
