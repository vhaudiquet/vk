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
    file_system_t* fs;
    struct mount_point* next;
} mount_point_t;
mount_point_t* root_point = 0;
u16 current_mount_points = 0;

static file_descriptor_t* do_open_fs(char* path, mount_point_t* mp);

u8 detect_fs_type(block_device_t* drive, u8 partition)
{
    if(!drive) return 0;
    
    u8* buff = 
    #ifdef MEMLEAK_DBG
    kmalloc(512, "detect fs type buffer");
    #else
    kmalloc(512);
    #endif

    u8* buff2 = 
    #ifdef MEMLEAK_DBG
    kmalloc(512, "detect fs type buffer 2");
    #else
    kmalloc(512);
    #endif

    u32 offset;
    if(partition)
    {
        if(!drive->partitions[partition-1]->system_id)
        {
            kfree(buff);
            kfree(buff2);
            return 0;
        }
        offset = drive->partitions[partition-1]->start_lba;
    }
    else offset = 0;

    if(block_read_flexible(offset, 0, buff, 512, drive) != DISK_SUCCESS)
    {
        kprintf("%lDisk failure during attempt to detect fs type...\n", 2);
    }
    if(block_read_flexible(offset+0x10, 0, buff2, 512, drive) != DISK_SUCCESS)
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

file_descriptor_t* open_file(char* path)
{
    //we are trying to access the root path : simplest case
    if(*path == '/' && strlen(path) == 1)
    {
        file_system_t* fs = root_point->fs;
        return &fs->root_dir;
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

    if(best != 0) return do_open_fs(path+1, best);
    
    fatal_kernel_error("Failed to find mount point ? WTF?", "OPEN_FILE"); //TEMP
    return 0;
}

void close_file(file_descriptor_t* file)
{
    fd_free(file);
}

list_entry_t* read_directory(file_descriptor_t* directory, u32* dirsize)
{
    if(directory->file_system->fs_type == FS_TYPE_FAT32)
        return fat32fs_read_dir(directory, dirsize);
    else if(directory->file_system->fs_type == FS_TYPE_ISO9660)
        return iso9660fs_read_dir(directory, dirsize);
    else return 0;
}

u8 read_file(file_descriptor_t* file, void* buffer, u64 count)
{
    if((file->attributes & FILE_ATTR_DIR) == FILE_ATTR_DIR) return 1;

    if(file->offset >= file->length) {*((u8*) buffer) = 0; return 0;}
	if(count+file->offset > file->length) count = file->length - file->offset; //if we want to read more, well, NO BASTARD GTFO
	if(count == 0) {*((u8*) buffer) = 0; return 0;}

    u8 tr = 2;
    switch(file->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            tr = fat32fs_read_file(file, buffer, count);
            break;
        case FS_TYPE_ISO9660:
            tr = iso9660fs_read_file(file, buffer, count);
            break;
    }
    if(!tr) file->offset += count;
    return tr;
}

u8 write_file(file_descriptor_t* file, void* buffer, u64 count)
{
    if((file->attributes & FILE_ATTR_DIR) == FILE_ATTR_DIR) return 1;
    if(count == 0) return 0;

    u8 tr = 2;
    switch(file->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            tr = fat32fs_write_file(file, buffer, count);
            break;
    }
    if(!tr) file->offset += count;
    return tr;
}

file_descriptor_t* create_file(u8* name, u8 attributes, file_descriptor_t* dir)
{
    switch(dir->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            return fat32fs_create_file(name, attributes, dir);
    }
    return 0;
}

bool unlink(file_descriptor_t* file)
{
    //if the file is a directory, checks if it's empty
    if(file->attributes & FILE_ATTR_DIR)
    {
        u32 dirsize = 0;
        list_entry_t* list = read_directory(file, &dirsize);
        list_free(list, dirsize);
        if(dirsize) return false;
    }

    switch(file->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            return fat32fs_delete_file(file);
    }
    return false;
}

bool rename_file(file_descriptor_t* file, u8* newname)
{
    switch(file->file_system->fs_type)
    {
        case FS_TYPE_FAT32:
            return fat32fs_rename(file, newname);
    }
    return false;
}

static file_descriptor_t* do_open_fs(char* path, mount_point_t* mp)
{
    //The path should be relative to the mount point (excluded) of this fs
	/* Step 1 : Split the path on '/' */
	u32 i = 0;
	u32 split_size = 0;
	char** spath = strsplit(path, '/', &split_size);
    
    /* Step 2 : iterate from the root directory and continue on as we found dirs/files on the list (splitted) */
	u32 dirsize = 0;
	list_entry_t* cdir = read_directory(&mp->fs->root_dir, &dirsize);
	if(!cdir) return 0;
	list_entry_t* lbuf = cdir;
	u32 j = 0;
	file_descriptor_t* ce = 0;
	while(i < split_size)
	{
		//looking for the i element in the directory
		j = 0;
		while(j < dirsize)
		{
			ce = lbuf->element;
			//looking at each element in the directory
			if(!ce) break;
			// kprintf("%llooking %s for %s...\n", 3, ce->name, spath[i]);
            if(((mp->fs->flags & FS_FLAG_CASE_INSENSITIVE) & (!strcmpnc(ce->name, spath[i]))) | 
            ((!(mp->fs->flags & FS_FLAG_CASE_INSENSITIVE)) & (!strcmp(ce->name, spath[i]))))
			{
                //if it is the last entry we need to find, return ; else continue exploring path
				if((i+1) == split_size)
				{
					file_descriptor_t* tr = 
					#ifdef MEMLEAK_DBG
					kmalloc(sizeof(file_descriptor_t), "open_file file descriptor struct tr");
					#else
					kmalloc(sizeof(file_descriptor_t));
					#endif
					tr->name = 
					#ifdef MEMLEAK_DBG
					kmalloc(strlen(ce->name)+1, "open_file tr file name");
					#else
					kmalloc(strlen(ce->name)+1);
                    #endif
					fd_copy(tr, ce);
					fd_list_free(cdir, dirsize);
					kfree(spath[i]);
                    kfree(spath);
					return tr;
				}

                //copy 'dir to explore' file descriptor
                file_descriptor_t* nextdir = 
                #ifdef MEMLEAK_DBG
                kmalloc(sizeof(file_descriptor_t), "open_file dir to explore copy fd");
                #else
                kmalloc(sizeof(file_descriptor_t));
                #endif
                nextdir->name = 
                #ifdef MEMLEAK_DBG
                kmalloc(strlen(ce->name)+1, "open_file dir to explore copy name");
                #else
                kmalloc(strlen(ce->name)+1);
                #endif
                fd_copy(nextdir, ce);

                //free the current dir list
				fd_list_free(cdir, dirsize);

				if((ce->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) return 0;
                cdir = lbuf = read_directory(nextdir, &dirsize);
                kfree(nextdir);

				//we have found a subdirectory, explore it
				break;
            }
			lbuf = lbuf->next;
			j++;
			//at this point, if we havent break yet, there is no such dir/file as we look for ; return 0
			if(j == dirsize) 
			{
				kfree(spath[i]);
				kfree(spath);
				fd_list_free(cdir, dirsize);
				return 0;
			}
		}
		kfree(spath[i]);
		i++;
	}
	kfree(spath[i-1]);
	kfree(spath);
	fd_list_free(cdir, dirsize);
	return 0;
}
