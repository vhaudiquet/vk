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

#ifndef FS_HEAD
#define FS_HEAD

#include "storage/storage.h"
#include "time/time.h"

#define FS_TYPE_RAMFS 1
#define FS_TYPE_FAT32 2
#define FS_TYPE_EXT2 3
#define FS_TYPE_ISO9660 4
#define FS_TYPE_DEVFS 5

#define FILE_ATTR_DIR 0x1
#define FILE_ATTR_HIDDEN 0x2
#define DIR_ATTR_MOUNTPOINT 0x3

//VFS, GLOBAL LAYER

typedef struct fsnode
{
    struct file_system* file_system;
    u64 length;
    u8 attributes;
    u32 hard_links;
    time_t creation_time;
    time_t last_access_time;
    time_t last_modification_time;
    void* specific;
} fsnode_t;

typedef struct dirent
{
    u32 inode;
    u32 name_len;
    char name[];
} dirent_t;

typedef struct fd
{
    fsnode_t* file;
    u64 offset;
} fd_t;

#define FS_FLAG_CASE_INSENSITIVE 1
#define FS_FLAG_READ_ONLY 2

typedef struct file_system
{
    block_device_t* drive;
    u8 partition;
    u8 fs_type;
    u8 flags;
    struct fsnode* root_dir;
    list_entry_t* inode_cache;
    u32 inode_cache_size;
    void* specific;
} file_system_t;

//mounting
u8 detect_fs_type(block_device_t* drive, u8 partition);
u8 mount_volume(char* path, block_device_t* drive, u8 partition);
void mount(char* path, file_system_t* fs);

#define OPEN_MODE_R 0 //read-only, at the beginning of the file
#define OPEN_MODE_W 1 //write-only, at the beginning of the file, creating it if doesnt exist, erasing if exists
#define OPEN_MODE_A 2 //append: write-only at end of file, creating it if doesnt exist
#define OPEN_MODE_RP 3 //r+ : update (read and write) at the beginning
#define OPEN_MODE_WP 4 //w+ : update (read and write) at the beginning, creating it if it doesnt exist, erasing if exists
#define OPEN_MODE_AP 5 //a+ : update (read and write) at end of file

//accessing files
fd_t* open_file(char* path, u8 mode);
void close_file(fd_t* file);
u64 flength(fd_t* file);
error_t read_file(fd_t* file, void* buffer, u64 count);
error_t write_file(fd_t* file, void* buffer, u64 count);
error_t rename_file(char* path, char* newname);
error_t link(char* oldpath, char* newpath);
error_t unlink(char* path);
list_entry_t* read_directory(fsnode_t* directory, u32* dirsize);

#endif