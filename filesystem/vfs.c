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
#include "fs.h"
#include "error/error.h"
#include "memory/mem.h"

/*
* This is the virtual file system, that contains all of the mount points and dialogs with every individual filesystem
* to provides general functions to open a file from a path, either the file is on disk/ram/external, fat32/ext2/... (once supported)
*/

typedef struct mount_point
{
    char* path;
    void* fs;
    struct mount_point* next;
    u8 fs_type;
} mount_point_t;
mount_point_t* root_point = 0;
u16 current_mount_points = 0;

static file_descriptor_t* do_open_fs(char* path, mount_point_t* mp);

//u8 detect_fs_type(block_device_t* drive, u8 partition) (find by signature)
//void mount_volume(char* path, block_device_t* drive, u8 partition) (using method above)

u8 detect_fs_type(block_device_t* drive, u8 partition)
{
    if(!drive) return 0;
    
    u8* buff = 
    #ifdef MEMLEAK_DBG
    kmalloc(512, "detect fs type buffer");
    #else
    kmalloc(512);
    #endif

    if(!drive->partitions[partition]->system_id)
    {
        kfree(buff);
        return 0;
    }

    u32 offset = drive->partitions[partition]->start_lba;
    if(block_read_flexible(offset, 0, buff, 512, drive) != DISK_SUCCESS)
    {
        kprintf("%lDisk failure during attempt to detect fs type...\n", 2);
    }
    
    //FAT8 and 16 signature offset kprintf("s: %s\n", buff+54);
    if(((*(buff+66) == 0x28) | (*(buff+66) == 0x29)) && (strcfirst("FAT32", (char*) buff+82) == 5))
    {
        kfree(buff);
        return FS_TYPE_FAT32;
    }
    else kprintf("%lUnknown file system type.\n", 3);

    kfree(buff);
    return 0;
}

u8 mount_volume(char* path, block_device_t* drive, u8 partition)
{
    void* fs = 0;
    u8 fs_type = detect_fs_type(drive, partition);
    switch(fs_type)
    {
        case 0: return 0;
        case FS_TYPE_FAT32:
        {
            fs = fat32fs_init(drive, partition);
            break;
        }
    }
    if(fs)
        mount(path, fs_type, fs);
    else return 0;
    
    return fs_type;
}

void mount(char* path, u8 fs_type, void* fs)
{
    if(fs_type == 0) fatal_kernel_error("Unknown file system type", "MOUNT");
    if(*path != '/') fatal_kernel_error("Path must be absolute for mounting", "MOUNT"); //TEMP
    if(root_point == 0 && strlen(path) > 1) fatal_kernel_error("Root point not mounted, can't mount anything else", "MOUNT"); //TEMP

    if(root_point == 0)
    {
        //we are mounting root point
        root_point = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(mount_point_t), "Mount point (root '/') struct");
        #else
        kmalloc(sizeof(mount_point_t));
        #endif
        root_point->path = path;
        root_point->fs = fs;
        root_point->fs_type = fs_type;
        root_point->next = 0;
        current_mount_points++;
        return;
    }

    //we are mounting a standard point
    //first lets get the folder pointed by path, check if it is really a folder, and make it mount_point
    file_descriptor_t* mf = open_file(path);
    if((mf->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) fatal_kernel_error("Trying to mount to a file ?!", "MOUNT");

    //check if 'mf' is empty ; if it is not we are bad

    mf->attributes |= DIR_ATTR_MOUNTPOINT;

    mount_point_t* next_point = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(mount_point_t), "Mount point struct");
    #else
    kmalloc(sizeof(mount_point_t));
    #endif
    next_point->path = path;
    next_point->fs = fs;
    next_point->fs_type = fs_type;
    next_point->next = 0;
    
    mount_point_t* last = root_point;
    u32 i = 0;
    while(i < current_mount_points)
    {
        last = last->next;
    }
    last->next = next_point;
    current_mount_points++;
}

//void umount

file_descriptor_t* open_file(char* path)
{
    //we are trying to access the root path : simplest case
    if(*path == '/' && strlen(path) == 1)
    {
        switch(root_point->fs_type)
        {
            case FS_TYPE_FAT32:
            {
                fat32fs_t* fs = root_point->fs;
                return &fs->root_dir;
            } 
        }
    }

    //we are already into a filesystem that contains a dir that is a mount point
    //looking on the list for the mount point
    u32 i = 0;
    u16 chars = 0; mount_point_t* best = root_point;
    mount_point_t* current = root_point;
    while(i < current_mount_points)
    {
        u16 cc = strcfirst(current->path, path);
        if(cc > chars)
        {
            best = current;
            chars = cc;
        }
        current = current->next;
        i++;
    }

    if(best != 0) return do_open_fs(path, best);
    
    fatal_kernel_error("Failed to find mount point ? WTF?", "OPEN_FILE"); //TEMP
    return 0;
}

static file_descriptor_t* do_open_fs(char* path, mount_point_t* mp)
{
    switch(mp->fs_type)
    {
        case FS_TYPE_FAT32:
            return fat32fs_open_file(path+1, (fat32fs_t*) mp->fs);
    }
    fatal_kernel_error("Could not find filesystem type ???", "DO_OPEN_FS");
    return 0;
}

u8 read_file(file_descriptor_t* file, void* buffer, u64 count)
{
    if((file->attributes & FILE_ATTR_DIR) == FILE_ATTR_DIR) return 1;

    if(file->offset >= file->length) {*((u8*) buffer) = 0; return 0;}
	if(count+file->offset > file->length) count = file->length - file->offset; //if we want to read more, well, NO BASTARD GTFO
	if(count == 0) {*((u8*) buffer) = 0; return 0;}

    switch(file->fs_type)
    {
        case FS_TYPE_FAT32:
            return fat32fs_read_file(file, buffer, count);
    }
    fatal_kernel_error("Could not find filesystem type ???", "DO_OPEN_FS");
    return 2;
}
