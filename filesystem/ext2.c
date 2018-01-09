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

typedef struct EXT2_SUPERBLOCK
{
    u32 inodes;
    u32 blocks;
    u32 superuser_reserved_blocks;
    u32 unallocated_blocks;
    u32 unallocated_inodes;
    u32 superblock_number;
    u32 block_size; //log2(block size) - 10 (real_block size = 1024 << block_size)
    u32 fragment_size; //log2(fragment_size) - 10 (real_fragment_size = 1024 << fragment_size)
    u32 blocks_per_blockgroup;
    u32 fragments_per_blockgroup;
    u32 inodes_per_blockgroup;
    u32 last_mount_time;
    u32 last_written_time;
    u16 mounted_times_since_fsck;
    u16 mounts_before_fsck;
    u16 signature; // = 0xef53
    u16 file_system_state; // 1=clean, 2=errors
    u16 error_action; // 1=ignore error, 2=remount as read-only, 3=kernel panic
    u16 version_minor;
    u32 last_fsck_time;
    u32 time_between_fsck;
    u32 creator_os_id; // 0=linux, 1=hurd, 2=MASIX, 3=FreeBSD, 4=BSDs/XNU.. (BSDLite derivatives)
    u32 version_major;
    u16 reserved_user_id; //user id that can use reserved blocks
    u16 reserved_group_id; //group id that can use reserved blocks
    
    //if version_major >= 1
    u32 first_nonreserved_inode;
    u16 inode_struct_size;
    u16 block_group; //block group of this superblock (if backup copy)
    u32 optional_features;
    u32 required_features; // 0x1: compression, 0x2:dirents contain type field, 0x4:fs needs to replay journal, 0x8:fs use journal device
    u32 read_only_features; // 0x1: , 0x2:fs use 64-bits file size, 0x4:dir contents are stored as binary tree
    u8 fs_id[16];
    u8 volume_name[16];
    u8 last_mount_path[64];
    u32 compression_algorithm_used;
    u8 file_preallocate_blocks;
    u8 dir_preallocate_blocks;
    u16 unused;
    u8 journal_id[16];
    u32 journal_inode;
    u32 journal_device;
    u32 orphan_inode_list_head;
} __attribute__((packed)) ext2_superblock_t;

typedef struct EXT2_BLOCK_GROUP_DESCRIPTOR
{
    u32 block_address_block_usage; //of block usage bitmap
    u32 block_adress_inode_usage; //of inode usage bitmap
    u32 starting_block_adress; //of inode table
    u16 unallocated_blocks;
    u16 unallocated_inodes;
    u16 directories;
    u8 unused[14];
} __attribute__((packed)) ext2_block_group_descriptor_t;

typedef struct EXT2_INODE
{
    u16 type_and_permissions;
    u16 user_id;
    u32 size_low;
    int32_t last_access_time;
    int32_t creation_time;
    int32_t last_modification_time;
    int32_t deletion_time;
    u16 group_id;
    u16 hard_links;
    u32 used_disk_sectors;
    u32 flags;
    u32 os_specific_1;
    u32 direct_block_pointers[12];
    u32 singly_indirect_block_pointer;
    u32 doubly_indirect_block_pointer;
    u32 triply_indirect_block_pointer;
    u32 generation_number;
    u32 extended_attributes; //if version_major >= 1
    u32 directory_acl; //if version_major >= 1 ; if file and fs using 64bit-file-size, size_high.
    u32 fragment_block_adress;
    u8 os_specific_2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct EXT2_DIRENT
{
    u32 inode;
    u16 size;
    u8 name_len;
    u8 type; //or name_len_high, if feature "directory entries have file type byte" not set
    //types: 0=unknown, 1=regular, 2=dir, 3=character device, 4=block device, 5=FIFO, 6=socket, 7=symlink
    u8 name[];
} __attribute__((packed)) ext2_dirent_t;

//disk block offset relative to the superblock, in bytes
#define BLOCK_OFFSET(block) ((block-1)*ext2->block_size)
static ext2_inode_t* ext2_inode_read(u32 inode, file_system_t* fs);
static void ext2_get_fd(file_descriptor_t* dest, ext2_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs);
static u8 ext2_inode_read_content(ext2_inode_t* inode, file_system_t* fs, u32 offset, u32 size, u8* buffer);

/*
* Init an ext2 filesystem on a volume
*/
file_system_t* ext2fs_init(block_device_t* drive, u8 partition)
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

    //allocating specific data struct
    ext2fs_specific_t* ext2spe = kmalloc(sizeof(ext2fs_specific_t));
    ext2spe->superblock = superblock;
    ext2spe->superblock_offset = start_offset+2;
    ext2spe->block_size = (u32)(1024 << superblock->block_size);
    
    tr->specific = ext2spe;

    //root directory file descriptor creation
    //creating a fake dirent for this
    ext2_dirent_t rdirent;
    rdirent.inode = 2;
    rdirent.name_len = 0;
    ext2_get_fd(&tr->root_dir, &rdirent, 0, tr);

    return tr;
}

