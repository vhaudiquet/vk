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

typedef struct ext2_node_specific
{
    u32 inode_nbr;
    u32 pointers[15];
} ext2_node_specific_t;

#define BLOCK_OFFSET(block) ((block)*(ext2->block_size/512))

static fsnode_t* ext2_std_inode_read(u32 inode, file_system_t* fs);
static error_t ext2_std_inode_write(fsnode_t* inode);

static error_t ext2_inode_read_content(fsnode_t* inode, u32 offset, u32 size, void* buffer, file_system_t* fs);
static error_t ext2_inode_write_content(fsnode_t* inode, u32 offset, u32 size, void* buffer, file_system_t* fs);

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
    memcpy(specific->pointers, &ext2_inode.direct_block_pointers, 15*sizeof(u32));
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
