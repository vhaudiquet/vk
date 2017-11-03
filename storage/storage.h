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
#define ATA_FLAG_MASTER 1
#define ATA_FLAG_LBA48 2
#define ATA_FLAG_ATAPI 4
typedef struct ata_device
{
	struct pci_device* controller;
	u16 base_port;
	u16 control_port;
	u16 bar4;
	u16 sectors_per_block;
	u8 irq;
	u8 flags;
} ata_device_t;
//global functions
void ata_install();
u8 ata_write_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);
u8 ata_read_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);

//ata driver internal functions
void ata_io_wait(ata_device_t* drive);
void ata_read_partitions(block_device_t* drive);
u8 ata_pio_poll_status(ata_device_t* drive);
u8 ata_pio_read_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);
u8 ata_dma_read_flexible(u64 sector, u32 offset, u8* data, u32 count, ata_device_t* drive);
u8 ata_pio_write_flexible(u64 sector, u32 offset, u8* data, u64 count, ata_device_t* drive);
u8 atapi_cmd_dma_read_28(u32 sector, ata_device_t* drive);

#define DATA_PORT(drive) drive->base_port
#define ERROR_PORT(drive) drive->base_port+1
#define SECTOR_COUNT_PORT(drive) drive->base_port+2
#define LBA_LOW_PORT(drive) drive->base_port+3
#define LBA_MID_PORT(drive) drive->base_port+4
#define LBA_HI_PORT(drive) drive->base_port+5
#define DEVICE_PORT(drive) drive->base_port+6
#define COMMAND_PORT(drive) drive->base_port+7
#define CONTROL_PORT(drive) drive->control_port

#define BYTES_PER_SECTOR 512
#define ATAPI_SECTOR_SIZE 2048

#endif