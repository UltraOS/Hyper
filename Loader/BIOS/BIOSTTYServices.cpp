#include "BIOSTTYServices.h"
#include "BIOSCall.h"

BIOSTTYServices BIOSTTYServices::create()
{
    return {};
}

BIOSTTYServices::BIOSTTYServices()
{
    RealModeRegisterState registers {};

    // 80x25 color text, https://stanislavs.org/helppc/int_10-0.html
    registers.eax = 0x03;

    // There's no way to check if this worked, so we're done.
    bios_call(0x10, &registers, &registers);

    // Disable cursor, https://stanislavs.org/helppc/int_10-1.html
    registers = {};
    registers.eax = 0x0100;
    registers.ecx = 0x2000;
    bios_call(0x10, &registers, &registers);
}

u16 BIOSTTYServices::as_attribute(Color color)
{
    switch (color)
    {
    default:
    case Color::WHITE:
        return 0x0F00;
    case Color::GRAY:
        return 0x0700;
    case Color::YELLOW:
        return 0x0E00;
    case Color::RED:
        return 0x0C00;
    case Color::BLUE:
        return 0x0900;
    case Color::GREEN:
        return 0x0A00;
    }
}

void BIOSTTYServices::scroll()
{
    auto* vga_memory = reinterpret_cast<volatile u16*>(vga_address);

    for (size_t y = 0; y < (rows - 1); ++y) {
        for (size_t x = 0; x < columns; ++x) {
            vga_memory[y * columns + x] = vga_memory[(y + 1) * columns + x];
        }
    }

    for (size_t x = 0; x < columns; ++x)
        vga_memory[(rows - 1) * columns + x] = ' ';
}

bool BIOSTTYServices::write(StringView string, Color color)
{
    if (!m_available)
        return false;

    auto* vga_memory = reinterpret_cast<volatile u16*>(vga_address);

    for (char c : string) {
        if (c == '\n') {
            m_y++;
            m_x = 0;
            continue;
        }

        if (c == '\t') {
            m_x += 4;
            continue;
        }

        if (m_x >= columns) {
            m_x = 0;
            m_y++;
        }

        if (m_y >= rows) {
            m_y--;
            scroll();
        }

        vga_memory[m_y * columns + m_x++] = as_attribute(color) | c;
    }

    return true;
}
