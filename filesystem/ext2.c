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

#include "ext2.h"

#define BLOCK_OFFSET(block) ((block)*(ext2->block_size/512))

static fsnode_t* ext2_std_inode_read(u32 inode, file_system_t* fs);
static error_t ext2_std_inode_write(fsnode_t* node);

static error_t ext2_inode_read_content(fsnode_t* node, u32 offset, u32 size, void* buffer);
static error_t ext2_inode_write_content(fsnode_t* node, u32 offset, u32 size, void* buffer);

static error_t ext2_remove_dirent(char* name, fsnode_t* dir);
static error_t ext2_create_dirent(u32 inode, char* name, fsnode_t* dir);

static u32 ext2_block_alloc(file_system_t* fs);
static void ext2_block_free(u32 block, file_system_t* fs);
static u32 ext2_inode_alloc(file_system_t* fs);
static void ext2_inode_free(fsnode_t* node);
static u32 ext2_bitmap_mark_first_zero_bit(u8* bitmap, u32 len);
static void ext2_bitmap_mark_bit_free(u8* bitmap, u32 bit);

/*
* This function initializes a new ext2 file_system_t
*/
file_system_t* ext2_init(block_device_t* drive, u8 partition)
{
    //read superblock
    u32 start_offset = 0;
    if(partition) start_offset = drive->partitions[partition-1]->start_lba;

    ext2_superblock_t* superblock = kmalloc(sizeof(ext2_superblock_t));
    block_read_flexible(start_offset+2, 0, (u8*) superblock, sizeof(ext2_superblock_t), drive);

    if((superblock->signature != 0xef53) | ((superblock->version_major >= 1) & (superblock->required_features)))
    {
        kfree(superblock);
        return 0;
    }

    //allocating data struct
    file_system_t* tr = 
    #ifndef MEMLEAK_DBG
    kmalloc(sizeof(file_system_t));
    #else
    kmalloc(sizeof(file_system_t), "ext2 file_system_t struct");
    #endif
    tr->drive = drive;
    tr->fs_type = FS_TYPE_EXT2;
    tr->flags = 0;
    tr->partition = partition;

    if(superblock->read_only_features & 5) //masking 64-bits file sizes (supported) //TODO: support sparse superblocks..
    {
        tr->flags |= FS_FLAG_READ_ONLY;
    }

    tr->inode_cache = 0;
    tr->inode_cache_size = 0;

    //allocating specific data struct
    ext2fs_specific_t* ext2spe = kmalloc(sizeof(ext2fs_specific_t));
    ext2spe->superblock = superblock;
    ext2spe->superblock_offset = start_offset;
    ext2spe->block_size = (u32)(1024 << superblock->block_size);
    
    ext2spe->blockgroup_count = (superblock->blocks-superblock->superblock_number)/superblock->blocks_per_blockgroup;
    if((superblock->blocks-superblock->superblock_number)%superblock->blocks_per_blockgroup) ext2spe->blockgroup_count++;
    
    tr->specific = ext2spe;

    //root directory is on inode 2
    
    tr->root_dir = ext2_std_inode_read(2, tr);

    return tr;
}

/*
* This function lists the content of a directory to a linked list of dirent_t
*/
error_t ext2_list_dir(list_entry_t* dest, fsnode_t* dir, u32* size)
{
    u64 length = dir->length;

    u8* dirent_buffer = kmalloc((u32) length);
    error_t readop = ext2_inode_read_content(dir, 0, (u32) length, dirent_buffer);
    if(readop != ERROR_NONE) return readop;

    list_entry_t* ptr = dest;
    *size = 0;

    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) break;

        dirent_t* fd = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(dirent_t)+dirent->name_len, "ext2_read_dir dirent");
        #else
        kmalloc(sizeof(dirent_t)+dirent->name_len);
        #endif

        fd->name_len = dirent->name_len;
        strncpy(fd->name, (char*) dirent->name, dirent->name_len);
        fd->inode = dirent->inode;

        ptr->element = fd;
        ptr->next = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(list_entry_t), "ext2_read_dir list entry");
        #else
        kmalloc(sizeof(list_entry_t));
        #endif
        ptr = ptr->next;

        (*size)++;
        u32 name_len_real = dirent->name_len; alignup(name_len_real, 4);
        offset += (u32)(8+name_len_real);
        length -= (u32)(8+name_len_real);
    }
    kfree(ptr);

    kfree(dirent_buffer);
    return ERROR_NONE;
}

/*
* Reads data from an ext2 file
* Returns ERROR_NONE if it went well, errno instead
*/
error_t ext2_read_file(fd_t* fd, void* buffer, u64 count)
{
    if(count > U32_MAX) return ERROR_FILE_OUT;
    return ext2_inode_read_content(fd->file, (u32) fd->offset, (u32) count, buffer);
}

