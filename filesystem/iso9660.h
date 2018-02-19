#ifndef ISO9660_HEAD
#define ISO9660_HEAD

#include "system.h"
#include "storage/storage.h"
#include "memory/mem.h"
#include "filesystem/fs.h"

typedef struct ISO9660_TIME_REC
{
    u8 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
    int8_t timezone;
} __attribute__((packed)) iso9660_time_rec_t;

#define ISO9660_FLAG_HIDDEN 1
#define ISO9660_FLAG_DIR 2
#define ISO9660_FLAG_ASSOCIATED 4
#define ISO9660_FLAG_EXTENDED_INF 8
#define ISO9660_FLAG_EXTENDED_OWN 16
#define ISO9660_FLAG_FINAL 128

typedef struct ISO9660_DIR_ENTRY
{
    u8 length;
    u8 ext_length;
    u32 extent_start_lsb; //little endian
    u32 extent_start_msb; //big endian
    u32 extent_size_lsb;
    u32 extent_size_msb;
    struct ISO9660_TIME_REC record_time;
    u8 flags;
    u8 interleave_units;
	u8 interleave_gap;

	u16 volume_sequence_lsb;
	u16 volume_sequence_msb;

	u8 name_len;
    char name[];
} __attribute__((packed)) iso9660_dir_entry_t;

typedef struct ISO_9660_DATE_TIME
{
	u32 year;
	u16 month;
	u16 day;
	u16 hour;
	u16 minute;
	u16 second;
	u16 hundredths;
	int8_t timezone;
} __attribute__((packed)) iso9660_date_time_t;

typedef struct ISO9660_PRIMARY_VOLUME_DESCRIPTOR
{
    u8 type; //if primary volume descriptor, 0x01;
    u8 identifier[5]; //CD001
    u8 version; //0x1
    u8 unused_0; //0x0
    u8 system_id[32]; //ex: LINUX, ...
    u8 volume_id[32];
    u8 unused_1[8]; //0x0
    u32 volume_space_lsb;
    u32 volume_space_msb;
    u8 unused_2[32]; //0x0
    u16 volume_set_lsb;
    u16 volume_set_msb;
    u16 volume_sequence_lsb;
    u16 volume_sequence_msb;
    u16 logical_block_size_lsb;
    u16 logical_block_size_msb;
    u32 path_table_size_lsb;
    u32 path_table_size_msb;
    u32 l_path_table_lba;
    u32 opt_l_path_table_lba; //optional, 0x0 = don't exist
    u32 m_path_table_lba;
    u32 opt_m_path_table_lba; //optional, 0x0 = don't exist
    u8 root_directory[34];//struct ISO9660_DIR_ENTRY root_directory;
    u8 volume_set_id[128];
    u8 volume_publisher[128];
    u8 data_preparer[128];
    u8 application_id[128];
    u8 copyright_file_id[38];
    u8 abstract_file_id[36];
    u8 bibliographic_file_id[37];
    struct ISO_9660_DATE_TIME creation_date_time;
    struct ISO_9660_DATE_TIME modification_date_time;
    struct ISO_9660_DATE_TIME expiration_date_time;
    struct ISO_9660_DATE_TIME effective_date_time;
    u8 file_structure_version; //0x1
    u8 unused_3; //0x0
    u8 application_used[512];
    u8 reserved[653];
} __attribute__((packed)) iso9660_primary_volume_descriptor_t;

typedef struct ISO9660_PATH_TABLE_ENTRY
{
    u8 name_len;
    u8 extended_attribute_rec_len;
    u32 extent_loc; //lba
    u16 parent_dir; //path table index
    u8 name[];
} __attribute__((packed)) iso9660_path_table_entry_t;

typedef struct iso9660_node_specific
{
    u32 extent_start;
} iso9660_node_specific_t;

file_system_t* iso9660fs_init(block_device_t* drive);
fsnode_t* iso9660_open(fsnode_t* dir, char* name);
error_t iso9660fs_read_file(fd_t* fd, void* buffer, u64 count);

#endif