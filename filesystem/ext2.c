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

#define EXT2_ROFEATURE_SIZE_64 0x2

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
    u32 block_address_inode_usage; //of inode usage bitmap
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

typedef struct EXT2_INODE_DESCRIPTOR
{
    u32 inode_number;
    ext2_inode_t inode;
} ext2_inode_descriptor_t;

//disk block offset relative to the superblock, in bytes
#define BLOCK_OFFSET(block) ((block)*(ext2->block_size/512))
static ext2_inode_descriptor_t* ext2_inode_read(u32 inode, file_system_t* fs);
static void ext2_get_fd(file_descriptor_t* dest, ext2_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs);
static u8 ext2_inode_read_content(ext2_inode_t* inode, file_system_t* fs, u32 offset, u32 size, u8* buffer);
static u8 ext2_inode_write_content(ext2_inode_descriptor_t* inode_desc, file_system_t* fs, u32 offset, u32 size, u8* buffer);
static u32 ext2_block_alloc(file_system_t* fs);
static u8 ext2_create_dirent(u32 inode, char* name, file_descriptor_t* dir);
static u32 ext2_bitmap_mark_first_zero_bit(u8* bitmap, u32 len);
static void ext2_inode_write(ext2_inode_descriptor_t* inode, file_system_t* fs);
static u8 ext2_remove_dirent(char* name, file_descriptor_t* dir);
static u32 ext2_inode_alloc(file_system_t* fs);
static void ext2_inode_free(u32 inode, file_system_t* fs);
static void ext2_bitmap_mark_bit_free(u8* bitmap, u32 bit);

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

    if(superblock->read_only_features & 5) //masking 64-bits file sizes (supported) //TODO: support sparse superblocks..
    {
        tr->flags |= FS_FLAG_READ_ONLY;
    }

    //allocating specific data struct
    ext2fs_specific_t* ext2spe = kmalloc(sizeof(ext2fs_specific_t));
    ext2spe->superblock = superblock;
    ext2spe->superblock_offset = start_offset;
    ext2spe->block_size = (u32)(1024 << superblock->block_size);
    
    ext2spe->blockgroup_count = (superblock->blocks-superblock->superblock_number)/superblock->blocks_per_blockgroup;
    if((superblock->blocks-superblock->superblock_number)%superblock->blocks_per_blockgroup) ext2spe->blockgroup_count++;

    ext2spe->inode_cache = 0;
    ext2spe->inode_cache_size = 0;
    
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
    ext2_inode_t* inode = &((ext2_inode_descriptor_t*) ((uintptr_t) dir->fsdisk_loc))->inode;

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
u8 ext2fs_read_file(fd_t* fd, void* buffer, u64 count)
{
    if(count > U32_MAX) return 1;
    ext2_inode_t* inode = &((ext2_inode_descriptor_t*)((uintptr_t)fd->file->fsdisk_loc))->inode;
    return ext2_inode_read_content(inode, fd->file->file_system, (u32) fd->offset, (u32) count, buffer);
}

/*
* Write a file to an ext2 fs
* Returns 0 if it went well, 1 instead
*/
u8 ext2fs_write_file(fd_t* fd, void* buffer, u64 count)
{
    if(count > U32_MAX) return 1;
    ext2_inode_descriptor_t* inode = (ext2_inode_descriptor_t*)((uintptr_t)fd->file->fsdisk_loc);
    return ext2_inode_write_content(inode, fd->file->file_system, (u32) fd->offset, (u32) count, buffer);
}

u8 ext2fs_link(file_descriptor_t* file, file_descriptor_t* newdir, char* newname)
{
    u32 inode = ((ext2_inode_descriptor_t*)((uintptr_t)file->fsdisk_loc))->inode_number;
    return ext2_create_dirent(inode, newname, newdir);
}

u8 ext2fs_unlink(file_descriptor_t* file)
{
    ext2_inode_descriptor_t* inode_desc = ((ext2_inode_descriptor_t*)((uintptr_t)file->fsdisk_loc));
    ext2_inode_t* inode = &inode_desc->inode;

    if(!ext2_remove_dirent(file->name, file->parent_directory)) return 0;
    inode->hard_links--;
    if(!inode->hard_links)
    {
        ext2_inode_free(inode_desc->inode_number, file->file_system);
    }
    else ext2_inode_write(inode_desc, file->file_system);

    return 1;
}

