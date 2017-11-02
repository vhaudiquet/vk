/*  
    This file is part of VK.

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

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

#define HARD_DISK_DRIVE 1
#define CD_DRIVE 2
#define USB_DRIVE 3

#define DISK_SUCCESS 0 //operation suceeded
#define DISK_FAIL_UNREACHABLE 1 //disk was not reachable (either media removed or temp bug)
#define DISK_FAIL_OUT 2 //block or size > disk capacity
#define DISK_FAIL_BUSY 3 //disk is already busy
#define DISK_FAIL_INTERNAL 4 //internal function failure

typedef struct block_device
{
	void* device_struct;
	u32 device_size;
	partition_descriptor_t* partitions[4];
	u8 device_type;
	u8 device_class;
	u8 partition_count;
} block_device_t;
extern block_device_t** block_devices;
extern u16 block_device_count;

u8 block_read_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive);
u8 block_write_flexible(u64 sector, u32 offset, u8* data, u64 count, block_device_t* drive);

void install_block_devices();

//ATA devices
typedef struct ata_device
{
	struct pci_device* controller;
	u16 base_port;
	u16 control_port;
	u16 bar4;
	u16 sectors_per_block;
	u8 irq;
	bool master;
	bool lba48_support;
} ata_device_t;
typedef struct atapi_device
{
	struct pci_device* controller;
	u16 base_port;
	u16 control_port;
	u16 bar4;
	u16 media_type;
	u8 irq;
	bool master;
	bool lba48_support;
} atapi_device_t;
void ata_install();
u8 ata_pio_write(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);
u8 ata_pio_read(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);

u8 ata_dma_read_28(u32 sector, u32 offset, u8* data, u32 count, ata_device_t* drive);

//GLOBAL ATA
//extern u32 DATA_PORT;
#define DATA_PORT(drive) drive->base_port
#define ERROR_PORT(drive) drive->base_port+1
#define SECTOR_COUNT_PORT(drive) drive->base_port+2
#define LBA_LOW_PORT(drive) drive->base_port+3
#define LBA_MID_PORT(drive) drive->base_port+4
#define LBA_HI_PORT(drive) drive->base_port+5
#define DEVICE_PORT(drive) drive->base_port+6
#define COMMAND_PORT(drive) drive->base_port+7
#define CONTROL_PORT(drive) drive->control_port

//#define ERROR_PORT DATA_PORT+1
//#define SECTOR_COUNT_PORT DATA_PORT+2
//#define LBA_LOW_PORT DATA_PORT+3
//#define LBA_MID_PORT DATA_PORT+4
//#define LBA_HI_PORT DATA_PORT+5
//#define DEVICE_PORT DATA_PORT+6 //talk to master or slave ++LBA_TOP if 48 bits
//#define COMMAND_PORT DATA_PORT+7
//#define CONTROL_PORT DATA_PORT+0x206

#endif