/*
* Write data to an ext2 file
* Returns ERROR_NONE if it went well, errno instead
*/
error_t ext2_write_file(fd_t* fd, void* buffer, u64 count)
{
    if(count > U32_MAX) return ERROR_FILE_OUT;
    return ext2_inode_write_content(fd->file, (u32) fd->offset, (u32) count, buffer);
}

/*
* This function opens node corresponding to file 'name' in directory 'dir'
*/
fsnode_t* ext2_open(fsnode_t* dir, char* name)
{
    u64 length = dir->length;
    u32 name_len = strlen(name);

    u8* dirent_buffer = kmalloc((u32) length);
    error_t readop = ext2_inode_read_content(dir, 0, (u32) length, dirent_buffer);
    if(readop != ERROR_NONE) return 0;

    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) break;

        // kprintf("%llooking %s for %s...\n", 3, dirent->name, name);
        if(strcfirst((char*) dirent->name, name) == name_len) 
        {
            fsnode_t* tr = ext2_std_inode_read(dirent->inode, dir->file_system);
            kfree(dirent_buffer); 
            return tr;
        }

        u32 name_len_real = dirent->name_len; alignup(name_len_real, 4);
        offset += (u32)(8+name_len_real);
        length -= (u32)(8+name_len_real);
    }

    kfree(dirent_buffer);
    return 0;
}

/*
* This function removes dirent for file_name in directory dir and frees inode of file if hard_links == 0
*/
error_t ext2_unlink(char* file_name, fsnode_t* dir)
{
    error_t direntop = ext2_remove_dirent(file_name, dir);
    if(direntop != ERROR_NONE) return direntop;

    fsnode_t* file = ext2_open(dir, file_name);
    file->hard_links--;
    if(!file->hard_links)
    {
        ext2_inode_free(file);
    }
    else ext2_std_inode_write(file);

    return ERROR_NONE;
}

/*
* This function creates a new dirent for src_file in directory dir and set hard_links++ for inode
*/
error_t ext2_link(fsnode_t* src_file, char* file_name, fsnode_t* dir)
{
    ext2_node_specific_t* spe = src_file->specific;
    error_t direntop = ext2_create_dirent(spe->inode_nbr, file_name, dir);
    if(direntop != ERROR_NONE) return direntop;

    src_file->hard_links++;
    ext2_std_inode_write(src_file);

    return ERROR_NONE;
}

/*
* This function reads an inode from his number to an std fsnode_t
*/
static fsnode_t* ext2_std_inode_read(u32 inode, file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    /* try to read inode from the cache */
    list_entry_t* iptr = fs->inode_cache;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        ext2_node_specific_t* espe = element->specific;
        if(espe->inode_nbr == inode) return element;
        iptr = iptr->next;
        isize--;
    }

    /* the inode is not in the cache, we need to read it from disk*/
    //getting inode size from superblock or standard depending on ext2 version
    u32 inode_size = ((ext2->superblock->version_major >=1) ? ext2->superblock->inode_struct_size : 128);

    //calculating inode block group
    u32 inodes_per_blockgroup = ext2->superblock->inodes_per_blockgroup;
    u32 inode_block_group = (inode - 1) / inodes_per_blockgroup;

    //calculating inode index in the inode table
    u32 index = (inode - 1) % inodes_per_blockgroup;

    //calculating block_group_descriptor offset
    u32 bg_read_offset = inode_block_group*sizeof(ext2_block_group_descriptor_t);

    //reading block group descriptor
    ext2_block_group_descriptor_t inode_bg;
    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), bg_read_offset, (u8*) &inode_bg, sizeof(ext2_block_group_descriptor_t), fs->drive);
    
    //calculating inode location on the disk (based on inode table block start found in block group descriptor)
    u32 inode_read_offset = (index * inode_size);

    ext2_inode_t ext2_inode;

    //read the inode
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.starting_block_adress), inode_read_offset, (u8*) &ext2_inode, sizeof(ext2_inode_t), fs->drive);

    /* we read it successfully ; now we need to normalize it to fsnode_t */
    fsnode_t* std_node = kmalloc(sizeof(fsnode_t));

    std_node->file_system = fs;
    
    std_node->attributes = 0;
    if((ext2_inode.type_and_permissions >> 12) == 4) std_node->attributes |= FILE_ATTR_DIR;

    std_node->hard_links = ext2_inode.hard_links;

    std_node->length = ext2_inode.size_low;
    //if file is not a directory and fs has feature activated, file_size is 64 bits long (directory_acl is size_high)
    if((!(std_node->attributes & FILE_ATTR_DIR)) && (ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & EXT2_ROFEATURE_SIZE_64))
    {
        u64 size_high = ((u64) ext2_inode.directory_acl << 32);
        std_node->length |= size_high;
    }

    std_node->creation_time = ext2_inode.creation_time;
    std_node->last_access_time = ext2_inode.last_access_time;
    std_node->last_modification_time = ext2_inode.last_modification_time;

    ext2_node_specific_t* specific = kmalloc(sizeof(ext2_node_specific_t));
    specific->inode_nbr = inode;
    memcpy(specific->direct_block_pointers, &ext2_inode.direct_block_pointers, 15*sizeof(u32));
    std_node->specific = specific;

    /* now that we have a normalized fsnode_t*, we can cache it and return it */
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

