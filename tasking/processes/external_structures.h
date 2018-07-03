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

#ifndef EXTERNAL_HEAD
#define EXTERNAL_HEAD

#include "system.h"

typedef struct statfs
{
    u32 f_type; //file system type
    u64 f_flags; //file system flags
    u32 f_bsize; //file system block size
    u64 f_blocks; //file system total blocks
    u64 f_bfree; //file system free blocks
    u64 f_bavail; //file system available to non-root blocks (= f_bfree)
    u64 f_files; //file system total files nodes
    u64 f_ffree; //file system free files nodes
    u32 f_fsid; //file system id
    char mount_path[100]; //file system mount path
} statfs_t;

typedef struct stat 
{
    u32 st_dev;
    u32 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u32 st_rdev;
    u32 st_size;
    u32 st_blksize;
    u32 st_blocks;
    time_t st_atime;
    time_t st_mtime;
    time_t st_ctime;
} stat_t;

#endif