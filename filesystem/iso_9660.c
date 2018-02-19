/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

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

static fsnode_t* iso9660_dirent_normalize_cache(iso9660_dir_entry_t* dirent, file_system_t* fs);

file_system_t* iso9660fs_init(block_device_t* drive)
{
    //allocating data struct
    file_system_t* tr = 
    #ifndef MEMLEAK_DBG
    kmalloc(sizeof(file_system_t));
    #else
    kmalloc(sizeof(file_system_t), "iso9660 file_system_t struct");
    #endif

    tr->drive = drive;
    tr->fs_type = FS_TYPE_ISO9660;
    tr->flags = 0 | FS_FLAG_CASE_INSENSITIVE | FS_FLAG_READ_ONLY;

    tr->inode_cache = 0;
    tr->inode_cache_size = 0;

    //reading primary volume descriptor
    iso9660_primary_volume_descriptor_t pvd;
    block_read_flexible(0x10, 0, (u8*) &pvd, sizeof(pvd), drive);

    //making root_dir file descriptor
    iso9660_dir_entry_t* rd = (iso9660_dir_entry_t*) pvd.root_directory;
    tr->root_dir = iso9660_dirent_normalize_cache(rd, tr);

    return tr;
}

fsnode_t* iso9660_open(fsnode_t* dir, char* name)
{
    file_system_t* fs = dir->file_system;
    iso9660_node_specific_t* spe = dir->specific;
    
    u32 length = (u32) dir->length;
    u8* dirent_data = kmalloc(length);

    //TEMP : this can't work, because on multiple sector using, sectors are padded with 0s
    error_t readop0 = block_read_flexible(spe->extent_start, 0, dirent_data, length, fs->drive);
    if(readop0 != ERROR_NONE) return 0;

    u32 offset = (u32) dirent_data;
    u32 index = 0;
    while(length)
    {
        iso9660_dir_entry_t* dirptr = (iso9660_dir_entry_t*) offset;
        if(dirptr->length == 0)
        {
            offset++;
            length--;
            continue;
        }

        if(strcfirst(dirptr->name, name) == dirptr->name_len) 
        {
            fsnode_t* tr = iso9660_dirent_normalize_cache(dirptr, fs);
            kfree(dirent_data);
            return tr;
        }
        
        offset+= dirptr->length;
        length-= dirptr->length;
        index++;
    }

    kfree(dirent_data);

    return 0;
}

error_t iso9660fs_read_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* file = fd->file;
    iso9660_node_specific_t* spe = file->specific;
    u64 offset = fd->offset;
    file_system_t* fs = file->file_system;

    return block_read_flexible(spe->extent_start, (u32) offset, buffer, count, fs->drive);
}

static fsnode_t* iso9660_dirent_normalize_cache(iso9660_dir_entry_t* dirent, file_system_t* fs)
{
    /* try to read node from the cache */
    list_entry_t* iptr = fs->inode_cache;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        iso9660_node_specific_t* espe = element->specific;
        if(espe->extent_start == dirent->extent_start_lsb) return element;
        iptr = iptr->next;
        isize--;
    }

    /* parse inode from dirent */
    fsnode_t* std_node = kmalloc(sizeof(fsnode_t));

    std_node->file_system = fs;
    std_node->length = dirent->extent_size_lsb;

    //parse time (check for year-100)
    std_node->creation_time = convert_to_std_time(dirent->record_time.second, dirent->record_time.minute, dirent->record_time.hour, dirent->record_time.day, dirent->record_time.month, (u8)(dirent->record_time.year));
    std_node->last_modification_time = std_node->creation_time;
    std_node->last_access_time = 0; //0 as NO value or -1 ? NO Value or creation_time ?

    //set attributes
    std_node->attributes = 0;
    if(dirent->flags & ISO9660_FLAG_HIDDEN) std_node->attributes |= FILE_ATTR_HIDDEN;
    if(dirent->flags & ISO9660_FLAG_DIR) std_node->attributes |= FILE_ATTR_DIR;

    //set specific
    iso9660_node_specific_t* spe = kmalloc(sizeof(iso9660_node_specific_t));
    spe->extent_start = dirent->extent_start_lsb;
    std_node->specific = spe;

    /* cache the object */
    if(!fs->inode_cache)
    {
        fs->inode_cache = kmalloc(sizeof(list_entry_t));
        fs->inode_cache->element = std_node;
        fs->inode_cache_size++;
    }
    else
    {
        list_entry_t* ptr = fs->inode_cache;
        list_entry_t* last = 0;
        u32 size = fs->inode_cache_size;
        while(ptr && size)
        {
            last = ptr;
            ptr = ptr->next;
            size--;
        }
        ptr = kmalloc(sizeof(list_entry_t));
        ptr->element = std_node;
        last->next = ptr;
        fs->inode_cache_size++;
    }

    return std_node;
}
