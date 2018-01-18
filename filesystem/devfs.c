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

#include "fs.h"
#include "memory/mem.h"

#define DEVFS_TYPE_DIRECTORY 1
#define DEVICE_TYPE_BLOCK 2
#define DEVICE_TYPE_INPUT 3

typedef struct DEVFS_DISKLOC
{
    void* device_struct;
    u8 device_type;
} __attribute__((packed)) devfs_diskloc_t;

typedef struct DEVFS_DIRENT
{
    devfs_diskloc_t* diskloc;
    char* name;
    u32 length;
} __attribute__((packed)) devfs_dirent_t;

static void devfs_get_fd(file_descriptor_t* dest, devfs_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs);
static void devfs_create_file(file_descriptor_t* dir, devfs_diskloc_t* diskloc, char* name, u32 length);

file_system_t* devfs_init()
{
    file_system_t* tr = kmalloc(sizeof(file_system_t));
    tr->drive = 0;
    tr->partition = 0;
    tr->fs_type = FS_TYPE_DEVFS;
    tr->flags = 0;

    //creating root_dir dirent
    devfs_dirent_t root_dirent;
    root_dirent.length = sizeof(devfs_dirent_t) * 20;
    root_dirent.name = "dev";
    root_dirent.diskloc = kmalloc(sizeof(devfs_diskloc_t));
    root_dirent.diskloc->device_type = DEVFS_TYPE_DIRECTORY;
    root_dirent.diskloc->device_struct = kmalloc(root_dirent.length);
    devfs_get_fd(&tr->root_dir, &root_dirent, 0, tr);

    //add block devices
    u32 i = 0;
    for(;i<block_device_count;i++)
    {
        block_device_t* blockdev = block_devices[i];

        //create sdx entry
        devfs_diskloc_t* diskloc = kmalloc(sizeof(devfs_diskloc_t));
        diskloc->device_struct = blockdev;
        diskloc->device_type = DEVICE_TYPE_BLOCK;
        char* name = kmalloc(4);
        name[0] = 's'; name[1] = 'd'; name[2] = (char) ('a'+i); name[3] = 0;
        devfs_create_file(&tr->root_dir, diskloc, name, blockdev->device_size); //care: size is in sectors

        //create sdxX entry
        u32 j = 0;
        for(;j<blockdev->partition_count;j++)
        {
            
        }
    }

    return tr;
}

list_entry_t* devfs_read_dir(file_descriptor_t* dir, u32* size)
{
    file_system_t* fs = dir->file_system;
    devfs_diskloc_t* diskloc = (devfs_diskloc_t*) ((uintptr_t) dir->fsdisk_loc);
    if(diskloc->device_type != DEVFS_TYPE_DIRECTORY) return 0;

    devfs_dirent_t* dirent_buffer = diskloc->device_struct;

    list_entry_t* tr = kmalloc(sizeof(list_entry_t));
    list_entry_t* ptr = tr;
    *size = 0;

    u32 length = (u32) dir->length;
    while(dirent_buffer->diskloc && length>=sizeof(devfs_dirent_t))
    {
        file_descriptor_t* fd = kmalloc(sizeof(file_descriptor_t));
        devfs_get_fd(fd, dirent_buffer, dir, fs);
        ptr->element = fd;
        ptr->next = kmalloc(sizeof(list_entry_t));
        ptr = ptr->next;
        dirent_buffer = (devfs_dirent_t*) (((uintptr_t) dirent_buffer) + sizeof(devfs_dirent_t));
        length -= sizeof(devfs_dirent_t);
        (*size)++;
    }
    kfree(ptr);

    return tr;
}

static void devfs_create_file(file_descriptor_t* dir, devfs_diskloc_t* diskloc, char* name, u32 length)
{
    devfs_diskloc_t* dir_diskloc = (devfs_diskloc_t*) ((uintptr_t) dir->fsdisk_loc);

    devfs_dirent_t tc;
    tc.diskloc = diskloc;
    tc.name = name;
    tc.length = length;

    devfs_dirent_t* dirent_buffer = dir_diskloc->device_struct;
    u32 dlength = (u32) dir->length;
    while(dirent_buffer->diskloc && dlength>=sizeof(devfs_dirent_t))
    {
        dirent_buffer = (devfs_dirent_t*) (((uintptr_t) dirent_buffer) + sizeof(devfs_dirent_t));
        dlength -= sizeof(devfs_dirent_t);
    }

    if(length < sizeof(devfs_dirent_t))
    {
        //worst case, we need to realloc space...
        u32 oldl = (u32) dir->length;
        dir->length += sizeof(devfs_dirent_t)*20;
        dir_diskloc->device_struct = krealloc(dir_diskloc->device_struct, (u32) dir->length);
        dirent_buffer = (devfs_dirent_t*) ((uintptr_t) dir_diskloc->device_struct + oldl);
    }

    memcpy(dirent_buffer, &tc, sizeof(devfs_dirent_t));
}

static void devfs_get_fd(file_descriptor_t* dest, devfs_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs)
{
    //copy name
    dest->name = kmalloc(strlen(dirent->name)+1);
    strcpy(dest->name, dirent->name);

    dest->attributes = 0;
    if(dirent->diskloc->device_type == DEVFS_TYPE_DIRECTORY) dest->attributes = FILE_ATTR_DIR;

    //set basic infos
    dest->file_system = fs;
    dest->parent_directory = parent;
    dest->length = dirent->length;
    //TODO: maybe better scale dest->offset = 0;

    dest->fsdisk_loc = (uintptr_t) dirent->diskloc;

    //set time
    dest->creation_time = dest->last_access_time = dest->last_modification_time = 0;
}