/*
* This function writes an inode from memory to the disk
*/
static error_t ext2_std_inode_write(fsnode_t* node)
{
    file_system_t* fs = node->file_system;
    ext2fs_specific_t* ext2 = fs->specific;
    ext2_node_specific_t* specific = node->specific;
    u32 inode = specific->inode_nbr;

    //getting inode size from superblock or standard depending on ext2 version
    u32 inode_size = ((ext2->superblock->version_major >=1) ? ext2->superblock->inode_struct_size : 128);

    //calculating inode block group
    u32 inodes_per_blockgroup = ext2->superblock->inodes_per_blockgroup;
    u32 inode_block_group = (inode - 1) / inodes_per_blockgroup;

    //calculating inode index in the inode table
    u32 index = (inode - 1) % inodes_per_blockgroup;

    //calculating block_group_descriptor offset
    u32 bg_read_offset = inode_block_group*sizeof(ext2_block_group_descriptor_t);

    //reading block group descriptor
    ext2_block_group_descriptor_t inode_bg;
    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);
    error_t readop0 = block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), bg_read_offset, (u8*) &inode_bg, sizeof(ext2_block_group_descriptor_t), fs->drive);
    if(readop0 != ERROR_NONE) return readop0;

    //calculating inode location on the disk (based on inode table block start found in block group descriptor)
    u32 inode_read_offset = (index * inode_size);

    ext2_inode_t ext2_inode;

    //read the inode
    error_t readop1 = block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.starting_block_adress), inode_read_offset, (u8*) &ext2_inode, sizeof(ext2_inode_t), fs->drive);
    if(readop1 != ERROR_NONE) return readop1;

    //change the informations of the inode to match 'node'
    ext2_inode.type_and_permissions = 0;
    if(node->attributes & FILE_ATTR_DIR) ext2_inode.type_and_permissions |= 0x4000;

    ext2_inode.size_low = node->length & 0xFFFFFFFF;
    //if file is not a directory and fs has feature activated, file_size is 64 bits long (directory_acl is size_high)
    if((!(node->attributes & FILE_ATTR_DIR)) && (ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & EXT2_ROFEATURE_SIZE_64))
    {
        ext2_inode.directory_acl = (u32)(node->length >> 32);
    }

    ext2_inode.creation_time = node->creation_time;
    ext2_inode.last_access_time = node->last_access_time;
    ext2_inode.last_modification_time = node->last_modification_time;

    memcpy(ext2_inode.direct_block_pointers, specific->direct_block_pointers, 15*sizeof(u32));

    //rewrite inode
    error_t writeop = block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.starting_block_adress), inode_read_offset, (u8*) &ext2_inode, sizeof(ext2_inode_t), fs->drive);
    return writeop;
}

