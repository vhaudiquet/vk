#include "storage.h"
#include "memory/mem.h"

block_device_t** block_devices = 0;
u16 block_device_count = 0;

void install_block_devices()
{
    kprintf("[STO] Installing block devices...");

    block_devices = kmalloc(sizeof(void*)*10);
    ata_install();

    vga_text_okmsg();
}

void block_read_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        ata_pio_read(sector, offset, data, count, (ata_device_t*) drive->device_struct);
    }
}

void block_write_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        ata_pio_write(sector, offset, data, count, (ata_device_t*) drive->device_struct);
    }
}