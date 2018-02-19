#ifndef EXT2_HEAD
#define EXT2_HEAD

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

typedef struct ext2_node_specific
{
    u32 inode_nbr;
    u32 direct_block_pointers[12];
    u32 singly_indirect_block_pointer;
    u32 doubly_indirect_block_pointer;
    u32 triply_indirect_block_pointer;
} ext2_node_specific_t;

typedef struct ext2fs_specific
{
    struct EXT2_SUPERBLOCK* superblock;
    u32 superblock_offset;
    u32 block_size;
    u32 blockgroup_count;
} ext2fs_specific_t;

file_system_t* ext2_init(block_device_t* drive, u8 partition);
fsnode_t* ext2_open(fsnode_t* dir, char* name);
error_t ext2fs_write_file(fd_t* fd, void* buffer, u64 count);
error_t ext2fs_read_file(fd_t* fd, void* buffer, u64 count);
error_t ext2_list_dir(list_entry_t* dest, fsnode_t* dir, u32* size);

#endif