/*
* This function reads the content of an ext2 file/inode (fsnode_t) into 'buffer'
*/
static error_t ext2_inode_read_content(fsnode_t* node, u32 offset, u32 size, void* buffer)
{
    file_system_t* fs = node->file_system;
    ext2fs_specific_t* ext2 = fs->specific;

    ext2_node_specific_t* inode = node->specific;

    //check for size conflict
    if(size+offset > node->length) return ERROR_FILE_OUT;

    //processing offset to get first block to read
    u32 first_block = 0;
    while(offset >= ext2->block_size) {offset -= ext2->block_size; first_block++;}

    u32 currentloc = 0;

    //reading 12 direct pointers at first
    u32 i;
    for(i=first_block;i<12;i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!inode->direct_block_pointers[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (direct block pointer %u ->0)\n", 0b00000110, i);
            return ERROR_FILE_CORRUPTED_FS;
        }

        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->direct_block_pointers[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;

        if(!size) return ERROR_NONE;
    }

    //resetting block counter
    i -= 12;

    //after that, reading singly indirect blocks (by caching the block that hold pointers)
    if(!inode->singly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->0)\n", 0b00000110);
        return ERROR_FILE_CORRUPTED_FS;
    }
    u32* singly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);

    for(;i<(ext2->block_size/4);i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!singly_indirect_block[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->direct block pointer->0)\n", 0b00000110);
            kfree(singly_indirect_block);
            return ERROR_FILE_CORRUPTED_FS;
        }

        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(singly_indirect_block[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;
        
        if(!size) {kfree(singly_indirect_block); return ERROR_NONE;}
    }
        
    kfree(singly_indirect_block);

    //resetting block counter
    i -= (ext2->block_size/4);

    //then, reading doubly indirect blocks (same way, just 2 loops instead of one)
    if(!inode->doubly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->0)\n", 0b00000110);
        return ERROR_FILE_CORRUPTED_FS;
    }
    u32* doubly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    
    u32 j;
    for(j = 0;j<(ext2->block_size/4);j++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!doubly_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->singly indirect pointer->0)\n", 0b00000110);
            kfree(doubly_indirect_block);
            return ERROR_FILE_CORRUPTED_FS;
        }
        u32* sib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

        for(;i<(ext2->block_size/4);i++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!sib[i]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly->singly->direct->0)\n", 0b00000110);
                kfree(sib); kfree(doubly_indirect_block);
                return ERROR_FILE_CORRUPTED_FS;
            }

            block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
            currentloc+=ext2->block_size;
            if(offset) offset = 0;

            if(size >= ext2->block_size) size -= ext2->block_size;
            else size = 0;
            
            if(!size) {kfree(sib); kfree(doubly_indirect_block); return ERROR_NONE;}
        }

        kfree(sib);
        //resetting block counter
        i -= (ext2->block_size/4);
    }

    kfree(doubly_indirect_block);

    //in the end, reading triply indirect blocks (same way, just 3 loops instead of 2)
    if(!inode->triply_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (triply indirect pointer->0)\n", 0b00000110);
        return ERROR_FILE_CORRUPTED_FS;
    }
    u32* triply_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);

    u32 k;
    for(k=0;k<(ext2->block_size/4);k++)
    {
        if(!triply_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (triply indirect pointer->doubly indirect pointer->0)\n", 0b00000110);
            kfree(triply_indirect_block);
            return ERROR_FILE_CORRUPTED_FS;
        }
        u32* dib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);

        for(j=0;j<(ext2->block_size/4);j++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!dib[j]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->0)\n", 0b00000110);
                kfree(triply_indirect_block); kfree(dib);
                return ERROR_FILE_CORRUPTED_FS;
            }
            u32* sib = kmalloc(ext2->block_size);
            block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

            for(;i<(ext2->block_size/4);i++)
            {
                //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
                if(!sib[i]) 
                {
                    kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->direct->0)\n", 0b00000110);
                    kfree(sib); kfree(dib); kfree(triply_indirect_block);
                    return ERROR_FILE_CORRUPTED_FS;
                }

                block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
                currentloc+=ext2->block_size;
                if(offset) offset = 0;

                if(size >= ext2->block_size) size -= ext2->block_size;
                else size = 0;
            
                if(!size) {kfree(sib); kfree(dib); kfree(triply_indirect_block); return ERROR_NONE;}
            }

            kfree(sib);
            //resetting block counter
            i -= (ext2->block_size/4);
        }

        kfree(dib);
    }

    kfree(triply_indirect_block);

    return ERROR_NONE;
}

