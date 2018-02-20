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

#include "fat32.h"
#include "ext2.h"
#include "iso9660.h"
#include "devfs.h"

/*
* This is the virtual file system, that contains all of the mount points and dialogs with every individual filesystem
* to provides general functions to open a file from a path, either the file is on disk/ram/external, fat32/ext2/... (once supported)
*/

typedef struct mount_point
{
    char* path;
    file_system_t* fs;
    struct mount_point* next;
} mount_point_t;
mount_point_t* root_point = 0;
u16 current_mount_points = 0;

static fsnode_t* do_open_fs(char* path, mount_point_t* mp);
static fsnode_t* create_file(char* path, u8 attributes);

u8 detect_fs_type(block_device_t* drive, u8 partition)
{
    if(!drive) return 0;
    
    u32 offset;
    if(partition)
    {
        if(!drive->partitions[partition-1]->system_id)
            return 0;
        offset = drive->partitions[partition-1]->start_lba;
    }
    else offset = 0;

    u8* buff = 
    #ifdef MEMLEAK_DBG
    kmalloc(2048, "detect fs type buffer");
    #else
    kmalloc(2048);
    #endif

    u8* buff2 = 
    #ifdef MEMLEAK_DBG
    kmalloc(512, "detect fs type buffer 2");
    #else
    kmalloc(512);
    #endif

    if(block_read_flexible(offset, 0, buff, 2048, drive) != ERROR_NONE)
    {
        kprintf("%lDisk failure during attempt to detect fs type...\n", 2);
    }
    if(block_read_flexible(offset+0x10, 0, buff2, 512, drive) != ERROR_NONE)
    {
        kprintf("%lDisk failure during attempt to detect fs type...\n", 2);
    }

    //FAT8 and 16 signature offset kprintf("s: %s\n", buff+54);
    if(((*(buff+66) == 0x28) | (*(buff+66) == 0x29)) && (strcfirst("FAT32", (char*) buff+82) == 5))
    {
        kfree(buff);
        kfree(buff2);
        return FS_TYPE_FAT32;
    }
    else if((strcfirst("CD001", (char*) buff2+1) == 5))
    {
        kfree(buff);
        kfree(buff2);
        return FS_TYPE_ISO9660;
    }
    else if((*(buff+1024+56) == 0x53) && (*(buff+1024+57) == 0xef))
    {
        kfree(buff);
        kfree(buff2);
        return FS_TYPE_EXT2;
    }
    else kprintf("%lUnknown file system type.\n", 3);

    kfree(buff);
    kfree(buff2);
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
        case FS_TYPE_ISO9660:
        {
            fs = iso9660fs_init(drive);
            break;
        }
        case FS_TYPE_EXT2:
        {
            fs = ext2_init(drive, partition);
            break;
        }
    }
    if(fs)
        mount(path, fs);
    else return 0;
    
    return fs_type;
}

void mount(char* path, file_system_t* fs)
{
    if(fs->fs_type == 0) fatal_kernel_error("Unknown file system type", "MOUNT");
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
        root_point->next = 0;
        current_mount_points++;
        return;
    }

    //we are mounting a standard point
    //first lets get the folder pointed by path, check if it is really a folder, and make it mount_point
    fd_t* mf = open_file(path, OPEN_MODE_R);
    if(!mf) fatal_kernel_error("Can't find mount point", "MOUNT");
    if((mf->file->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) fatal_kernel_error("Trying to mount to a file ?!", "MOUNT");

    //TODO : check if 'mf' is empty ; if it is not we are bad
    
    mf->file->attributes |= DIR_ATTR_MOUNTPOINT;

    mount_point_t* next_point = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(mount_point_t), "Mount point struct");
    #else
    kmalloc(sizeof(mount_point_t));
    #endif
    next_point->path = path;
    next_point->fs = fs;
    next_point->next = 0;
    
    mount_point_t* last = root_point;
    while(last->next)
    {
        last = last->next;
    }
    last->next = next_point;
    current_mount_points++;
}

