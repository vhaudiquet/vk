#ifndef STORAGE_HEAD
#define STORAGE_HEAD

#include "system.h"

//PARTITIONS
typedef struct partition_descriptor
{
	u32 start_lba;
	u32 length;
	u8 bootable;
	u8 system_id;
} partition_descriptor_t;
typedef struct partition_table_entry
{
	u8 bootable;

	//CHS partition start address - unused
	u8 start_head;
	u8 start_sector;
	u8 start_cylinder;

	u8 system_id;

	//CHS partition end address - unused
	u8 end_head;
	u8 end_sector;
	u8 end_cylinder;

	//LBA start address and lenght
	u32 start_lba;
	u32 length;
} __attribute__((packed)) partition_table_entry_t;

typedef struct master_boot_record
{
	u8 bootloader[436];
	u8 signature[10];
	partition_table_entry_t partitions[4];
	u16 magic_number;
} __attribute__((packed)) master_boot_record_t;

//BLOCK DEVICES ABSTRACTION LAYER
#define ATA_DEVICE 1
#define ATAPI_DEVICE 2
typedef struct hdd_device
{
    u8 device_type;
	void* device_struct;
	u32 device_size;
	partition_descriptor_t* partitions[4];
} hdd_device_t;
extern hdd_device_t* hd_devices[4];
extern u8 hd_devices_count;
extern hdd_device_t* sd_devices[10];
extern u8 sd_devices_count;
void hdd_read(u64 sector, u32 offset, u8* data, u64 count, hdd_device_t* drive);
void hdd_write(u64 sector, u32 offset, u8* data, u64 count, hdd_device_t* drive);

typedef struct reader_device
{
	u8 device_type;
	void* device_struct;
	u16 media_type;
} reader_device_t;
extern reader_device_t* reader_devices[4];
extern u8 reader_devices_count;

void install_block_devices();
void print_block_devices();

//ATA devices
typedef struct ata_device
{
	u32 base_port;
	u16 sectors_per_block;
	bool master;
	bool lba48_support;
} ata_device_t;
typedef struct atapi_device
{
	u32 base_port;
	bool master;
	bool lba48_support;
} atapi_device_t;
void ata_install();
void ata_pio_write(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);
void ata_pio_read(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);

#endif