/*
* This function writes the content of 'buffer' to an ext2 file/inode (fsnode_t)
*/
static error_t ext2_inode_write_content(fsnode_t* inode_desc, u32 offset, u32 size, void* buffer)
{
    file_system_t* fs = inode_desc->file_system;
    ext2fs_specific_t* ext2 = fs->specific;

    ext2_node_specific_t* inode = inode_desc->specific;

    if(fs->flags & FS_FLAG_READ_ONLY) return ERROR_FILE_SYSTEM_READ_ONLY;

    //resetting inode size (it will have a larger size cause we write to it)
    if(size + offset > inode_desc->length) inode_desc->length = (size+offset);

    //checking offsets conflicts
    if(offset > inode_desc->length) return ERROR_FILE_OUT;

    //processing offset to get first block to write
    u32 first_block = 0;
    while(offset >= ext2->block_size) {offset -= ext2->block_size; first_block++;}

    u32 currentloc = 0;

    //writing 12 direct pointers at first
    u32 i;
    for(i=first_block;i<12;i++)
    {
        //in case the pointer is invalid, we allocate a new direct block
        if(!inode->direct_block_pointers[i]) 
        {
            u32 block = ext2_block_alloc(fs);
            if(!block) return 1; //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
            inode->direct_block_pointers[i] = block;
        }

        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->direct_block_pointers[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;

        if(!size) 
        {
            //rewrite inode
            return ext2_std_inode_write(inode_desc);
        }
    }

    //resetting block counter
    i -= 12;

    //after that, writing singly indirect blocks
    u32* singly_indirect_block = kmalloc(ext2->block_size);
    if(!inode->singly_indirect_block_pointer)
    {
        u32 block = ext2_block_alloc(fs);
        if(!block) return 1; //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
        inode->singly_indirect_block_pointer = block;
        memset(singly_indirect_block, 0, ext2->block_size);
    }  
    else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);

    for(;i<(ext2->block_size/4);i++)
    {
        //in case the pointer is invalid, we allocate a new block
        if(!singly_indirect_block[i]) 
        {
            u32 block = ext2_block_alloc(fs);
            if(!block) {return 1; kfree(singly_indirect_block);} //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
            singly_indirect_block[i] = block;
        }

        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(singly_indirect_block[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;
            
        if(!size)
        {
            //rewrite singly indirect block on disk
            block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);
            kfree(singly_indirect_block); 
            //rewrite inode
            return ext2_std_inode_write(inode_desc);
        }
    }
    
    //rewrite singly indirect block on disk
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);
    kfree(singly_indirect_block);

    //resetting block counter
    i -= (ext2->block_size/4);

    //then, writing doubly indirect blocks (same way, just 2 loops instead of one)
    u32* doubly_indirect_block = kmalloc(ext2->block_size);
    if(!inode->doubly_indirect_block_pointer)
    {
        u32 block = ext2_block_alloc(fs);
        if(!block) return 1; //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
        inode->doubly_indirect_block_pointer = block;
        memset(doubly_indirect_block, 0, ext2->block_size);
    }
    else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    
    u32 j;
    for(j = 0;j<(ext2->block_size/4);j++)
    {
        u32* sib = kmalloc(ext2->block_size);
        //in case the pointer is invalid, we allocate a new block
        if(!doubly_indirect_block[j]) 
        {
            u32 block = ext2_block_alloc(fs);
            if(!block) {kfree(doubly_indirect_block); return 1;} //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
            doubly_indirect_block[j] = block;
            memset(sib, 0, ext2->block_size);
        }
        else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

        for(;i<(ext2->block_size/4);i++)
        {
            //in case the pointer is invalid, we allocate a new block
            if(!sib[i]) 
            {
                u32 block = ext2_block_alloc(fs);
                if(!block) {kfree(doubly_indirect_block); kfree(sib); return 1;} //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
                sib[i] = block;
            }

            block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
            currentloc+=ext2->block_size;
            if(offset) offset = 0;

            if(size >= ext2->block_size) size -= ext2->block_size;
            else size = 0;
            
            if(!size) 
            {
                //rewriting sib
                block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);
                //rewriting doubly_indirect_block
                block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
                kfree(sib); kfree(doubly_indirect_block); 
                //rewrite inode
                return ext2_std_inode_write(inode_desc);
            }
        }

        //rewriting sib
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);
        kfree(sib);
        //resetting block counter
        i -= (ext2->block_size/4);
    }

    //rewriting doubly indirect block
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    kfree(doubly_indirect_block);

    //in the end, writing triply indirect blocks (same way, just 3 loops instead of 2)
    u32* triply_indirect_block = kmalloc(ext2->block_size);
    if(!inode->triply_indirect_block_pointer)
    {
        u32 block = ext2_block_alloc(fs);
        if(!block) return 1; //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
        inode->triply_indirect_block_pointer = block;
        memset(triply_indirect_block, 0, ext2->block_size);
    }
    else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);

    u32 k;
    for(k=0;k<(ext2->block_size/4);k++)
    {
        u32* dib = kmalloc(ext2->block_size);
        if(!triply_indirect_block[j]) 
        {
            u32 block = ext2_block_alloc(fs);
            if(!block) {return 1; kfree(triply_indirect_block); kfree(dib);} //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
            inode->triply_indirect_block_pointer = block;
            memset(dib, 0, ext2->block_size);
        }
        else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);

        for(j=0;j<(ext2->block_size/4);j++)
        {
            u32* sib = kmalloc(ext2->block_size);
            //in case the pointer is invalid, we allocate a new block
            if(!dib[j]) 
            {
                u32 block = ext2_block_alloc(fs);
                if(!block) {return 1; kfree(triply_indirect_block); kfree(dib); kfree(sib);} //TODO : Care, if we allocated blocks before, the inode is not flushed on disk, so it may cause space loss
                dib[j] = block;
                memset(sib, 0, ext2->block_size);
            }
            else block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

            for(;i<(ext2->block_size/4);i++)
            {
                //in case the pointer is invalid, we allocate a new block
                if(!sib[i]) 
                {
                    u32 block = ext2_block_alloc(fs);
                    if(!block) {kfree(sib); kfree(dib); kfree(triply_indirect_block); return 1;}
                    sib[j] = block;
                }

                block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
                currentloc+=ext2->block_size;
                if(offset) offset = 0;

                if(size >= ext2->block_size) size -= ext2->block_size;
                else size = 0;
            
                if(!size) 
                {
                    //rewriting sib
                    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);
                    //rewriting dib
                    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);
                    //rewriting triply indirect block
                    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);
                    
                    kfree(sib); kfree(dib); kfree(triply_indirect_block); 
                    
                    //rewrite inode
                    return ext2_std_inode_write(inode_desc);
                }
            }

            //rewriting sib
            block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);
            kfree(sib);

            //resetting block counter
            i -= (ext2->block_size/4);
        }

        //rewriting dib
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);
                    
        kfree(dib);
    }

    //rewriting triply indirect block
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);
    kfree(triply_indirect_block);
    
    //rewrite inode
    return ext2_std_inode_write(inode_desc);
}