file_descriptor_t* ext2fs_create_file(char* name, u8 attributes, file_descriptor_t* dir)
{
    file_system_t* fs = dir->file_system;

    u32 inode_nbr = ext2_inode_alloc(fs);
    ext2_inode_t inode;
    
    //setting type and permissions
    inode.type_and_permissions = 0;
    if(attributes | FILE_ATTR_DIR) inode.type_and_permissions |= 0x4000;
    else inode.type_and_permissions |= 0x8000;

    //set user_id/permissions
    inode.user_id = 0;
    inode.group_id = 0;
    
    //set inode time
    time_t current_time = get_current_time_utc();
    inode.last_access_time = (int32_t) current_time;
    inode.creation_time = (int32_t) current_time;
    inode.last_access_time = (int32_t) current_time;
    inode.deletion_time = 0;

    //set inode size
    inode.size_low = 0;

    //set hard links
    inode.hard_links = 1;

    //TODO: set inode used disk sectors
    inode.used_disk_sectors = 0;

    //set inode flags
    inode.flags = 0;

    //set os specific values
    inode.os_specific_1 = 0;
    memset(inode.os_specific_2, 0, 12);

    //set block pointers
    u32 i = 0;
    for(;i<12;i++)
    {
        inode.direct_block_pointers[i] = 0;
    }
    inode.singly_indirect_block_pointer = 0;
    inode.doubly_indirect_block_pointer = 0;
    inode.triply_indirect_block_pointer = 0;

    //TODO : set generation number ?
    inode.generation_number = 0;

    //set extended attributes
    inode.extended_attributes = 0;
    inode.directory_acl = 0;

    //TODO: set fragment block adress ?
    inode.fragment_block_adress = 0;

    //get inode desc
    ext2_inode_descriptor_t inode_desc;
    inode_desc.inode_number = inode_nbr;
    inode_desc.inode = inode;

    //TODO: maybe cache inode
    ext2_inode_write(&inode_desc, fs);

    //create dirent
    ext2_create_dirent(inode_nbr, name, dir);

    ext2_dirent_t dirent;
    dirent.inode = inode_nbr;
    dirent.name_len = (u8) strlen(name);
    strcpy((char*) dirent.name, name); 

    file_descriptor_t* tr = kmalloc(sizeof(file_descriptor_t));
    ext2_get_fd(tr, &dirent,dir, fs);

    return tr;
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
    if((ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & EXT2_ROFEATURE_SIZE_64) && ((inode->type_and_permissions >> 12) != 4))
    {
        u64 size_high = ((u64) inode->directory_acl << 32);
        inode_size |= size_high;
    }

    //checking size conflicts
    if(size+offset > inode_size) return 1;

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
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (direct block pointer %u ->0)\n", 0b00000110, i);
            return 1;
        }

        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->direct_block_pointers[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
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
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->singly_indirect_block_pointer), 0, (u8*) singly_indirect_block, ext2->block_size, fs->drive);

    for(;i<(ext2->block_size/4);i++)
    {
        //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
        if(!singly_indirect_block[i]) 
        {
            kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (singly indirect pointer->direct block pointer->0)\n", 0b00000110);
            kfree(singly_indirect_block);
            return 1;
        }

        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(singly_indirect_block[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
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
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->doubly_indirect_block_pointer), 0, (u8*) doubly_indirect_block, ext2->block_size, fs->drive);
    
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
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(doubly_indirect_block[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

        for(;i<(ext2->block_size/4);i++)
        {
            //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
            if(!sib[i]) 
            {
                kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (doubly->singly->direct->0)\n", 0b00000110);
                kfree(sib); kfree(doubly_indirect_block);
                return 1;
            }

            block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
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
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode->triply_indirect_block_pointer), 0, (u8*) triply_indirect_block, ext2->block_size, fs->drive);

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
        block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(triply_indirect_block[j]), 0, (u8*) dib, ext2->block_size, fs->drive);

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
            block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(dib[j]), 0, (u8*) sib, ext2->block_size, fs->drive);

            for(;i<(ext2->block_size/4);i++)
            {
                //in case the pointer is invalid, but should not (inode size supposed to cover this area), we return fail
                if(!sib[i]) 
                {
                    kprintf("%v[WARNING] [SEVERE] EXT2 Filesystem might be corrupted (tip->dip->sip->direct->0)\n", 0b00000110);
                    kfree(sib); kfree(dib); kfree(triply_indirect_block);
                    return 1;
                }

                block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(sib[i]), offset, buffer+currentloc, size >= ext2->block_size ? ext2->block_size : size, fs->drive);
        
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
* Write the content of an inode (the direct and indirect blocks)
* Returns 0 if it went well, 1 instead
*/
static u8 ext2_inode_write_content(ext2_inode_descriptor_t* inode_desc, file_system_t* fs, u32 offset, u32 size, u8* buffer)
{
    if(fs->flags & FS_FLAG_READ_ONLY) return 1;
    ext2fs_specific_t* ext2 = fs->specific;
    ext2_inode_t* inode = &inode_desc->inode;

    //processing inode size
    u64 inode_size = inode->size_low;
    if((ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & EXT2_ROFEATURE_SIZE_64) && ((inode->type_and_permissions >> 12) != 4))
    {
        u64 size_high = ((u64) inode->directory_acl << 32);
        inode_size |= size_high;
    }

    //resetting inode size (it will have a larger size cause we write to it)
    if(size + offset > inode_size) inode->size_low = (size+offset);

    //checking offsets conflicts
    if(offset > inode_size) return 1;

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
            ext2_inode_write(inode_desc, fs);
            return 0;
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
            ext2_inode_write(inode_desc, fs);
            return 0;
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
                //rewrite inode
                ext2_inode_write(inode_desc, fs);
                kfree(sib); kfree(doubly_indirect_block); return 0;
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
                    //rewrite inode
                    ext2_inode_write(inode_desc, fs);
                    kfree(sib); kfree(dib); kfree(triply_indirect_block); 
                    return 0;
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
    ext2_inode_write(inode_desc, fs);

    return 0;
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

static void ext2_inode_free(u32 inode, file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    //calculating inode block group
    u32 inodes_per_blockgroup = ext2->superblock->inodes_per_blockgroup;
    u32 inode_block_group = (inode - 1) / inodes_per_blockgroup;

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
    u32 bit_to_mark = inode - 1 - inode_block_group*ext2->superblock->inodes_per_blockgroup;
    ext2_bitmap_mark_bit_free(bitmap_buffer, bit_to_mark);

    //rewrite marked bitmap on disk
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.block_address_inode_usage), 0, bitmap_buffer, ext2->block_size, fs->drive);
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

