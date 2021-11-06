#pragma once

#include "Services.h"

class BIOSDiskServices final : public DiskServices {
public:
    static BIOSDiskServices create();

    Span<Disk> list_disks() override;

    bool read(void* handle, void* buffer, u64 offset, size_t bytes) override;
    bool read_blocks(void* handle, void* buffer, u64 sector, size_t blocks) override;

private:
    BIOSDiskServices(Disk* buffer, size_t capacity);

    Disk* disk_from_handle(void*);
    bool do_read(const Disk&, void* buffer, u64 offset, size_t bytes);

    void fetch_all_disks();

private:
    Disk* m_buffer { nullptr };
    size_t m_size { 0 };
};
