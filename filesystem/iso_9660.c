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
	u8 year[4];
	u8 month[2];
	u8 day[2];
	u8 hour[2];
	u8 minute[2];
	u8 second[2];
	u8 hundredths[2];
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

static void iso9660_get_fd(file_descriptor_t* dest, iso9660_dir_entry_t* dirent, file_descriptor_t* parent, file_system_t* fs);

file_system_t* iso9660fs_init(block_device_t* drive)
{
    //allocating data struct
    file_system_t* tr = kmalloc(sizeof(file_system_t));
    tr->drive = drive;
    tr->fs_type = FS_TYPE_ISO9660;
    tr->flags = 0 | FS_FLAG_CASE_INSENSITIVE;

    //reading primary volume descriptor
    iso9660_primary_volume_descriptor_t pvd;
    block_read_flexible(0x10, 0, (u8*) &pvd, sizeof(pvd), drive);

    //making root_dir file descriptor
    iso9660_dir_entry_t* rd = (iso9660_dir_entry_t*) pvd.root_directory;
    iso9660_get_fd(&tr->root_dir, rd, 0, tr);
    kfree(tr->root_dir.name);
    tr->root_dir.name = 0;

    return tr;
}

void iso9660fs_close(file_system_t* fs)
{
    if(fs->fs_type != FS_TYPE_ISO9660) return;
    kfree(fs);
}

list_entry_t* iso9660fs_read_dir(file_descriptor_t* dir, u32* size)
{
    file_system_t* fs = dir->file_system;
    u64 lba = dir->fsdisk_loc;
    u32 length = (u32) dir->length;
    u8* dirent_data = kmalloc(length);

    //TEMP : this can't work, because on multiple sector using, sectors are padded with 0s
    if(block_read_flexible(lba, 0, dirent_data, length, fs->drive) != DISK_SUCCESS)
        return 0;
    
    list_entry_t* tr = kmalloc(sizeof(list_entry_t));
    list_entry_t* ptr = tr;
    *size = 0;

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
        
        file_descriptor_t* fd = kmalloc(sizeof(file_descriptor_t));
        
        if(index == 0)
        {
            //current dir (.)
            fd->name = 0;
            fd_copy(fd, dir);
            fd->name = kmalloc(2); *(fd->name+1) = 0; strcpy(fd->name, ".");
        }
        else if(index == 1)
        {
            //parent dir (..)
            fd->name = 0;
            if(dir->parent_directory) 
                fd_copy(fd, dir->parent_directory); 
            else 
                fd_copy(fd, dir); 
            
            fd->name = kmalloc(3); *(fd->name+2) = 0; strcpy(fd->name, "..");
        }
        else
            iso9660_get_fd(fd, dirptr, dir, fs);
        
        //kprintf("%s\n", fd->name);
        ptr->element = fd;
        ptr->next = kmalloc(sizeof(list_entry_t));
        ptr = ptr->next;
        offset+= dirptr->length;
        length-= dirptr->length;
        (*size)++;
        index++;
    }
    kfree(ptr);

    return tr;
}

u8 iso9660fs_read_file(file_descriptor_t* file, void* buffer, u64 count)
{
    u64 offset = file->offset;

    file_system_t* fs = file->file_system;
    u64 lba = file->fsdisk_loc;

    while(offset > 2048) {offset -= 2048; lba++;}

    if(block_read_flexible(lba, (u32) offset, buffer, count, fs->drive) != DISK_SUCCESS)
        return 1;
    
    return 0;
}

static void iso9660_get_fd(file_descriptor_t* dest, iso9660_dir_entry_t* dirent, file_descriptor_t* parent, file_system_t* fs)
{
    //copy name
    dest->name = kmalloc((u32) dirent->name_len+1);
    *(dest->name+dirent->name_len) = 0;
    strncpy(dest->name, dirent->name, dirent->name_len);
    if(dirent->name_len > 2) if(*(dest->name+dirent->name_len-2) == ';') *(dest->name+dirent->name_len-2) = 0;
    if(dirent->name_len > 1) {u32 len = strlen(dest->name); if(*(dest->name+len-1) == '.') *(dest->name+len-1) = 0;}
    //set infos
    dest->file_system = fs;
    dest->parent_directory = parent;
    dest->fsdisk_loc = dirent->extent_start_lsb;
    dest->length = dirent->extent_size_lsb;
    dest->offset = 0;

    //set attributes
    dest->attributes = 0;
    if(dirent->flags & ISO9660_FLAG_HIDDEN) dest->attributes |= FILE_ATTR_HIDDEN;
    if(dirent->flags & ISO9660_FLAG_DIR) dest->attributes |= FILE_ATTR_DIR;
}