/*
* This function removes a dirent from a directory using dirent name
*/
static error_t ext2_remove_dirent(char* name, fsnode_t* dir)
{
    //read the directory to determine offset to remove dirent
    u64 length = dir->length;

    u8* dirent_buffer = kmalloc((u32) length);
    error_t readop = ext2_inode_read_content(dir, 0, (u32) length, dirent_buffer);
    if(readop != ERROR_NONE) {kfree(dirent_buffer); return readop;}

    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) {kfree(dirent_buffer); return ERROR_FILE_NOT_FOUND;}

        u32 name_len_real = dirent->name_len; alignup(name_len_real, 4);

        if(strcfirst(name, (char*) dirent->name) == strlen(name)) 
        {memcpy(dirent_buffer+offset, dirent_buffer+offset+8+name_len_real, (u32) length-((u32)8+name_len_real)); break;}

        offset += (u32)(8+name_len_real);
        length -= (u32)(8+name_len_real);
    }

    //rewrite the buffer
    error_t writeop = ext2_inode_write_content(dir, offset, (u32) dir->length, (u8*) dirent_buffer);

    kfree(dirent_buffer);

    return writeop;
}

/*
* This function creates a dirent in a directory
*/
static error_t ext2_create_dirent(u32 inode, char* name, fsnode_t* dir)
{
    //prepare the dirent in memory
    ext2_dirent_t towrite;
    towrite.inode = inode;
    towrite.name_len = (u8) strlen(name);
    towrite.type = 0; //TODO : adapt type or name_len_high
    strcpy((char*) towrite.name, name);
    towrite.size = (u16) (8+towrite.name_len);
    alignup(towrite.size, 4);

    //read the directory to determine offset to add dirent
    u64 length = dir->length;

    u8* dirent_buffer = kmalloc((u32) length);
    error_t readop = ext2_inode_read_content(dir, 0, (u32) length, dirent_buffer);
    if(readop != ERROR_NONE) {kfree(dirent_buffer); return readop;}
    
    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) break;
        u32 name_len_real = dirent->name_len; alignup(name_len_real, 4);
        offset += (u32)(8+name_len_real);
        length -= (u32)(8+name_len_real);
    }
    
    kfree(dirent_buffer);

    //write the dirent
    error_t writeop = ext2_inode_write_content(dir, offset, towrite.size, (u8*) &towrite);
    
    return writeop;
}

/*
* Allocates a new block
*/
static u32 ext2_block_alloc(file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);

    u32 i = 0;
    for(;i<ext2->blockgroup_count;i++)
    {
        //read bg_desc of blockgroup i
        ext2_block_group_descriptor_t bg_desc;
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), i*sizeof(ext2_block_group_descriptor_t),
        (u8*) &bg_desc, sizeof(ext2_block_group_descriptor_t), fs->drive);

        //read block bitmap of blockgroup i
        u8* bitmap_buffer = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg_desc.block_address_block_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);

        //find first free bit of bitmap
        u32 first_zero_bit = ext2_bitmap_mark_first_zero_bit(bitmap_buffer, ext2->block_size);
        if(first_zero_bit == ((u32)-1)) {kfree(bitmap_buffer); continue;} //this blockgroup has no free block
        
        //rewrite marked bitmap on disk
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg_desc.block_address_block_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);
    
        //calculate block to return
        u32 first_block = i*ext2->superblock->blocks_per_blockgroup + bg_desc.starting_block_adress;
        u32 tr = first_block + first_zero_bit;

        //update bg_desc and rewrite it
        bg_desc.unallocated_blocks--;
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), i*sizeof(ext2_block_group_descriptor_t),
        (u8*) &bg_desc, sizeof(ext2_block_group_descriptor_t), fs->drive);

        //update superblock and rewrite it
        ext2->superblock->unallocated_blocks--;
        block_write_flexible(ext2->superblock_offset+2, 0,
        (u8*) ext2->superblock, sizeof(ext2_superblock_t), fs->drive);

        //return the free block that we have marked
        return tr;
    }

    return 0;
}