static u8 ext2_create_dirent(u32 inode, char* name, file_descriptor_t* dir)
{
    file_system_t* fs = dir->file_system;

    //prepare the dirent in memory
    ext2_dirent_t towrite;
    towrite.inode = inode;
    towrite.name_len = (u8) strlen(name);
    towrite.type = 0; //TODO : adapt type or name_len_high
    strcpy((char*) towrite.name, name);
    towrite.size = (u16) (8+towrite.name_len);
    alignup(towrite.size, 4);

    ext2_inode_descriptor_t* dir_inode_desc = ((ext2_inode_descriptor_t*) ((uintptr_t) dir->fsdisk_loc));
    ext2_inode_t* dir_inode = &dir_inode_desc->inode;

    //read the directory to determine offset to add dirent
    u64 length = dir->length;

    u8* dirent_buffer = kmalloc((u32) length);
    if(ext2_inode_read_content(dir_inode, fs, 0, (u32) length, dirent_buffer)) {kfree(dirent_buffer); return 0;}
    
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
    if(ext2_inode_write_content(dir_inode_desc, fs, offset, towrite.size, (u8*) &towrite)) return 0;

    return 1;
}

static u8 ext2_remove_dirent(char* name, file_descriptor_t* dir)
{
    file_system_t* fs = dir->file_system;

    ext2_inode_descriptor_t* dir_inode_desc = ((ext2_inode_descriptor_t*) ((uintptr_t) dir->fsdisk_loc));
    ext2_inode_t* dir_inode = &dir_inode_desc->inode;

    //read the directory to determine offset to remove dirent
    u64 length = dir->length;

    u8* dirent_buffer = kmalloc((u32) length);
    if(ext2_inode_read_content(dir_inode, fs, 0, (u32) length, dirent_buffer)) {kfree(dirent_buffer); return 0;}

    u32 offset = 0;
    while(length)
    {
        ext2_dirent_t* dirent = (ext2_dirent_t*)((u32) dirent_buffer+offset);
        if(!dirent->inode) {kfree(dirent_buffer); return 0;}

        u32 name_len_real = dirent->name_len; alignup(name_len_real, 4);

        if(strcfirst(name, (char*) dirent->name) == strlen(name)) {memcpy(dirent_buffer+offset, dirent_buffer+offset+8+name_len_real, (u32) length-((u32)8+name_len_real)); break;}

        offset += (u32)(8+name_len_real);
        length -= (u32)(8+name_len_real);
    }

    //rewrite the buffer
    if(ext2_inode_write_content(dir_inode_desc, fs, offset, (u32) dir->length, (u8*) dirent_buffer)) {kfree(dirent_buffer); return 0;}

    kfree(dirent_buffer);

    return 1;
}

