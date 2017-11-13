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

#ifndef FS_HEAD
#define FS_HEAD

#include "storage/storage.h"

#define FS_TYPE_RAMFS 1
#define FS_TYPE_FAT32 2
#define FS_TYPE_EXT2 3
#define FS_TYPE_ISO9660 4

#define FILE_ATTR_DIR 0x1
#define FILE_ATTR_HIDDEN 0x2
#define DIR_ATTR_MOUNTPOINT 0x3

//VFS, GLOBAL LAYER
typedef struct file_descriptor
{
    char* name; //file name
    void* file_system; //file system
    struct file_descriptor* parent_directory; //parent directory
    u8 fs_type; //file system type
    u64 fsdisk_loc;//location of the file on the drive (ex: first cluster if FAT32fs)
    u8 attributes; //dir/file, hidden , (system/prgm/user), 
    u64 length; //lenght in bytes
    u64 offset; //offset (file only)
} file_descriptor_t;

u8 detect_fs_type(block_device_t* drive, u8 partition);
u8 mount_volume(char* path, block_device_t* drive, u8 partition);
void mount(char* path, u8 fs_type, void* fs);
file_descriptor_t* open_file(char* path);
u8 read_file(file_descriptor_t* file, void* buffer, u64 count);

//FILESYSTEM utils
void fd_list_free(list_entry_t* list, u32 list_size);
void fd_copy(file_descriptor_t* dest, file_descriptor_t* src);
void fd_free(file_descriptor_t* fd);

//RAMFS specific
typedef struct ramfs
{
    u32 base_addr;
    u32 size;
    u32 list_size;
    file_descriptor_t root_directory;
} ramfs_t;

ramfs_t* ramfs_init(u32 size);
list_entry_t* ramfs_read_dir(file_descriptor_t* dir, u32* size);
file_descriptor_t* ramfs_open_file(char* path, ramfs_t* fs);
u8 ramfs_read_file(file_descriptor_t* file, void* buffer, u64 count);
u8 ramfs_write_file(file_descriptor_t* file, u8* buffer, u64 count);
file_descriptor_t* ramfs_create_file(u8* name, u8 attributes, file_descriptor_t* dir);

//FAT32 specific
typedef struct fat32fs
{
    block_device_t* drive;
    struct BPB* bpb;
    file_descriptor_t root_dir;
    u32 bpb_offset;
    u32* fat_table;
    u8 partition;
} fat32fs_t;

fat32fs_t* fat32fs_init(block_device_t* drive, u8 partition);
void fat32fs_close(fat32fs_t* fs);
list_entry_t* fat32fs_read_dir(file_descriptor_t* dir, u32* size);
file_descriptor_t* fat32fs_open_file(char* path, fat32fs_t* fs);
u8 fat32fs_read_file(file_descriptor_t* file, void* buffer, u64 count);
u8 fat32fs_write_file(file_descriptor_t* file, u8* buffer, u64 count);
file_descriptor_t* fat32fs_create_file(u8* name, u8 attributes, file_descriptor_t* dir);

//ISO9660 specific
typedef struct iso9660fs
{
    block_device_t* drive;
    file_descriptor_t root_dir;
} iso9660fs_t;
iso9660fs_t* iso9660fs_init(block_device_t* drive);
list_entry_t* iso9660fs_read_dir(file_descriptor_t* dir, u32* size);

#endif