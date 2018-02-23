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

#include "devfs.h"

file_system_t* devfs = 0;

void devfs_init()
{
    kprintf("Mounting /dev filesystem...");
    
    /* setting up devfs file_system_t structure */
    devfs = kmalloc(sizeof(file_system_t));
    devfs->drive = 0;
    devfs->partition = 0;
    devfs->fs_type = FS_TYPE_DEVFS;
    devfs->flags = 0;

    /* setting up root directory node */
    fsnode_t* root_dir = kmalloc(sizeof(fsnode_t));
    root_dir->file_system = devfs;
    root_dir->length = DEVFS_DIR_SIZE_DEFAULT;
    root_dir->attributes = 0 | FILE_ATTR_DIR;
    root_dir->creation_time = get_current_time_utc();
    root_dir->last_access_time = root_dir->creation_time;
    root_dir->last_modification_time = root_dir->creation_time;
    devfs_node_specific_t* spe = kmalloc(sizeof(devfs_node_specific_t));
    spe->device_struct = kmalloc(DEVFS_DIR_SIZE_DEFAULT);
    memset(spe->device_struct, 0, DEVFS_DIR_SIZE_DEFAULT);
    spe->device_type = DEVFS_TYPE_DIRECTORY;
    spe->device_info = 0;
    root_dir->specific = spe;

    devfs->root_dir = root_dir;

    mount("/dev", devfs);

    vga_text_okmsg();
}

fsnode_t* devfs_open(fsnode_t* dir, char* name)
{
    devfs_node_specific_t* spe = dir->specific;
    u8* dirptr = spe->device_struct;

    while(((uintptr_t)dirptr) < ((uintptr_t)(spe->device_struct+dir->length)))
    {
        devfs_dirent_t* dirent = (devfs_dirent_t*) dirptr;
        if(!dirent->node) break;
        if(!strcmp(dirent->name, name)) return dirent->node;
        dirptr += sizeof(devfs_dirent_t);
    }

    return 0;
}

error_t devfs_read_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* file = fd->file;
    devfs_node_specific_t* spe = file->specific;

    if(spe->device_type == DEVFS_TYPE_BLOCKDEV)
    {
        return block_read_flexible(0, (u32) fd->offset, (u8*) buffer, count, spe->device_struct);
    }
    else if(spe->device_type == DEVFS_TYPE_BLOCKDEV_PART)
    {
        block_device_t* bd = spe->device_struct;
        return block_read_flexible(bd->partitions[spe->device_info]->start_lba, (u32) fd->offset, (u8*) buffer, count, bd);
    }
    else if(spe->device_type == DEVFS_TYPE_TTY)
    {
        tty_t* tty = spe->device_struct;
        if(count == 1)
        {
            *((u8*) buffer) = tty_getch(tty);
            return ERROR_NONE;
        }
        return ERROR_FILE_FS_INTERNAL;
    }

    return ERROR_FILE_FS_INTERNAL;
}

error_t devfs_write_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* file = fd->file;
    devfs_node_specific_t* spe = file->specific;

    if(spe->device_type == DEVFS_TYPE_BLOCKDEV)
    {
        return block_write_flexible(0, (u32) fd->offset, (u8*) buffer, count, spe->device_struct);
    }
    else if(spe->device_type == DEVFS_TYPE_BLOCKDEV_PART)
    {
        block_device_t* bd = spe->device_struct;
        return block_write_flexible(bd->partitions[spe->device_info]->start_lba, (u32) fd->offset, (u8*) buffer, count, bd);
    }
    else if(spe->device_type == DEVFS_TYPE_TTY)
    {
        tty_t* tty = spe->device_struct;
        return tty_write(buffer, (u32) count, tty);
    }

    return ERROR_FILE_FS_INTERNAL;
}

fsnode_t* devfs_register_device(fsnode_t* dir, char* name, void* device, u32 device_type, u32 device_info)
{
    /* setting up node */
    fsnode_t* node = kmalloc(sizeof(fsnode_t));
    node->file_system = devfs;
    node->length = 0;
    node->attributes = 0;
    node->creation_time = get_current_time_utc();
    node->last_access_time = node->creation_time;
    node->last_modification_time = node->creation_time;
    devfs_node_specific_t* spe = kmalloc(sizeof(devfs_node_specific_t));
    spe->device_struct = device;
    spe->device_type = device_type;
    spe->device_info = device_info;
    node->specific = spe;

    /* putting node in directory */
    devfs_node_specific_t* dirspe = dir->specific;
    u8* dirptr = dirspe->device_struct;

    while(((uintptr_t)dirptr) < ((uintptr_t)(dirspe->device_struct+dir->length)))
    {
        devfs_dirent_t* dirent = (devfs_dirent_t*) dirptr;
        if(!*dirptr) 
        {
            u32 name_len = strlen(name);
            dirent->name = kmalloc(name_len+1); 
            strcpy(dirent->name, name);
            *(dirent->name+name_len) = 0; 
            dirent->node = node; 
            return node;
        }
        dirptr += sizeof(devfs_dirent_t);
    }
    
    u32 off = ((uintptr_t)dirptr) - ((uintptr_t)dirspe->device_struct);
    dir->length*=2;
    dirspe->device_struct = krealloc(dirspe->device_struct, (u32) dir->length);
    devfs_dirent_t* tc = (dirspe->device_struct+off);
    u32 name_len = strlen(name);
    tc->name = kmalloc(name_len+1);
    strcpy(tc->name, name);
    *(tc->name+name_len) = 0;
    tc->node = node;
    return node;
}