/*
* Reads a dir from an ext2 fs
* Returns 0 if it fails, or a pointer to the dirent list if it suceed
*/
list_entry_t* ext2fs_read_dir(file_descriptor_t* dir, u32* size)
{
    file_system_t* fs = dir->file_system;
    u64 length = dir->length;
    ext2_inode_t* inode = (ext2_inode_t*) ((uintptr_t) dir->fsdisk_loc);

    u8* dirent_buffer = kmalloc((u32) length);
    if(ext2_inode_read_content(inode, fs, 0, (u32) length, dirent_buffer)) return 0;
    
    if(!dirent_buffer) return 0;

    list_entry_t* tr = kmalloc(sizeof(list_entry_t));
    list_entry_t* ptr = tr;
    *size = 0;

    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) break;

        file_descriptor_t* fd = 
        #ifdef MEMLEAK_DBG
        kmalloc(sizeof(file_descriptor_t), "ext2_read_dir dirent file_descriptor");
        #else
        kmalloc(sizeof(file_descriptor_t));
        #endif

        ext2_get_fd(fd, dirent, dir, fs);

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

    return tr;
}

/*
* Reads a file from an ext2 fs
* Returns 0 if it went well, 1 instead
*/
u8 ext2fs_read_file(file_descriptor_t* file, void* buffer, u64 count)
{
    if(count > U32_MAX) return 1;
    ext2_inode_t* inode = (ext2_inode_t*)((uintptr_t)file->fsdisk_loc);
    return ext2_inode_read_content(inode, file->file_system, (u32) file->offset, (u32) count, buffer);
}

/*
* Read the content of an inode (the direct and indirect blocks)
* Returns 0 if it went well, 1 instead
* TODO: Optimisations
*/
static u8 ext2_inode_read_content(ext2_inode_t* inode, file_system_t* fs, u32 offset, u32 size, u8* buffer)
{
    ext2fs_specific_t* ext2 = fs->specific;

    //processing inode size
    u64 inode_size = inode->size_low;
    if((ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & 0x2))
    {
        u64 size_high = ((u64) inode->directory_acl << 32);
        inode_size |= size_high;
    }

    //checking size conflicts
    if(size+offset > inode_size) return 0;

    u32 currentloc = 0;

    //processing offset to get first block to read
    u32 first_block = 0;
    while(offset >= ext2->block_size) {offset -= ext2->block_size; first_block++;}

    //reading 12 direct pointers at first
    u32 i;
    for(i=first_block;i<12;i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!inode->direct_block_pointers[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (direct block pointer->0)\n", 0b00000110);
            return 1;
        }

        block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(inode->direct_block_pointers[i])+offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;

        if(!size) return 0;
    }

    //resetting block counter
    i -= 12;

    //after that, reading singly indirect blocks (by caching the block that hold pointers)
    if(!inode->singly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->0)\n", 0b00000110);
        return 1;
    }
    u32* singly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(inode->singly_indirect_block_pointer), (u8*) singly_indirect_block, ext2->block_size, fs->drive);

    for(;i<(ext2->block_size/4);i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!singly_indirect_block[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->direct block pointer->0)\n", 0b00000110);
            kfree(singly_indirect_block);
            return 1;
        }

        block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(singly_indirect_block[i])+offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
        currentloc+=ext2->block_size;
        if(offset) offset = 0;

        if(size >= ext2->block_size) size -= ext2->block_size;
        else size = 0;
            
        if(!size) {kfree(singly_indirect_block); return 0;}
    }
        
    kfree(singly_indirect_block);

    //resetting block counter
    i -= (ext2->block_size/4);

    //then, reading doubly indirect blocks (same way, just 2 loops instead of one)
    if(!inode->doubly_indirect_block_pointer)
    {
        kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->0)\n", 0b00000110);
        return 1;
    }
    u32* doubly_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(inode->doubly_indirect_block_pointer), (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    
    u32 j;
    for(j = 0;j<(ext2->block_size/4);j++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!doubly_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly indirect pointer->singly indirect pointer->0)\n", 0b00000110);
            kfree(doubly_indirect_block);
            return 1;
        }
        u32* sib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(doubly_indirect_block[j]), (u8*) sib, ext2->block_size, fs->drive);

        for(;i<(ext2->block_size/4);i++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!sib[i]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly->singly->direct->0)\n", 0b00000110);
                kfree(sib); kfree(doubly_indirect_block);
                return 1;
            }

            block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(sib[i])+offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
            currentloc+=ext2->block_size;
            if(offset) offset = 0;

            if(size >= ext2->block_size) size -= ext2->block_size;
            else size = 0;
            
            if(!size) {kfree(sib); kfree(doubly_indirect_block); return 0;}
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
        return 1;
    }
    u32* triply_indirect_block = kmalloc(ext2->block_size);
    block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(inode->triply_indirect_block_pointer), (u8*) triply_indirect_block, ext2->block_size, fs->drive);

    u32 k;
    for(k=0;k<(ext2->block_size/4);k++)
    {
        if(!triply_indirect_block[j]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (triply indirect pointer->doubly indirect pointer->0)\n", 0b00000110);
            kfree(triply_indirect_block);
            return 1;
        }
        u32* dib = kmalloc(ext2->block_size);
        block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(triply_indirect_block[j]), (u8*) dib, ext2->block_size, fs->drive);

        for(j=0;j<(ext2->block_size/4);j++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!dib[j]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->0)\n", 0b00000110);
                kfree(triply_indirect_block); kfree(dib);
                return 1;
            }
            u32* sib = kmalloc(ext2->block_size);
            block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(dib[j]), (u8*) sib, ext2->block_size, fs->drive);

            for(;i<(ext2->block_size/4);i++)
            {
                //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
                if(!sib[i]) 
                {
                    kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->direct->0)\n", 0b00000110);
                    kfree(sib); kfree(dib); kfree(triply_indirect_block);
                    return 1;
                }

                block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(sib[i])+offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
                currentloc+=ext2->block_size;
                if(offset) offset = 0;

                if(size >= ext2->block_size) size -= ext2->block_size;
                else size = 0;
            
                if(!size) {kfree(sib); kfree(dib); kfree(triply_indirect_block); return 0;}
            }

            kfree(sib);
            //resetting block counter
            i -= (ext2->block_size/4);
        }

        kfree(dib);
    }

    kfree(triply_indirect_block);

    return 0;
}