/*
* Free an allocated block
*/
static void ext2_block_free(u32 block, file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    //calculating inode block group
    u32 blocks_per_blockgroup = ext2->superblock->blocks_per_blockgroup;
    u32 block_group = (block) / blocks_per_blockgroup;

    //calculating block_group_descriptor offset
    u32 bg_read_offset = block_group*sizeof(ext2_block_group_descriptor_t);

    //reading block group descriptor
    ext2_block_group_descriptor_t bg;
    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), bg_read_offset, (u8*) &bg, sizeof(ext2_block_group_descriptor_t), fs->drive);

    //reading block bitmap
    u8* bitmap_buffer = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg.block_address_block_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);

    //marking bit free in bitmap
    u32 bit_to_mark = block - block_group*ext2->superblock->blocks_per_blockgroup;
    ext2_bitmap_mark_bit_free(bitmap_buffer, bit_to_mark);

    //rewrite marked bitmap on disk
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg.block_address_block_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);
}

/*
* Allocates a new inode
*/
static u32 ext2_inode_alloc(file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);

    u32 i = 0;
    for(;i<ext2->blockgroup_count;i++)
    {
        //read bg_desc of blockgroup i
        ext2_block_group_descriptor_t bg_desc;
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table),  i*sizeof(ext2_block_group_descriptor_t),
        (u8*) &bg_desc, sizeof(ext2_block_group_descriptor_t), fs->drive);

        //read inode bitmap of blockgroup i
        u8* bitmap_buffer = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg_desc.block_address_inode_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);

        //find first free bit of bitmap
        u32 first_zero_bit = ext2_bitmap_mark_first_zero_bit(bitmap_buffer, ext2->block_size);
        if(first_zero_bit == ((u32)-1)) {kfree(bitmap_buffer); continue;} //this blockgroup has no free inode

        //rewrite marked bitmap on disk
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(bg_desc.block_address_inode_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);
        
        //calculate inode to return
        u32 inode = i*ext2->superblock->inodes_per_blockgroup + first_zero_bit + 1;

        //update bg_desc and rewrite it
        bg_desc.unallocated_inodes--;
        block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table),  i*sizeof(ext2_block_group_descriptor_t),
        (u8*) &bg_desc, sizeof(ext2_block_group_descriptor_t), fs->drive);

        //update superblock and rewrite it
        ext2->superblock->unallocated_inodes--;
        block_write_flexible(ext2->superblock_offset+2, 0,
        (u8*) ext2->superblock, sizeof(ext2_superblock_t), fs->drive);

        //return the free block that we have marked
        return inode;
    }

    return 0;
}

