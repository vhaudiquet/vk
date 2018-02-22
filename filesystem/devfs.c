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
#include "io/io.h"

typedef struct DEVFS_DISKLOC
{
    void* device_struct;
    u8 device_type;
    u8 device_info;
} __attribute__((packed)) devfs_diskloc_t;

typedef struct DEVFS_DIRENT
{
    devfs_diskloc_t* diskloc;
    char* name;
    u32 length;
} __attribute__((packed)) devfs_dirent_t;

static void devfs_get_fd(file_descriptor_t* dest, devfs_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs);
static void devfs_create_file(file_descriptor_t* dir, devfs_diskloc_t* diskloc, char* name, u32 length);

file_system_t* devfs = 0;

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
    memset(root_dirent.diskloc->device_struct, 0, root_dirent.length);
    root_dirent.diskloc->device_info = 0;
    devfs_get_fd(&tr->root_dir, &root_dirent, 0, tr);

    /* add block devices */
    u32 i = 0;
    for(;i<block_device_count;i++)
    {
        block_device_t* blockdev = block_devices[i];
        if(blockdev->device_class != HARD_DISK_DRIVE) continue;

        //create sdx entry
        devfs_diskloc_t* diskloc = kmalloc(sizeof(devfs_diskloc_t));
        diskloc->device_struct = blockdev;
        diskloc->device_type = DEVICE_TYPE_BLOCK;
        diskloc->device_info = 0;
        char* name = kmalloc(4);
        name[0] = 's'; name[1] = 'd'; name[2] = (char) ('a'+i); name[3] = 0;
        devfs_create_file(&tr->root_dir, diskloc, name, blockdev->device_size*512); //care: size is in sectors

        //create sdxX entry
        u8 j = 0;
        for(;j<blockdev->partition_count;j++)
        {
            devfs_diskloc_t* xdiskloc = kmalloc(sizeof(devfs_diskloc_t));
            xdiskloc->device_struct = blockdev;
            xdiskloc->device_type = DEVICE_TYPE_BLOCK_PART;
            xdiskloc->device_info = j;
            char* xname = kmalloc(5);
            xname[0] = 's'; xname[1] = 'd'; xname[2] = (char) ('a'+i); xname[3] = (char) ('1' + j); xname[4] = 0;
            devfs_create_file(&tr->root_dir, xdiskloc, xname, blockdev->partitions[j]->length); //care: length is in sectors ?
        }
    }
    devfs = tr;
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
    while((dirent_buffer->diskloc) && (length>=sizeof(devfs_dirent_t)))
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

error_t devfs_read_file(fd_t* fd, void* buffer, u64 count)
{
    file_descriptor_t* file = fd->file;
    devfs_diskloc_t* diskloc = (devfs_diskloc_t*) ((uintptr_t) file->fsdisk_loc);

    if(diskloc->device_type == DEVICE_TYPE_BLOCK)
    {
        return block_read_flexible(0, (u32) fd->offset, (u8*) buffer, count, diskloc->device_struct);
    }
    else if(diskloc->device_type == DEVICE_TYPE_BLOCK_PART)
    {
        block_device_t* bd = diskloc->device_struct;
        return block_read_flexible(bd->partitions[diskloc->device_info]->start_lba, (u32) fd->offset, (u8*) buffer, count, bd);
    }
    else if(diskloc->device_type == DEVICE_TYPE_TTY)
    {
        tty_t* tty = diskloc->device_struct;
        *((u8*) buffer) = tty_getch(tty);
        return ERROR_NONE;
    }

    return ERROR_FILE_FS_INTERNAL;
}

error_t devfs_write_file(fd_t* fd, void* buffer, u64 count)
{
    file_descriptor_t* file = fd->file;
    devfs_diskloc_t* diskloc = (devfs_diskloc_t*) ((uintptr_t) file->fsdisk_loc);

    if(diskloc->device_type == DEVICE_TYPE_BLOCK)
    {
        return block_write_flexible(0, (u32) fd->offset, (u8*) buffer, count, diskloc->device_struct);
    }
    else if(diskloc->device_type == DEVICE_TYPE_BLOCK_PART)
    {
        block_device_t* bd = diskloc->device_struct;
        return block_write_flexible(bd->partitions[diskloc->device_info]->start_lba, (u32) fd->offset, (u8*) buffer, count, bd);
    }
    else if(diskloc->device_type == DEVICE_TYPE_TTY)
    {
        tty_t* tty = diskloc->device_struct;
        return tty_write(buffer, (u32) count, tty);
    }

    return ERROR_FILE_FS_INTERNAL;
}

void devfs_register_device(char* name, void* device, u8 device_type, u8 device_info)
{
    char* name_bck = kmalloc(strlen(name)+1);
    strcpy(name_bck, name);

    devfs_diskloc_t* diskloc = kmalloc(sizeof(devfs_diskloc_t));
    diskloc->device_struct = device;
    diskloc->device_type = device_type;
    diskloc->device_info = device_info;

    devfs_create_file(&devfs->root_dir, diskloc, name_bck, 0);
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
    while((dirent_buffer->diskloc) && (dlength>=sizeof(devfs_dirent_t)))
    {
        dirent_buffer = (devfs_dirent_t*) (((uintptr_t) dirent_buffer) + sizeof(devfs_dirent_t));
        dlength -= sizeof(devfs_dirent_t);
    }

    if(dir->length < sizeof(devfs_dirent_t))
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
