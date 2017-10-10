#include "storage.h"

hdd_device_t* hd_devices[4] = {0};
u8 hd_devices_count = 0;
hdd_device_t* sd_devices[10] = {0};
u8 sd_devices_count = 0;

reader_device_t* reader_devices[4] = {0};
u8 reader_devices_count = 0;

void install_block_devices()
{
    kprintf("[STO] Installing block devices...");

    ata_install();

    vga_text_okmsg();
}

void hdd_read(u64 sector, u32 offset, u8* data, u64 count, hdd_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        ata_pio_read(sector, offset, data, count, (ata_device_t*) drive->device_struct);
    }
}

void hdd_write(u64 sector, u32 offset, u8* data, u64 count, hdd_device_t* drive)
{
    if(drive->device_type == ATA_DEVICE)
    {
        ata_pio_write(sector, offset, data, count, (ata_device_t*) drive->device_struct);
    }
}