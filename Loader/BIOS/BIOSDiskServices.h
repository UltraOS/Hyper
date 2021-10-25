#pragma once

#include "Services.h"

class BIOSDiskServices final : public DiskServices {
public:
    static BIOSDiskServices create();

    Span<Disk> list_disks() override;
    bool read(u32 id, void* buffer, u64 sector, size_t count) override;

private:
    BIOSDiskServices(Disk* buffer, size_t capacity);

    void fetch_all_disks();

private:
    Disk* m_buffer { nullptr };
    size_t m_size { 0 };
};