/*
* Reads an inode struct from an inode number
*/
static ext2_inode_t* ext2_inode_read(u32 inode, file_system_t* fs)
{
    if(inode == 1) return 0; //bad blocks inode
    
    ext2fs_specific_t* ext2 = fs->specific;

    u32 inode_size = ((ext2->superblock->version_major >=1) ? ext2->superblock->inode_struct_size : 128);

    u32 inodes_per_blockgroup = ext2->superblock->inodes_per_blockgroup;
    u32 inode_block_group = (inode - 1) / inodes_per_blockgroup;

    u32 index = (inode - 1) % inodes_per_blockgroup;

    u32 bg_read_offset = inode_block_group*sizeof(ext2_block_group_descriptor_t);

    ext2_block_group_descriptor_t inode_bg;
    block_read_flexible(ext2->superblock_offset, BLOCK_OFFSET(2)+bg_read_offset, (u8*) &inode_bg, sizeof(ext2_block_group_descriptor_t), fs->drive);
    u32 inode_read_offset = BLOCK_OFFSET(inode_bg.starting_block_adress) + (index * inode_size);

    ext2_inode_t* tr = kmalloc(inode_size);
    block_read_flexible(ext2->superblock_offset, inode_read_offset, (u8*) tr, inode_size, fs->drive);
    return tr;
}

/*
* Get a file desciptor from an ext2 dirent
*/
static void ext2_get_fd(file_descriptor_t* dest, ext2_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    ext2_inode_t* inode = ext2_inode_read(dirent->inode, fs);

    //copy name
    dest->name = 
    #ifdef MEMLEAK_DBG
    kmalloc((u32) dirent->name_len+1, "ext2 fd name");
    #else
    kmalloc((u32) dirent->name_len+1);
    #endif
    *(dest->name+dirent->name_len) = 0;
    strncpy(dest->name, (char*) dirent->name, dirent->name_len);

    //set attributes
    dest->attributes = 0;
    if((inode->type_and_permissions >> 12) == 4) dest->attributes |= FILE_ATTR_DIR;
    
    //set basic infos
    dest->file_system = fs;
    dest->parent_directory = parent;
    dest->fsdisk_loc = (uintptr_t) inode;

    dest->length = inode->size_low;
    //if file is not a directory and fs has feature activated, file_size is 64 bits long (directory_acl is size_high)
    if((!(dest->attributes & FILE_ATTR_DIR)) && (ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & 0x2))
    {
        u64 size_high = ((u64) inode->directory_acl << 32);
        dest->length |= size_high;
    }
    dest->offset = 0;

    //set time
    dest->creation_time = inode->creation_time;
    dest->last_access_time = inode->last_access_time;
    dest->last_modification_time = inode->last_modification_time;
}
