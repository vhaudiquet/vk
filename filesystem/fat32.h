/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

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
#ifndef FAT32_HEAD
#define FAT32_HEAD

#include "system.h"
#include "storage/storage.h"
#include "memory/mem.h"
#include "filesystem/fs.h"

typedef struct BPB
{
	u8 jmp_code[3]; //JMP instruction to actual code, to avoid trying to execute data that isnt code
	char prgm_name[8]; //Name of the program that formatted disk
	u16 bytes_per_sector;
	u8 sectors_per_cluster;
	u16 reserved_sectors; //Number of reserved sectors, including boot sector
	u8 fats_number; // Number of FATs on the disk (backup copies)
	u16 root_dir_size; //Root dir size in ENTRY number (how many entries) (unused NOW) (osdev: number of directories)
	u16 total_sectors; //if total_sectors = 0, there are more than 65535 sectors, and the value is stored in "large_total_sectors"
	u8 disk_type; //(media type : HARD_DRIVE, FLOPPY_DISK, ...)
	u16 old_fat_size; //Sectors per FAT ; FAT12/FAT16 only (unused value)
	u16 sectors_per_track; //CHS addressing, unused now
	u16 heads; //same
	u32 hidden_sectors;
	u32 large_total_sectors; //This value is set if total_sectors = 0
	//FAT32 only PART
	u32 fat_size; //Sectors per FAT
	u16 flags;
	u16 fat_version_number; //first 8 bits = Major version, last 8bits = minor version
	u32 root_directory_cluster;
	u16 file_system_infos;
	u16 backup_boot_sector;
	char reserved[12];
	u8 drive_number; //Same as bios int 0x13 (0x00 = floppy disk, 0x80 = hard disk)
	u8 win_nt_flags; //Reserved
	u8 signature; //0x29 (default) or 0x28
	u32 serial_number;
	char volume_name[11];
	char system_id_string[8]; // = "FAT32"
	//offset 90 to 420 : boot code
	//offset 510 to 512 : 0xAA55 (bootable partition signature)
} __attribute__((packed)) bpb_t;

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20
#define FAT_ATTR_LFN (FAT_ATTR_READ_ONLY|FAT_ATTR_HIDDEN|FAT_ATTR_SYSTEM|FAT_ATTR_VOLUME_ID)

#define FAT_NAME_MAX 13

typedef struct FAT32_DIR_ENTRY
{
	u8 name[8]; //if name[0] (== 0x00 : end of directory) (== 0x05 : name[0] = 0xE5) (== 0xE5 : file deleted) (== 0x2E '.' ou '..')
	u8 extension[3];
	u8 attributes;
	u8 nt_reserved;
	u8 creation_time_tmilli; // ??? Creation time / 10 ms (between 0 and 199)
	u16 creation_time; //bits 15-11 : hours, 10-5 : minutes, 4-0 : seconds/2
	u16 creation_date; //bits 15-9 : year (0=1980, 127=2107), 8-5: month (J = 1), 4-0 : day
	u16 last_access_date; //same
	u16 first_cluster_high; //the high 16 bits of this entry's first cluster number (FAT 32 only)
	u16 last_modification_time;
	u16 last_modification_date;
	u16 first_cluster_low; //the low 16 bits of this entry's first cluster number
	u32 file_size; //in bytes
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct LFN_ENTRY
{
	u8 order;
	u16 firstn[5];
	u8 attributes;
	u8 type;
	u8 checksum;
	u16 nextn[6];
	u16 zero;
	u16 lastn[2];
} __attribute__((packed)) lfn_entry_t;

typedef struct fat32_node_specific
{
    u32 cluster;
	u32 dir_cluster;
} fat32_node_specific_t;

typedef struct fat32fs_specific
{
    struct BPB* bpb;
    u32 bpb_offset;
    u32* fat_table;
} fat32fs_specific_t;

file_system_t* fat32fs_init(block_device_t* drive, u8 partition);
fsnode_t* fat32_open(fsnode_t* dir, char* name);
error_t fat32_list_dir(list_entry_t* tr, fsnode_t* dir, u32* size);
error_t fat32_read_file(fd_t* fd, void* buffer, u64 count);
error_t fat32_write_file(fd_t* fd, void* buffer, u64 count);
error_t fat32_unlink(char* file_name, fsnode_t* dir);
error_t fat32_rename(fsnode_t* src_file, char* src_file_name, char* new_file_name, fsnode_t* dir);
fsnode_t* fat32_create_file(fsnode_t* dir, char* name, u8 attributes);

#endif