/*
* Free an allocated inode
*/
static void ext2_inode_free(fsnode_t* node)
{
    file_system_t* fs = node->file_system;
    ext2fs_specific_t* ext2 = fs->specific;
    ext2_node_specific_t* inode = node->specific;
    u32 inode_nbr = inode->inode_nbr;

    /* freeing inode blocks */
    u64 size = node->length;
    u32 i = 0;
    for(i=0;i<12;i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!inode->direct_block_pointers[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (direct block pointer %u ->0)\n", 0b00000110, i);
            return;// ERROR_FILE_CORRUPTED_FS;
        }

        ext2_block_free(inode->direct_block_pointers[i], fs);

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;

        if(!size) goto free_inode;
    }

    if(!inode->singly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->0)\n", 0b00000110);
        return;// ERROR_FILE_CORRUPTED_FS;
    }
    u32* singly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);

    for(i=0;i<(ext2->block_size/4);i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!singly_indirect_block[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->direct block pointer->0)\n", 0b00000110);
            kfree(singly_indirect_block);
            return;// ERROR_FILE_CORRUPTED_FS;
        }

        ext2_block_free(singly_indirect_block[i], fs);

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;
        
        if(!size) {kfree(singly_indirect_block); ext2_block_free(inode->singly_indirect_block_pointer, fs); goto free_inode;}
    }
        
    kfree(singly_indirect_block);
    ext2_block_free(inode->singly_indirect_block_pointer, fs);

    if(!inode->doubly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->0)\n", 0b00000110);
        return;// ERROR_FILE_CORRUPTED_FS;
    }
    u32* doubly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    
    u32 j;
    for(j = 0;j<(ext2->block_size/4);j++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!doubly_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->singly indirect pointer->0)\n", 0b00000110);
            kfree(doubly_indirect_block);
            return;// ERROR_FILE_CORRUPTED_FS;
        }
        u32* sib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

        for(i=0;i<(ext2->block_size/4);i++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!sib[i]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly->singly->direct->0)\n", 0b00000110);
                kfree(sib); kfree(doubly_indirect_block);
                return;// ERROR_FILE_CORRUPTED_FS;
            }

            ext2_block_free(sib[i], fs);

            if(size >= ext2->block_size) size -= ext2->block_size;
            else size = 0;
            
            if(!size) {kfree(sib); kfree(doubly_indirect_block); ext2_block_free(doubly_indirect_block[j], fs); ext2_block_free(inode->doubly_indirect_block_pointer, fs); goto free_inode;}
        }

        kfree(sib);
        ext2_block_free(doubly_indirect_block[j], fs);
    }

    kfree(doubly_indirect_block);
    ext2_block_free(inode->doubly_indirect_block_pointer, fs);

    if(!inode->triply_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (triply indirect pointer->0)\n", 0b00000110);
        return;// ERROR_FILE_CORRUPTED_FS;
    }
    u32* triply_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);

    u32 k;
    for(k=0;k<(ext2->block_size/4);k++)
    {
        if(!triply_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (triply indirect pointer->doubly indirect pointer->0)\n", 0b00000110);
            kfree(triply_indirect_block);
            return;// ERROR_FILE_CORRUPTED_FS;
        }
        u32* dib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);

        for(j=0;j<(ext2->block_size/4);j++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!dib[j]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->0)\n", 0b00000110);
                kfree(triply_indirect_block); kfree(dib);
                return;// ERROR_FILE_CORRUPTED_FS;
            }
            u32* sib = kmalloc(ext2->block_size);
            block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

            for(i=0;i<(ext2->block_size/4);i++)
            {
                //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
                if(!sib[i]) 
                {
                    kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->direct->0)\n", 0b00000110);
                    kfree(sib); kfree(dib); kfree(triply_indirect_block);
                    return;// ERROR_FILE_CORRUPTED_FS;
                }

                ext2_block_free(sib[i], fs);

                if(size >= ext2->block_size) size -= ext2->block_size;
                else size = 0;
            
                if(!size) {kfree(sib); kfree(dib); kfree(triply_indirect_block); ext2_block_free(dib[j], fs); ext2_block_free(triply_indirect_block[k], fs); ext2_block_free(inode->triply_indirect_block_pointer, fs); goto free_inode;}
            }

            kfree(sib);
            ext2_block_free(dib[j], fs);
        }

        kfree(dib);
        ext2_block_free(triply_indirect_block[k], fs);
    }

    kfree(triply_indirect_block);
    ext2_block_free(inode->triply_indirect_block_pointer, fs);

    /* freeing the inode */
    free_inode:
    {
    //calculating inode block group
    u32 inodes_per_blockgroup = ext2->superblock->inodes_per_blockgroup;
    u32 inode_block_group = (inode_nbr - 1) / inodes_per_blockgroup;

    //calculating block_group_descriptor offset
    u32 bg_read_offset = inode_block_group*sizeof(ext2_block_group_descriptor_t);

    //reading block group descriptor
    ext2_block_group_descriptor_t inode_bg;
    u32 block_group_descriptor_table = (ext2->block_size == 1024 ? 2:1);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(block_group_descriptor_table), bg_read_offset, (u8*) &inode_bg, sizeof(ext2_block_group_descriptor_t), fs->drive);

    //reading inode bitmap
    u8* bitmap_buffer = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.block_address_inode_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);

    //marking bit free in bitmap
    u32 bit_to_mark = inode_nbr - 1 - inode_block_group*ext2->superblock->inodes_per_blockgroup;
    ext2_bitmap_mark_bit_free(bitmap_buffer, bit_to_mark);

    //rewrite marked bitmap on disk
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.block_address_inode_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);

    /* free inode from the cache */
    //TODO: here we assume that inode we want to free is not the first in the cache (cause the first is theorically inode 2)
    //this is a little risquy tho
    list_entry_t* iptr = fs->inode_cache;
    list_entry_t* last = 0;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        ext2_node_specific_t* spe = element->specific;
        if(spe->inode_nbr == inode_nbr) {last->next = iptr->next; kfree(element->specific); kfree(element); kfree(iptr);}
        last = iptr;
        iptr = iptr->next;
        isize--;
    }
    }
}

static u32 ext2_bitmap_mark_first_zero_bit(u8* bitmap, u32 len)
{
    u32 i = 0;
    for(;i<len;i++)
    {
        u8 j = 0;
        for(;j<8;j++)
        {
            if((*(bitmap+i) & (1 << j)) == 0) 
            {
                // TODO : this is just gcc warning going crazy
                // if i do *(bitmap+i) |= ((u8) (1 << j)); i get a warning
                // but with e = (u8) (1 << j); and *(bitmap+i) |= e; i dont...
                u8 e = (u8) (1 << j);
                *(bitmap+i) |= e;
                return (i*8)+j;
            }
        }
    }
    return ((u32)-1);
}

static void ext2_bitmap_mark_bit_free(u8* bitmap, u32 bit)
{
    u32 byte = bit/8;
    u32 inbit = bit%8;
    *(bitmap+byte) &= (0 << inbit);
}
