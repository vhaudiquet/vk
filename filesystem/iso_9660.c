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

#include "iso9660.h"

static fsnode_t* iso9660_dirent_normalize_cache(iso9660_dir_entry_t* dirent, file_system_t* fs);

file_system_t* iso9660fs_init(block_device_t* drive)
{
    //allocating data struct
    file_system_t* tr = 
    #ifndef MEMLEAK_DBG
    kmalloc(sizeof(file_system_t));
    #else
    kmalloc(sizeof(file_system_t), "iso9660 file_system_t struct");
    #endif

    tr->drive = drive;
    tr->fs_type = FS_TYPE_ISO9660;
    tr->flags = 0 | FS_FLAG_CASE_INSENSITIVE | FS_FLAG_READ_ONLY;

    tr->inode_cache = 0;
    tr->inode_cache_size = 0;

    //reading primary volume descriptor
    iso9660_primary_volume_descriptor_t pvd;
    block_read_flexible(0x10, 0, (u8*) &pvd, sizeof(pvd), drive);

    //making root_dir file descriptor
    iso9660_dir_entry_t* rd = (iso9660_dir_entry_t*) pvd.root_directory;
    tr->root_dir = iso9660_dirent_normalize_cache(rd, tr);

    return tr;
}

fsnode_t* iso9660_open(fsnode_t* dir, char* name)
{
    file_system_t* fs = dir->file_system;
    iso9660_node_specific_t* spe = dir->specific;
    
    u32 length = (u32) dir->length;
    u8* dirent_data = kmalloc(length);

    //TEMP : this can't work, because on multiple sector using, sectors are padded with 0s
    error_t readop0 = block_read_flexible(spe->extent_start, 0, dirent_data, length, fs->drive);
    if(readop0 != ERROR_NONE) return 0;

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

        char* dname = kmalloc((u32) dirptr->name_len+1);
        strncpy(dname, dirptr->name, dirptr->name_len);
        if(dirptr->name_len > 2) {if(*(dname+dirptr->name_len-2) == ';') *(dname+dirptr->name_len-2) = 0;}
        if(dirptr->name_len > 1) {u32 len = strlen(dname); if(*(dname+len-1) == '.') *(dname+len-1) = 0;}

        if(!strcmpnc(dname, name)) 
        {
            fsnode_t* tr = iso9660_dirent_normalize_cache(dirptr, fs);
            kfree(dirent_data);
            return tr;
        }
        
        offset+= dirptr->length;
        length-= dirptr->length;
        index++;
    }

    kfree(dirent_data);

    return 0;
}

error_t iso9660_list_dir(list_entry_t* dest, fsnode_t* dir, u32* size)
{
    file_system_t* fs = dir->file_system;
    iso9660_node_specific_t* spe = dir->specific;
    u64 lba = spe->extent_start;
    u32 length = (u32) dir->length;
    u8* dirent_data = 
    #ifdef MEMLEAK_DBG
    kmalloc(length, "iso9660_read_dir dirent buffer");
    #else
    kmalloc(length);
    #endif

    //TEMP : this can't work, because on multiple sector using, sectors are padded with 0s
    error_t readop = block_read_flexible(lba, 0, dirent_data, length, fs->drive);
    if(readop != ERROR_NONE) {kfree(dirent_data); return readop;}
    
    list_entry_t* ptr = dest;
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

        dirent_t* fd;
        if(index == 0)
        {
            //current dir (.)
            fd = kmalloc(sizeof(dirent_t)+2);
            strcpy(fd->name, ".");
            *(fd->name+1) = 0;
            fd->name_len = 1;
            fd->inode = dirptr->extent_start_lsb; //todo: check if that really works
        }
        else if(index == 1)
        {
            //parent dir (..)
            fd = kmalloc(sizeof(dirent_t)+3);
            strcpy(fd->name, "..");
            *(fd->name+2) = 0;
            fd->name_len = 2;
            fd->inode = dirptr->extent_start_lsb; //todo: check if that really works
        }
        else
        {
            fd = kmalloc(sizeof(dirent_t)+dirptr->name_len+1);
            strncpy(fd->name, dirptr->name, dirptr->name_len);
            *(fd->name+dirptr->name_len) = 0;
            fd->name_len = dirptr->name_len;
            fd->inode = dirptr->extent_start_lsb;
        }
        
        //kprintf("%s\n", fd->name);
        ptr->element = fd;
        ptr->next = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(list_entry_t), "iso9660_read_dir list entry");
        #else
        kmalloc(sizeof(list_entry_t));
        #endif
        ptr = ptr->next;
        offset+= dirptr->length;
        length-= dirptr->length;
        (*size)++;
        index++;
    }
    kfree(ptr);

    kfree(dirent_data);

    return ERROR_NONE;
}

error_t iso9660_read_file(fd_t* fd, void* buffer, u64 count)
{
    fsnode_t* file = fd->file;
    iso9660_node_specific_t* spe = file->specific;
    u64 offset = fd->offset;
    file_system_t* fs = file->file_system;

    return block_read_flexible(spe->extent_start, (u32) offset, buffer, count, fs->drive);
}

static fsnode_t* iso9660_dirent_normalize_cache(iso9660_dir_entry_t* dirent, file_system_t* fs)
{
    /* try to read node from the cache */
    list_entry_t* iptr = fs->inode_cache;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        iso9660_node_specific_t* espe = element->specific;
        if(espe->extent_start == dirent->extent_start_lsb) return element;
        iptr = iptr->next;
        isize--;
    }

    /* parse inode from dirent */
    fsnode_t* std_node = kmalloc(sizeof(fsnode_t));

    std_node->hard_links = 1;

    std_node->file_system = fs;
    std_node->length = dirent->extent_size_lsb;

    //parse time (check for year-100)
    std_node->creation_time = convert_to_std_time(dirent->record_time.second, dirent->record_time.minute, dirent->record_time.hour, dirent->record_time.day, dirent->record_time.month, (u8)(dirent->record_time.year));
    std_node->last_modification_time = std_node->creation_time;
    std_node->last_access_time = 0; //0 as NO value or -1 ? NO Value or creation_time ?

    //set attributes
    std_node->attributes = 0;
    if(dirent->flags & ISO9660_FLAG_HIDDEN) std_node->attributes |= FILE_ATTR_HIDDEN;
    if(dirent->flags & ISO9660_FLAG_DIR) std_node->attributes |= FILE_ATTR_DIR;

    //set specific
    iso9660_node_specific_t* spe = kmalloc(sizeof(iso9660_node_specific_t));
    spe->extent_start = dirent->extent_start_lsb;
    std_node->specific = spe;

    /* cache the object */
    if(!fs->inode_cache)
    {
        fs->inode_cache = kmalloc(sizeof(list_entry_t));
        fs->inode_cache->element = std_node;
        fs->inode_cache_size++;
    }
    else
    {
        list_entry_t* ptr = fs->inode_cache;
        list_entry_t* last = 0;
        u32 size = fs->inode_cache_size;
        while(ptr && size)
        {
            last = ptr;
            ptr = ptr->next;
            size--;
        }
        ptr = kmalloc(sizeof(list_entry_t));
        ptr->element = std_node;
        last->next = ptr;
        fs->inode_cache_size++;
    }

    return std_node;
}