fd_t* open_file(char* path, u8 mode)
{
    //we are trying to access the root path : simplest case
    if(*path == '/' && strlen(path) == 1)
    {
        file_system_t* fs = root_point->fs;
        fd_t* tr = kmalloc(sizeof(fd_t));
        tr->file = fs->root_dir;
        tr->offset = 0;
        return tr;
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

    //if we want the root directory of the root point
    if(!strcmp(path, best->path))
    {
        fd_t* tr = kmalloc(sizeof(fd_t));
        tr->file = best->fs->root_dir;
        tr->offset = 0;
        return tr;
    }

    if(!best) {fatal_kernel_error("Failed to find mount point ? WTF?", "OPEN_FILE"); return 0;}

    fsnode_t* node = do_open_fs(path+strlen(best->path), best);
    
    if((!node) && ((mode == OPEN_MODE_R) | (mode == OPEN_MODE_RP))) return 0;
    
    fd_t* tr = kmalloc(sizeof(fd_t));
    tr->file = node;
    tr->offset = 0;

    //depending on mode
    if((mode == OPEN_MODE_R) | (mode == OPEN_MODE_RP)) return tr;
    else
    {
        if(node == 0) {tr->file = create_file(path, 0); if(!tr->file) return 0;}
        if((mode == OPEN_MODE_W) | (mode == OPEN_MODE_WP))
        {
            u8* zero_buffer = kmalloc((u32) flength(tr)); memset(zero_buffer, 0, (size_t) flength(tr));
            write_file(tr, zero_buffer, flength(tr));
            tr->file->length = 0;
        }
        if((mode == OPEN_MODE_A) | (mode == OPEN_MODE_AP))
        {
            tr->offset = flength(tr);
        }
    }

    return tr;
}

void close_file(fd_t* file)
{
    kfree(file);
}

u64 flength(fd_t* file)
{
    return file->file->length;
}

error_t read_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* inode = fd->file;
    if((inode->attributes & FILE_ATTR_DIR) == FILE_ATTR_DIR) return ERROR_FILE_IS_DIRECTORY;

    if(flength(fd)) //if file->length is 0, it's a special file (blockdev/tty...)
    {
        if(fd->offset >= flength(fd)) {*((u8*) buffer) = 0; return ERROR_EOF;}
	    if(count+fd->offset > flength(fd)) count = flength(fd) - fd->offset; //if we want to read more, we just reajust
    }

	if(count == 0) {*((u8*) buffer) = 0; return ERROR_NONE;}

    error_t tr = ERROR_FILE_UNSUPPORTED_FILE_SYSTEM;
    switch(inode->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            //tr = fat32_read_file(fd, buffer, count);
            break;
        case FS_TYPE_ISO9660:
            tr = iso9660_read_file(fd, buffer, count);
            break;
        case FS_TYPE_EXT2:
            tr = ext2_read_file(fd, buffer, count);
            break;
        case FS_TYPE_DEVFS:
            tr = devfs_read_file(fd, buffer, count);
            break;
    }
    if(tr == ERROR_NONE) fd->offset += count;
    return tr;
}

error_t write_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* inode = fd->file;

    if((inode->attributes & FILE_ATTR_DIR) == FILE_ATTR_DIR) return ERROR_FILE_IS_DIRECTORY;
    if(count == 0) return 0;

    error_t tr = ERROR_FILE_UNSUPPORTED_FILE_SYSTEM;
    switch(inode->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            //tr = fat32_write_file(fd, buffer, count);
            break;
        case FS_TYPE_DEVFS:
            tr = devfs_write_file(fd, buffer, count);
            break;
        case FS_TYPE_EXT2:
            tr = ext2_write_file(fd, buffer, count);
            break;
    }
    if(tr == ERROR_NONE) fd->offset += count;
    return tr;
}

static fsnode_t* do_open_fs(char* path, mount_point_t* mp)
{
    if(*path == '/') path++;

    /* Step 1 : Split the path on '/' */
	u32 i = 0;
	u32 split_size = 0;
	char** spath = strsplit(path, '/', &split_size);
    
    /* Step 2 : iterate from the root directory and continue on as we found dirs/files on the list (splitted) */
    fsnode_t* node = mp->fs->root_dir;
    while(i < split_size)
    {
        switch(mp->fs->fs_type)
        {
            case FS_TYPE_FAT32: {node = fat32_open(node, spath[i]); break;}
            case FS_TYPE_EXT2: {node = ext2_open(node, spath[i]); break;}
            case FS_TYPE_ISO9660: {node = iso9660_open(node, spath[i]); break;}
            case FS_TYPE_DEVFS: {node = devfs_open(node, spath[i]); break;}
        }
        if(!node) return 0;

        //this is the last entry we needed : we found our file !
        if((i+1) == split_size) return node;
        
        //we need to continue iterating, but an element on the path is not a directory...
        if(!(node->attributes & FILE_ATTR_DIR)) return 0;

        i++;
    }

    //the code should never reach that part
    return 0;
}

static fsnode_t* create_file(char* path, u8 attributes)
{
    return 0;
}