/*
* Reads an inode struct from an inode number
*/
static ext2_inode_descriptor_t* ext2_inode_read(u32 inode, file_system_t* fs)
{
    if(inode == 1) return 0; //bad blocks inode

    ext2fs_specific_t* ext2 = fs->specific;

    //checks if the inode is in the cache
    list_entry_t* iptr = ext2->inode_cache;
    u32 isize = ext2->inode_cache_size;
    while(iptr && isize)
    {
        ext2_inode_descriptor_t* element = iptr->element;
        if(element->inode_number == inode) return element;
        iptr = iptr->next;
        isize--;
    }

    /* We need to read the inode from disk. For that, we have to find the block group and block_group_descriptor of this inode */

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

    //create a cache entry for this inode
    ext2_inode_descriptor_t* descriptor = kmalloc(sizeof(ext2_inode_descriptor_t));
    descriptor->inode_number = inode;
    
    /*
    * READ THE INODE (directly to the cache entry) (victory we found it !)
    * here we are reading only the first 128 bits of inode, because when it has more
    * we dont need the other values (for now) and the inode struct (&descriptor->inode) has only 128 bits space 
    */
    block_read_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.starting_block_adress), inode_read_offset, (u8*) &descriptor->inode, sizeof(ext2_inode_t), fs->drive);

    //add the cache entry to the cache (linked list)
    if(!ext2->inode_cache)
    {
        ext2->inode_cache = kmalloc(sizeof(list_entry_t));
        ext2->inode_cache->element = descriptor;
    }
    else
    {
        list_entry_t* ptr = ext2->inode_cache;
        list_entry_t* last = 0;
        u32 size = ext2->inode_cache_size;
        while(ptr && size)
        {
            last = ptr;
            ptr = ptr->next;
            size--;
        }
        ptr = kmalloc(sizeof(list_entry_t));
        ptr->element = descriptor;
        last->next = ptr;
    }

    ext2->inode_cache_size++;

    return descriptor;
}

/*
* Writes an inode from memory to the disk
*/
static void ext2_inode_write(ext2_inode_descriptor_t* inode_desc, file_system_t* fs)
{
    u32 inode = inode_desc->inode_number;
    ext2fs_specific_t* ext2 = fs->specific;

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
    u32 inode_write_offset = (index * inode_size);

    //write the inode
    block_write_flexible(ext2->superblock_offset+BLOCK_OFFSET(inode_bg.starting_block_adress), inode_write_offset, (u8*) &inode_desc->inode, sizeof(ext2_inode_t), fs->drive);
}

/*
* Get a file desciptor from an ext2 dirent
*/
static void ext2_get_fd(file_descriptor_t* dest, ext2_dirent_t* dirent, file_descriptor_t* parent, file_system_t* fs)
{
    ext2fs_specific_t* ext2 = fs->specific;

    ext2_inode_descriptor_t* inode = ext2_inode_read(dirent->inode, fs);

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
    if((inode->inode.type_and_permissions >> 12) == 4) dest->attributes |= FILE_ATTR_DIR;
    
    //set basic infos
    dest->file_system = fs;
    dest->parent_directory = parent;
    dest->fsdisk_loc = (uintptr_t) inode;

    dest->length = inode->inode.size_low;
    //if file is not a directory and fs has feature activated, file_size is 64 bits long (directory_acl is size_high)
    if((!(dest->attributes & FILE_ATTR_DIR)) && (ext2->superblock->version_major >= 1) && (ext2->superblock->read_only_features & EXT2_ROFEATURE_SIZE_64))
    {
        u64 size_high = ((u64) inode->inode.directory_acl << 32);
        dest->length |= size_high;
    }
    //TODO : maybe better scale dest->offset = 0;

    //set time
    dest->creation_time = inode->inode.creation_time;
    dest->last_access_time = inode->inode.last_access_time;
    dest->last_modification_time = inode->inode.last_modification_time;
}
