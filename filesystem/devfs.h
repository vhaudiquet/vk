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
#ifndef DEVFS_HEAD
#define DEVFS_HEAD

#include "fs.h"
#include "memory/mem.h"
#include "io/io.h"

typedef struct devfs_dirent
{
    fsnode_t* node;
    char* name;
} devfs_dirent_t;

typedef struct devfs_node_specific
{
    void* device_struct;
    u32 device_type;
    u32 device_info;
} devfs_node_specific_t;

#define DEVFS_TYPE_DIRECTORY 1
#define DEVFS_TYPE_BLOCKDEV 2
#define DEVFS_TYPE_BLOCKDEV_PART 3
#define DEVFS_TYPE_TTY 4
#define DEVFS_TYPE_IOSTREAM 5

#define DEVFS_DIR_SIZE_DEFAULT (sizeof(devfs_dirent_t)*10)

extern file_system_t* devfs;
void devfs_init();
fsnode_t* devfs_open(fsnode_t* dir, char* name);
error_t devfs_list_dir(list_entry_t* dest, fsnode_t* dir, u32* size);
error_t devfs_read_file(fd_t* fd, void* buffer, u64 count);
error_t devfs_write_file(fd_t* fd, void* buffer, u64 count);
fsnode_t* devfs_register_device(fsnode_t* dir, char* name, void* device, u32 device_type, u32 device_info);

#endif