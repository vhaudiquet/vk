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

//this file provides support for the FAT32 file system
#include "system.h"
#include "storage/storage.h"
#include "memory/mem.h"
#include "filesystem/fs.h"

typedef struct BPB
{
	u8 jmp_code[3]; //JMP instruction to actual code, to avoid trying to execute data that isnt code
	char prgm_name[8]; //Name of the program that formatted disk
	u16 bytes_per_sector;
	u8 sectors_per_cluster;
	u16 reserved_sectors; //Number of reserved sectors, including boot sector
	u8 fats_number; // Number of FATs on the disk (backup copies)
	u16 root_dir_size; //Root dir size in ENTRY number (how many entries) (unused NOW) (osdev: number of directories)
	u16 total_sectors; //if total_sectors = 0, there are more than 65535 sectors, and the value is stored in "large_total_sectors"
	u8 disk_type; //(media type : HARD_DRIVE, FLOPPY_DISK, ...)
	u16 old_fat_size; //Sectors per FAT ; FAT12/FAT16 only (unused value)
	u16 sectors_per_track; //CHS addressing, unused now
	u16 heads; //same
	u32 hidden_sectors;
	u32 large_total_sectors; //This value is set if total_sectors = 0
	//FAT32 only PART
	u32 fat_size; //Sectors per FAT
	u16 flags;
	u16 fat_version_number; //first 8 bits = Major version, last 8bits = minor version
	u32 root_directory_cluster;
	u16 file_system_infos;
	u16 backup_boot_sector;
	char reserved[12];
	u8 drive_number; //Same as bios int 0x13 (0x00 = floppy disk, 0x80 = hard disk)
	u8 win_nt_flags; //Reserved
	u8 signature; //0x29 (default) or 0x28
	u32 serial_number;
	char volume_name[11];
	char system_id_string[8]; // = "FAT32"
	//offset 90 to 420 : boot code
	//offset 510 to 512 : 0xAA55 (bootable partition signature)
} __attribute__((packed)) bpb_t;

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN 0x02
#define FAT_ATTR_SYSTEM 0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE 0x20
#define FAT_ATTR_LFN (FAT_ATTR_READ_ONLY|FAT_ATTR_HIDDEN|FAT_ATTR_SYSTEM|FAT_ATTR_VOLUME_ID)

#define FAT_NAME_MAX 13

typedef struct FAT32_DIR_ENTRY
{
	u8 name[8]; //if name[0] (== 0x00 : end of directory) (== 0x05 : name[0] = 0xE5) (== 0xE5 : file deleted) (== 0x2E '.' ou '..')
	u8 extension[3];
	u8 attributes;
	u8 nt_reserved;
	u8 creation_time_tmilli; // ??? Creation time / 10 ms (between 0 and 199)
	u16 creation_time; //bits 15-11 : hours, 10-5 : minutes, 4-0 : seconds/2
	u16 creation_date; //bits 15-9 : year (0=1980, 127=2107), 8-5: month (J = 1), 4-0 : day
	u16 last_access_date; //same
	u16 first_cluster_high; //the high 16 bits of this entry's first cluster number (FAT 32 only)
	u16 last_modification_time;
	u16 last_modification_date;
	u16 first_cluster_low; //the low 16 bits of this entry's first cluster number
	u32 file_size; //in bytes
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct LFN_ENTRY
{
	u8 order;
	u16 firstn[5];
	u8 attributes;
	u8 type;
	u8 checksum;
	u16 nextn[6];
	u16 zero;
	u16 lastn[2];
} __attribute__((packed)) lfn_entry_t;

//Static methods : look at method body (FAT32 utilities basically)
static u64 fat32fs_cluster_to_lba(file_system_t* fs, u32 cluster);
static list_entry_t* fat32fs_get_cluster_chain(u32 fcluster, file_system_t* fs, u32* size);
static u8 fat32fs_read_fat(file_system_t* fs);
static void fat32fs_write_fat(file_system_t* fs);
static u32 fat32fs_get_free_cluster(file_system_t* fs);
static u32 fat32fs_gm_free_clusters(u32 nbr, file_system_t* fs);
static void fat32fs_get_old_name(u8* oldname, u8* oldext, u8* name, list_entry_t* dirnames, u32 dirsize);
static u8 fat32fs_lfn_checksum (u8* pFcbName);
static bool fat32fs_resize_dirent(file_descriptor_t* file, u32 nsize);
static bool fat32fs_delete_dirent(file_descriptor_t* file);

/*
* Initialize a new FAT32 filesystem on a volume
*/
file_system_t* fat32fs_init(block_device_t* drive, u8 partition)
{
	//read the fat32 BPB
	bpb_t* bpb = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(bpb_t), "FAT32 BPB");
	#else
	kmalloc(sizeof(bpb_t));
	#endif

	u32 offset;
	if(partition) offset = drive->partitions[partition-1]->start_lba;
	else offset = 0;

	u8 attempts = 0;
	while(block_read_flexible(offset, 0, (u8*) bpb, sizeof(bpb_t), drive) != DISK_SUCCESS)
	{
		attempts++;
		if(attempts > 2) {kfree(bpb); return 0;}
	}

	//check if it is a valid fat32 fs
	if(*bpb->system_id_string != 'F' || *(bpb->system_id_string+1) != 'A' || *(bpb->system_id_string+2) != 'T' || *(bpb->system_id_string+3) != '3' || *(bpb->system_id_string+4) != '2')
		return 0;

	//setup the file_system_t and fat32fs_specific_t struct that represents the fat32fs
	file_system_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(file_system_t), "FAT32FS struct");
	#else
	kmalloc(sizeof(file_system_t));
	#endif

	fat32fs_specific_t* spe = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(fat32fs_specific_t), "FAT32FS struct");
	#else
	kmalloc(sizeof(fat32fs_specific_t));
	#endif
	tr->specific = spe;

	spe->bpb = bpb;
	spe->bpb_offset = offset;
	tr->fs_type = FS_TYPE_FAT32;
	tr->flags = 0 | FS_FLAG_CASE_INSENSITIVE;
	tr->drive = drive;
	tr->partition = partition;

	tr->root_dir.name = 0;
	tr->root_dir.file_system = tr;
	tr->root_dir.fsdisk_loc = bpb->root_directory_cluster;
	tr->root_dir.attributes = FILE_ATTR_DIR;
	tr->root_dir.length = 0;

	//reading the FAT and caching it in memory
	spe->fat_table = 
	#ifdef MEMLEAK_DBG
	kmalloc(bpb->fat_size * bpb->bytes_per_sector, "FAT Table");
	#else
	kmalloc(bpb->fat_size * bpb->bytes_per_sector);
	#endif
	if(fat32fs_read_fat(tr))
	{
		kfree(bpb);
		kfree(spe->fat_table);
		kfree(tr);
		return 0;
	}

	fat32fs_get_free_cluster(tr);

	//returing the fat32 fs struct, setup done
	return tr;
}

/*
* Free the ressources used by a FAT32 filesystem that needs to be removed
*/
void fat32fs_close(file_system_t* fs)
{
	if(fs->fs_type != FS_TYPE_FAT32) return;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	kfree(spe->bpb);
	kfree(spe->fat_table);
	kfree(fs->specific);
	kfree(fs);
}

/*
* Read all the files and folders inside a directory into a linked list
*/
list_entry_t* fat32fs_read_dir(file_descriptor_t* dir, u32* size)
{
	if((dir->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) return 0;

	file_system_t* fs = dir->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 cluster = (u32) dir->fsdisk_loc;

	//get the cluster chain for this directory
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//here we have the full dir size, and we allocate an equivalent buffer
	u64 dir_size = cluss * spe->bpb->sectors_per_cluster * 512;
	fat32_dir_entry_t* dirents = 
	#ifdef MEMLEAK_DBG
	kmalloc((u32) dir_size, "fat32:list_dir dirents buffer");
	#else
	kmalloc((u32) dir_size);
	#endif
	fat32_dir_entry_t* buffer = dirents;

	//reading all clusters inside this buffer
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_read_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//freeing the cluster list, we dont need it, we read them already
	list_free(cluslist, (u32) cluss);

	//allocate the first list_entry of the list that we'll return
	list_entry_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(list_entry_t), "fat32: first dir entry (list_dir) tr");
	#else
	kmalloc(sizeof(list_entry_t));
	#endif
	list_entry_t* lbuf = tr;

	//processing all the entries, and adding them to the list or skipping them (for LFN as example)
	u32 falses_entries = 0;
	char* lfn_name = 0;
	u32 lfn_offset = 0;
	//u8 lfn_checksum = 0;
	for(i = 0;i<dir_size/sizeof(fat32_dir_entry_t);i++)
	{
		//kprintf("entry %s -- ", *(dirents[i].name) == 0x0 ? "end" : *(dirents[i].name) == 0xE5 ? "deleted" : dirents[i].attributes == FAT_ATTR_LFN ? "lfn" : "8.3");
		if(*(dirents[i].name) == 0x0) {falses_entries++; break;}
		if(*(dirents[i].name) == 0xE5) {falses_entries++; continue;}
		if(dirents[i].attributes == FAT_ATTR_LFN)
		{
			//long file name entry processing
			lfn_entry_t* lfn = (lfn_entry_t*) &dirents[i];
			//THIS is the part that is hmmm
			//ASSUMING that the first lfn we will find is the 'top' lfn, the higher in order
			//from what i have seen, this is true, but...
			u16 lfn_nbr = (lfn->order & 0x40) == 0x40 ? lfn->order ^ 0x40 : lfn->order;

			if(lfn_name == 0)
			{
				lfn_name = 
				#ifdef MEMLEAK_DBG
				kmalloc((u32) 13*lfn_nbr+1, "fat32:list_dir lfn name");
				#else
				kmalloc((u32) 13*lfn_nbr+1);
				#endif
				*(lfn_name+13*lfn_nbr) = 0;
				//lfn_checksum = lfn->checksum;
			}
			
			lfn_offset = 13*((u32) (lfn_nbr-1));

			u32 fff = 0;
			while(fff < 5) {*(lfn_name+fff+lfn_offset) = (char) lfn->firstn[fff]; fff++;}
			fff = 0;
			while(fff < 6) {*(lfn_name+fff+5+lfn_offset) = (char) lfn->nextn[fff]; fff++;}
			fff = 0;
			while(fff < 2) {*(lfn_name+fff+11+lfn_offset) = (char) lfn->lastn[fff]; fff++;}

			falses_entries++; continue;
		}

		file_descriptor_t* tf = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(file_descriptor_t), "fat32:list_dir file_descriptor element");
		#else
		kmalloc(sizeof(file_descriptor_t));
		#endif
	
		if(lfn_name != 0)
		{
			//long file name
			//TODO : check the checksum kprintf("checksum is %d (0x%X)\n", lfn_checksum, lfn_checksum);
			tf->name = lfn_name;
			lfn_name = 0;
			lfn_offset = 0;
		}
		else
		{
			//legacy FAT32 no lfn support
			tf->name = 
			#ifdef MEMLEAK_DBG
			kmalloc(FAT_NAME_MAX, "fat32:list_dir old fat name");
			#else
			kmalloc(FAT_NAME_MAX);
			#endif
			strncpy(tf->name, (char*) dirents[i].name, 8);
			strtrim(tf->name);
			if(dirents[i].extension[0] != ' ' && dirents[i].extension[0] != 0)
			{
				strncat(tf->name, ".", 1);
				strncat(tf->name, (char*) dirents[i].extension, 3);
			}
		}

		//kprintf("%lfname : %s\n", 3, tf->name);

		tf->file_system = fs;

		u32 file_cluster = (((u32)dirents[i].first_cluster_high) << 16) | ((u32)dirents[i].first_cluster_low);
		file_cluster &= 0x0FFFFFFF;
		tf->fsdisk_loc = file_cluster;
	
		tf->length = dirents[i].file_size;

		if((dirents[i].attributes & FAT_ATTR_DIRECTORY) == FAT_ATTR_DIRECTORY)
			tf->attributes = FILE_ATTR_DIR;
		else
			tf->attributes = 0;

		if((dirents[i].attributes & FAT_ATTR_HIDDEN) == FAT_ATTR_HIDDEN)
			tf->attributes |= FILE_ATTR_HIDDEN;

		tf->offset = 0;
		tf->parent_directory = dir;

		//gets and converts creation/access/modification time to std time
		//we know there can't be overflow here, and we are handling conversion mannually, so disable -Wconversion
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wconversion"
		u8 chour = dirents[i].creation_time >> 11;
		u8 cmins = (dirents[i].creation_time & 0x7FF) >> 5;
		u8 csecs = (dirents[i].creation_time & 0x1F)*2;
		u8 cday = dirents[i].creation_date & 0x1F;
		u8 cmonth = (dirents[i].creation_date & 0x1FF) >> 5;
		u8 cyear = (dirents[i].creation_date >> 9) - 20; //0 = 1980 ; we are only taking dates from 2000 for now
		tf->creation_time = convert_to_std_time(csecs, cmins, chour, cday, cmonth, cyear);

		u8 laday = dirents[i].last_access_date & 0x1F;
		u8 lamonth = (dirents[i].last_access_date & 0x1FF) >> 5;
		u8 layear = (dirents[i].last_access_date >> 9) - 20; //same
		tf->last_access_time = convert_to_std_time(0, 0, 0, laday, lamonth, layear);

		u8 lmhour = dirents[i].last_modification_time >> 11;
		u8 lmmins = (dirents[i].last_modification_time & 0x7FF) >> 5;
		u8 lmsecs = (dirents[i].last_modification_time & 0x1F)*2;
		u8 lmday = dirents[i].last_modification_date & 0x1F;
		u8 lmmonth = (dirents[i].last_modification_date & 0x1FF) >> 5;
		u8 lmyear = (dirents[i].last_modification_date >> 9) - 20; //same
		tf->last_modification_time = convert_to_std_time(lmsecs, lmmins, lmhour, lmday, lmmonth, lmyear);
		#pragma GCC diagnostic pop
	
		lbuf->element = tf;

		//kprintf("%lfile_descriptor : (%s) (0x%X)\n", 3, tf->name, tf->fsdisk_loc);
	
		lbuf->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "fat32:list_dir list entry");
		#else
		kmalloc(sizeof(list_entry_t));
		#endif
		lbuf = lbuf->next;
	}
	
	//freeing the last list entry, that is empty and unused
	kfree(lbuf);
	//freeing the first dirents buffer, unused cause we have processed the entries
	kfree(dirents);

	//returning the size of the list and the list of entries
	*size = (u32)(i-falses_entries+1);
	//kprintf("size = %u (i=%u, f=%u)\n", *size, i, falses_entries);
	return tr;
}

/*
* Read the data of a file
*/
u8 fat32fs_read_file(file_descriptor_t* file, void* buffer, u64 count)
{
	u64 offset = file->offset;
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	if(count+offset < spe->bpb->sectors_per_cluster*512)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, (u32) file->fsdisk_loc);
		return block_read_flexible(pos, (u32) offset, (u8*) buffer, count, fs->drive);
	}
	
	//alright, we want to read more than 1 cluster, so we'll need to get the whole cluster chain
	u32 cluster = (u32) file->fsdisk_loc;
	u64 cluss = 0;

	list_entry_t* cluslist = fat32fs_get_cluster_chain(cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	while(offset >= spe->bpb->sectors_per_cluster*512) {clusbuffer=clusbuffer->next; offset = (u64) (offset - ((u64) (spe->bpb->sectors_per_cluster*512)));}

	//reading all needed clusters inside this buffer
	u64 cluss2 = cluss;
	while(count > 0 && cluss2 > 0)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		if(count >= spe->bpb->sectors_per_cluster*512)
		{
			block_read_flexible(pos, (u32) offset, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
			count = ((u32)(count - ((u64)spe->bpb->sectors_per_cluster*512)));
			if(offset) offset = 0;
		}
		else
		{
			block_read_flexible(pos, (u32) offset, (u8*) buffer, count, fs->drive);
			if(offset) offset = 0;
			break;
		}
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
		cluss2--;
	}

	//freeing the cluster list, we dont need it, we read them already
	list_free(cluslist, (u32) cluss);

	//returning 0, everything went well
	return 0;
}

/*
* Write data to a file
*/
u8 fat32fs_write_file(file_descriptor_t* file, u8* buffer, u64 count)
{
	u64 count_bckp = count;
	u32 cluster = (u32) file->fsdisk_loc;
	u64 offset = file->offset;
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	//get the cluster chain for this file
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//get the first cluster to write
	u32 fclus_tw = 0;
	while(offset > spe->bpb->sectors_per_cluster*512) {offset = (u64) (offset - ((u64) spe->bpb->sectors_per_cluster*512)); fclus_tw++;}

	//get the last cluster to write
	u32 lclus_tw = fclus_tw;
	while(count > spe->bpb->sectors_per_cluster*512) {count = (u64) (count - ((u64) spe->bpb->sectors_per_cluster*512)); lclus_tw++;}

	if(lclus_tw > cluss)
	{
		//expand file size
		u32 needed_clusters = lclus_tw - fclus_tw;
		if(!needed_clusters) needed_clusters++;

		u32 fnc = fat32fs_gm_free_clusters(needed_clusters, fs);
		
		u32 clus_temp_i;
		for(clus_temp_i = 0; clus_temp_i < cluss-1; clus_temp_i++) 
		{
			clusbuffer = clusbuffer->next;
		}
		u32* last_cluster = (u32*) clusbuffer->element;

		u32* nclv = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "Cluster number, fat32.c:write_file"); 
		#else
		kmalloc(sizeof(u32)); 
		#endif
		*nclv = fnc; 

		clusbuffer->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "new cluster list element, fat32.c:write_file"); 
		#else 
		kmalloc(sizeof(list_entry_t)); 
		#endif
		clusbuffer->next->element = nclv;
		
		cluss++;

		spe->fat_table[*last_cluster] = fnc;

		fat32fs_write_fat(fs);
	}

	u32 i = fclus_tw;

	clusbuffer = cluslist;
	while(i) {clusbuffer = clusbuffer->next; i--;}

	for(i = fclus_tw; i <= lclus_tw; i++)
	{
		if(i == fclus_tw)
			block_write_flexible(fat32fs_cluster_to_lba(fs, *((u32*)clusbuffer->element)), (u32) offset, buffer, (i == lclus_tw ? count : ((u64) spe->bpb->sectors_per_cluster*512)), fs->drive);
		else
			block_write_flexible(fat32fs_cluster_to_lba(fs, *((u32*)clusbuffer->element)), 0, buffer, (i == lclus_tw ? count : ((u64) spe->bpb->sectors_per_cluster*512)), fs->drive);
		
		clusbuffer = clusbuffer->next;	
	}

	//freeing the cluster list
	list_free(cluslist, (u32) cluss);

	if(count_bckp+file->offset > file->length)
	{
		file->length = count_bckp+file->offset;
		//here we assume the function can't fail, because we wrote to the file so it obviously exists
		fat32fs_resize_dirent(file, ((u32)count_bckp)+((u32) file->offset));
	}

	return 0;
}

/*
* Create a new file
*/
file_descriptor_t* fat32fs_create_file(u8* name, u8 attributes, file_descriptor_t* dir)
{
	file_system_t* fs = dir->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	//get the parent directory cluster
	u32 dir_cluster = (u32) dir->fsdisk_loc;

	//get the cluster chain for the parent directory
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(dir_cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//here we have the full dir size, and we allocate an equivalent buffer
	u64 dir_size = cluss * spe->bpb->sectors_per_cluster * 512;
	fat32_dir_entry_t* dirents = 
	#ifdef MEMLEAK_DBG
	kmalloc((u32) dir_size, "fat32:create_file dir entries buffer");
	#else
	kmalloc((u32) dir_size);
	#endif
	fat32_dir_entry_t* buffer = dirents;

	//reading all clusters inside this buffer
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_read_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//getting the 'end of directory' entry and checking for name conflicts
	list_entry_t* names = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(list_entry_t), "fat32:create_file name storing (first)");
	#else
	kmalloc(sizeof(list_entry_t));
	#endif

	//initializing the lfn entries
	//each lfn entry can handle 13 chars
	u32 name_len = strlen((char*) name);
	u32 needed_lfns = name_len / 13;
	if(name_len % 13) needed_lfns++;

	list_entry_t* namesb = names;
	u32 namess = 0;
	char* lfn_name = 0;
	u32 lfn_offset = 0;
	u32 unused_entries = 0;
	for(i = 0;i<dir_size/sizeof(fat32_dir_entry_t);i++)
	{
		if(*(dirents[i].name) == 0x0) {break;}
		if(*(dirents[i].name) == 0xE5) {unused_entries++; continue;}
		if(dirents[i].attributes == FAT_ATTR_LFN)
		{
			//long file name entry processing
			lfn_entry_t* lfn = (lfn_entry_t*) &dirents[i];
			
			u16 lfn_nbr = (lfn->order & 0x40) == 0x40 ? lfn->order ^ 0x40 : lfn->order;
			
			if(lfn_name == 0)
			{
				lfn_name = 
				#ifdef MEMLEAK_DBG
				kmalloc((u32) 13*lfn_nbr+1, "fat32:create_file lfn name store");
				#else
				kmalloc((u32) 13*lfn_nbr+1);
				#endif
				*(lfn_name+13*lfn_nbr) = 0;
			}
						
			lfn_offset = 13*((u32) (lfn_nbr-1));
		
			u32 fff = 0;
			while(fff < 5) {*(lfn_name+fff+lfn_offset) = (char) lfn->firstn[fff]; fff++;}
			fff = 0;
			while(fff < 6) {*(lfn_name+fff+5+lfn_offset) = (char) lfn->nextn[fff]; fff++;}
			fff = 0;
			while(fff < 2) {*(lfn_name+fff+11+lfn_offset) = (char) lfn->lastn[fff]; fff++;}
		}
		else
		{
			//std entries processing
			if(lfn_name != 0)
			{
				//long file name
				if(!strcmpnc(lfn_name, (char*) name)) 
				{
					//kprintf("%lcant create, same long name...\n", 2);
					kfree(lfn_name); kfree(dirents); list_free_eonly(names, namess); 
					kfree(namesb); list_free(cluslist, (u32) cluss); 
					return 0;
				}
				kfree(lfn_name);
				lfn_name = 0;
				lfn_offset = 0;
			}

			//reinit deleted entries
			if(unused_entries) kprintf("unused_entries that were there: %d\n", unused_entries);
			unused_entries = 0;

			char tc[13] = {0};
			strncpy(tc, (char*) dirents[i].name, 8);
			strncat(tc, ".", 1);
			strncat(tc, (char*) dirents[i].extension, 3);
			if(!strcmpnc(tc, (char*) name)) 
			{
				//kprintf("%lcant create, same name...\n", 2); 
				kfree(dirents); list_free_eonly(names, namess); kfree(namesb); 
				list_free(cluslist, (u32) cluss); 
				return 0;
			}
			namesb->element = dirents[i].name;
			namess++;
			namesb->next = 
			#ifdef MEMLEAK_DBG
			kmalloc(sizeof(list_entry_t), "fat32:file_create names storing");
			#else
			kmalloc(sizeof(list_entry_t));
			#endif
			namesb = namesb->next;
		}
	}
	kfree(namesb);
	
	//reserve space for the file
	u32 file_cluster = fat32fs_gm_free_clusters(1, fs);

	//verify is the dir can actually handle those entries
	if(i+needed_lfns < dir_size/sizeof(fat32_dir_entry_t))
	{
		*(dirents[i+needed_lfns+1].name) = 0x0;
		buffer = dirents;
	}
	else
	{
		//worst case, we are on cluster end...
		//we need to allocate a new cluster to this directory, mark it as EOC and make the previous 'last cluster' point to it
		//kprintf("%vNEEDING more CLUSTER !\n", 0b00010011);
		clusbuffer = cluslist;
		u32 clus_temp_i;
		for(clus_temp_i = 0; clus_temp_i < cluss-1; clus_temp_i++) 
		{
			//kprintf("going next cluster\n"); 
			clusbuffer = clusbuffer->next;
		}
		u32* last_cluster = (u32*) clusbuffer->element;
		u32 new_cluster = fat32fs_gm_free_clusters(1, fs);
		
		u32* nclv = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "Cluster number, fat32.c:create_file"); 
		#else
		kmalloc(sizeof(u32)); 
		#endif
		*nclv = new_cluster; 
		clusbuffer->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "new cluster list element, fat32.c:create_file"); 
		#else 
		kmalloc(sizeof(list_entry_t)); 
		#endif
		clusbuffer->next->element = nclv;
		
		spe->fat_table[*last_cluster] = new_cluster;
		dir_size += (u64) spe->bpb->sectors_per_cluster * 512;
		cluss++;
		buffer = dirents = krealloc(dirents, (u32) dir_size);
		*(dirents[i+needed_lfns+1].name) = 0x0;
	}

	//initializing the 8.3 file entry
	dirents[i+needed_lfns].attributes = 0;
	if((attributes & FILE_ATTR_HIDDEN) == FILE_ATTR_HIDDEN) {dirents[i+needed_lfns].attributes |= FAT_ATTR_HIDDEN;}
	fat32fs_get_old_name(dirents[i+needed_lfns].name, dirents[i+needed_lfns].extension, name, names, namess);
	dirents[i+needed_lfns].file_size = 0;
	dirents[i+needed_lfns].first_cluster_low = (u16) (file_cluster << 16 >> 16);
	dirents[i+needed_lfns].first_cluster_high = (u16) (file_cluster >> 16);

	//free the dir old names
	list_free_eonly(names, namess);

	//back to the lfn entry
	u32 j = 0;
	
	//getting checksum
	u8 lfn_checksum = fat32fs_lfn_checksum(dirents[i+needed_lfns].name);
	//kprintf("cheksum is : %d (0x%X)\n", lfn_checksum, lfn_checksum);

	while(j < needed_lfns)
	{
		u32 curr_name_offset = ((needed_lfns-j-1)*13);

		lfn_entry_t* currlfn = (lfn_entry_t*) &dirents[i+j];
		//kprintf("writing lfn entry %d\n", i+j);

		currlfn->order = (u8) (j ? (needed_lfns-j) : (needed_lfns|0x40));
		currlfn->attributes = FAT_ATTR_LFN;
		currlfn->zero = 0;
		currlfn->checksum = lfn_checksum;

		u32 fff = 0;
		while(fff < 5) {currlfn->firstn[fff] = *(name+fff+curr_name_offset); fff++;}
		fff = 0;
		while(fff < 6) {currlfn->nextn[fff] = *(name+fff+curr_name_offset+5); fff++;}
		fff = 0;
		while(fff < 2) {currlfn->lastn[fff] = *(name+fff+curr_name_offset+11); fff++;}

		j++;
	}

	//write the buffer on the clusters
	clusbuffer = cluslist;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_write_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//free the cluster list
	list_free(cluslist, (u32) cluss);
	//free the dir entries buffer
	kfree(dirents);
	
	//write the fat
	fat32fs_write_fat(fs);

	//return the file_descriptor of the file we just created
	file_descriptor_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(file_descriptor_t), "File Descriptor struct (created FAT32, returned)");
	#else
	kmalloc(sizeof(file_descriptor_t));
	#endif
	
	tr->name = 
	#ifdef MEMLEAK_DBG
	kmalloc(strlen((char*) name)+1, "FD->name of file created on FAT32 (returned)");
	#else
	kmalloc(strlen((char*) name)+1);
	#endif
	strcpy(tr->name, (char*) name);
	tr->attributes = attributes;
	tr->file_system = fs;
	tr->fsdisk_loc = file_cluster;
	tr->length = 0;
	tr->offset = 0;
	tr->parent_directory = dir;
	return tr;
}

/*
* Delete a file
* Note : carefull, when used on a directory, the directory MUST be empty (and there is no check)
*/
bool fat32fs_delete_file(file_descriptor_t* file)
{
	//Step 1 : Delete dirent (mark it as 0xE5)
	if(!fat32fs_delete_dirent(file)) return false;
	
	//Step 2 : mark all the clusters of this file as free
	fat32fs_specific_t* spe = (fat32fs_specific_t*) file->file_system->specific;
	u32 clusize = 0;
	list_entry_t* clulist = fat32fs_get_cluster_chain((u32) file->fsdisk_loc, file->file_system, &clusize);
	list_entry_t* cluspoint = clulist;
	u32 i = 0;
	for(i = 0;i<clusize;i++)
	{
		spe->fat_table[*((u32*) cluspoint->element)] = 0;
		cluspoint = cluspoint->next;
	}
	list_free(clulist, clusize);
	fat32fs_write_fat(file->file_system);

	return true;
}

/*
* Rename a file
*
bool fat32fs_rename(file_descriptor_t* file, u8* newname)
{
	//modify dirent and file descriptor (fuck, lfns...)
}
*/

/*
* this function updates the size of a file inside the dirent
*/
static bool fat32fs_resize_dirent(file_descriptor_t* file, u32 nsize)
{
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	//get the parent directory cluster
	u32 dir_cluster = (u32) file->parent_directory->fsdisk_loc;

	//get the cluster chain for the parent directory
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(dir_cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//here we have the full dir size, and we allocate an equivalent buffer
	u64 dir_size = cluss * spe->bpb->sectors_per_cluster * 512;
	fat32_dir_entry_t* dirents = 
	#ifdef MEMLEAK_DBG
	kmalloc((u32) dir_size, "fat32:resize_dirent dir entries buffer");
	#else
	kmalloc((u32) dir_size);
	#endif
	fat32_dir_entry_t* buffer = dirents;

	//reading all clusters inside this buffer
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_read_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//finding the corresponding entries
	bool found = false;
	char* lfn_name = 0;
	u32 lfn_offset = 0;
	for(i = 0;i<dir_size/sizeof(fat32_dir_entry_t);i++)
	{
		//parsing the LFN entries
		if(*(dirents[i].name) == 0x0) {break;}
		if(*(dirents[i].name) == 0xE5) {continue;}
		if(dirents[i].attributes == FAT_ATTR_LFN)
		{
			//long file name entry processing
			lfn_entry_t* lfn = (lfn_entry_t*) &dirents[i];
			//THIS is the part that is hmmm
			//ASSUMING that the first lfn we will find is the 'top' lfn, the higher in order
			//from what i have seen, this is true, but...
			u16 lfn_nbr = (lfn->order & 0x40) == 0x40 ? lfn->order ^ 0x40 : lfn->order;

			if(lfn_name == 0)
			{
				lfn_name = 
				#ifdef MEMLEAK_DBG
				kmalloc((u32) 13*lfn_nbr+1, "fat32:resize_dirent lfn name");
				#else
				kmalloc((u32) 13*lfn_nbr+1);
				#endif
				*(lfn_name+13*lfn_nbr) = 0;
				//lfn_checksum = lfn->checksum;
			}
			
			lfn_offset = 13*((u32) (lfn_nbr-1));

			u32 fff = 0;
			while(fff < 5) {*(lfn_name+fff+lfn_offset) = (char) lfn->firstn[fff]; fff++;}
			fff = 0;
			while(fff < 6) {*(lfn_name+fff+5+lfn_offset) = (char) lfn->nextn[fff]; fff++;}
			fff = 0;
			while(fff < 2) {*(lfn_name+fff+11+lfn_offset) = (char) lfn->lastn[fff]; fff++;}

			continue;
		}

		if(lfn_name)
		{
			if(!strcmp(lfn_name, file->name))
			{
				//we have found the right 8.3 entry
				dirents[i].file_size = nsize;
				kfree(lfn_name);
				found = true;
				break;
			}
			kfree(lfn_name);
			lfn_name = 0;
			lfn_offset = 0;
		}
	}

	if(!found) 
	{
		//free the cluster list
		list_free(cluslist, (u32) cluss);
		//free the dir entries buffer
		kfree(dirents);
		return false;
	}

	//write the buffer on the clusters
	clusbuffer = cluslist;
	buffer = dirents;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_write_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//free the cluster list
	list_free(cluslist, (u32) cluss);
	//free the dir entries buffer
	kfree(dirents);

	return true;
}

/*
* this function marks a dirent and all lfns corresponding to as 0xE5, wich means unused
*/
static bool fat32fs_delete_dirent(file_descriptor_t* file)
{
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	//get the parent directory cluster
	u32 dir_cluster = (u32) file->parent_directory->fsdisk_loc;

	//get the cluster chain for the parent directory
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(dir_cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//here we have the full dir size, and we allocate an equivalent buffer
	u64 dir_size = cluss * spe->bpb->sectors_per_cluster * 512;
	fat32_dir_entry_t* dirents = 
	#ifdef MEMLEAK_DBG
	kmalloc((u32) dir_size, "fat32:delete_dirent dir entries buffer");
	#else
	kmalloc((u32) dir_size);
	#endif
	fat32_dir_entry_t* buffer = dirents;

	//reading all clusters inside this buffer
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_read_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//finding the corresponding entries
	bool found = false;
	char* lfn_name = 0;
	u32 lfn_offset = 0;
	for(i = 0;i<dir_size/sizeof(fat32_dir_entry_t);i++)
	{
		//parsing the LFN entries
		if(*(dirents[i].name) == 0x0) {break;}
		if(*(dirents[i].name) == 0xE5) {continue;}
		if(dirents[i].attributes == FAT_ATTR_LFN)
		{
			//long file name entry processing
			lfn_entry_t* lfn = (lfn_entry_t*) &dirents[i];
			//THIS is the part that is hmmm
			//ASSUMING that the first lfn we will find is the 'top' lfn, the higher in order
			//from what i have seen, this is true, but...
			u16 lfn_nbr = (lfn->order & 0x40) == 0x40 ? lfn->order ^ 0x40 : lfn->order;

			if(lfn_name == 0)
			{
				lfn_name = 
				#ifdef MEMLEAK_DBG
				kmalloc((u32) 13*lfn_nbr+1, "fat32:delete_dirent lfn name");
				#else
				kmalloc((u32) 13*lfn_nbr+1);
				#endif
				*(lfn_name+13*lfn_nbr) = 0;
				//lfn_checksum = lfn->checksum;
			}
			
			lfn_offset = 13*((u32) (lfn_nbr-1));

			u32 fff = 0;
			while(fff < 5) {*(lfn_name+fff+lfn_offset) = (char) lfn->firstn[fff]; fff++;}
			fff = 0;
			while(fff < 6) {*(lfn_name+fff+5+lfn_offset) = (char) lfn->nextn[fff]; fff++;}
			fff = 0;
			while(fff < 2) {*(lfn_name+fff+11+lfn_offset) = (char) lfn->lastn[fff]; fff++;}

			continue;
		}

		if(lfn_name)
		{
			if(!strcmp(lfn_name, file->name))
			{
				//we have found the right 8.3 entry
				*(dirents[i].name) = 0xE5; //marks 0xE5
				
				//marks all lfns
				u32 nlen = strlen(lfn_name); alignup(nlen, 13);
				u32 counter = 0;
				while(nlen)
				{
					*(dirents[i].name) = 0xE5;
					counter++;
					nlen -= 13;
				}

				kfree(lfn_name);
				found = true;
				break;
			}
			kfree(lfn_name);
			lfn_name = 0;
			lfn_offset = 0;
		}
	}

	if(!found) 
	{
		//free the cluster list
		list_free(cluslist, (u32) cluss);
		//free the dir entries buffer
		kfree(dirents);
		return false;
	}

	//write the buffer on the clusters
	clusbuffer = cluslist;
	buffer = dirents;
	for(i = 0; i < cluss; i++)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		block_write_flexible(pos, 0, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
	}

	//free the cluster list
	list_free(cluslist, (u32) cluss);
	//free the dir entries buffer
	kfree(dirents);

	return true;
}

/*
* this function converts cluster address to LBA address
*/
static u64 fat32fs_cluster_to_lba(file_system_t* fs, u32 cluster)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	return ((u64) ((int64_t) (spe->bpb_offset + spe->bpb->reserved_sectors + (spe->bpb->fats_number*spe->bpb->fat_size) + (cluster * spe->bpb->sectors_per_cluster)) - (2*spe->bpb->sectors_per_cluster)));
}

/*
* this function reads the fat from disk
*/
static u8 fat32fs_read_fat(file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 fat_size = spe->bpb->fat_size * spe->bpb->bytes_per_sector;
	u32 fat_sector = spe->bpb_offset + spe->bpb->reserved_sectors;
	//note : same as part->start_lba

	u8 attempts = 0;
	while(block_read_flexible(fat_sector, 0, (u8*) spe->fat_table, fat_size, fs->drive) != DISK_SUCCESS)
	{	
		attempts++;
		if(attempts > 2) return 1;
	}
	return 0;
}

/*
* this function writes the cached fat on disk
*/
static void fat32fs_write_fat(file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 fat_size = spe->bpb->fat_size * spe->bpb->bytes_per_sector;
	u32 fat_sector = spe->bpb_offset + spe->bpb->reserved_sectors;
	block_write_flexible(fat_sector, 0, (u8*) spe->fat_table, fat_size, fs->drive);
}

/*
* this function return all the clusters of a cluster chain inside a chained list (list_entry_t)
*/
static list_entry_t* fat32fs_get_cluster_chain(u32 fcluster, file_system_t* fs, u32* size)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	u32 cluster = fcluster;
	u32 cchain = 0;
	list_entry_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(list_entry_t), "fat32: cluster chain first entry");
	#else
	kmalloc(sizeof(list_entry_t));
	#endif
	list_entry_t* lbuf = tr;
	*size = 0;

	do
	{
		cchain = spe->fat_table[cluster] & 0x0FFFFFFF;
		
		u32* cl = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "fat32: cluster chain entry element (u32)");
		#else
		kmalloc(sizeof(u32));
		#endif
		*cl = cluster;
		lbuf->element = cl;

		lbuf->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "fat32: cluster chain list entry");
		#else
		kmalloc(sizeof(list_entry_t));
		#endif
		lbuf = lbuf->next;

		if(!cchain) kprintf("%v[WARNING] [SEVERE] Filesystem might be corrupted (->0)\n", 0b00000110);
		else if(cchain == 1) kprintf("%v[WARNING] [SEVERE] Filesystem might be corrupted (reserved cluster)\n", 0b00000110);
		else if(cchain == 0x0FFFFFF7) kprintf("%v[WARNING] [SEVERE] Filesystem might be corrupted (bad cluster)\n", 0b00000110);

		cluster = cchain;
		(*size)++;
	} while((cchain != 0) && !(cchain >= 0x0FFFFFF8));

	//free the last entry, unused cause empty
	kfree(lbuf);

	return tr;
}

/*
* this function returns the 8.3 name of a file
* this is still experimental and full of bugs but as we dont really care about 8.3 and the code is horrible i'll see that later
*/
static void fat32fs_get_old_name(u8* oldname, u8* oldext, u8* name, list_entry_t* dirnames, u32 dirsize)
{
	u8* namebuffer = name;

	while(*name == '.') name++;

	bool generate_tail = false;
	u8 temp[9] = {0};
	u8* dest = temp;
	u32 i = 0;
	while ((i < 8) && (*dest++ = toUpper(*namebuffer++))) 
	{
		if((*dest) && !isalnum(*dest) && (*dest < 127)) {kprintf("gtail : no127 (%c) (%u)\n", *dest, *dest); *dest = '_'; generate_tail = true;}
		if(*dest == ' '){dest--; generate_tail = true; kprintf("gtail : spaces\n");}
		else i++;
	}
	*(dest++) = 0;
	
	i = 0;
	while(*(temp+i) && *(temp+i) != '.' && i < 8)
	{
		*(oldname+i) = *(temp+i);
		i++;
		if(i==8 && *(temp+i)) {generate_tail = true; kprintf("gtail : name too big\n");}
	}

	u8* ext = (u8*) strrchr((char*) name, '.');
	if(name)
	{
		*oldext = toUpper(*(ext+1));
		*(oldext+1) = toUpper(*(ext+2));
		*(oldext+2) = toUpper(*(ext+3));
	}
	//if(strchr((char*) name, '.') != strrchr((char*) name, '.')) {generate_tail = true; kprintf("gtail : dots\n");}

	if(generate_tail)
	{
		kprintf("%lgenerating tail...\n", 3);
		u32 n = 1;
		u8 toCopy[7] = {0};
		*toCopy = '~';
		utoa(n, toCopy+1);
		u32 len = strlen((char*) toCopy);
		strncpy((char*) (oldext+7-len), (char*) toCopy, len);

		i = 0;
		while(i < dirsize)
		{
			u8* nn = (u8*) dirnames->element;
			if(!strcmpnc((char*) oldname, (char*) nn))
			{
				n++;
				u8 toCopy[7] = {0};
				*toCopy = '~';
				utoa(n, toCopy+1);
				u32 len = strlen((char*) toCopy);
				strncpy((char*) (oldext+7-len), (char*) toCopy, len);
			}
			list_entry_t* temp = dirnames;
			dirnames = dirnames->next;
			kfree(temp);
			i++;
		}
	}

	kprintf("%lname = %s\n", 3, name);
	kprintf("%lreturning old name : %s , old ext : %s\n", 3, oldname, oldext);
}

/*
* this function gets the checksum of a lfn entry from a name
* it uses microsoft fatgen103.doc implementation
*/
static u8 fat32fs_lfn_checksum (u8* pFcbName)
{
	u16 FcbNameLen;
	u8 Sum;

	Sum = 0;
	for (FcbNameLen=11; FcbNameLen!=0; FcbNameLen--) 
	{
		// NOTE: The operation is an unsigned char rotate right
		Sum = (u8) (((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++);
	}
	return (Sum);
}

/*
* this function returns the next free cluster on this fat32 file system
*/
static u32 fat32fs_get_free_cluster(file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 active_cluster = 0;
	u32 table_value = spe->fat_table[active_cluster] & 0x0FFFFFFF;
	while(table_value != 0) 
	{
		active_cluster++;
		table_value = spe->fat_table[active_cluster] & 0x0FFFFFFF;
	}
	return active_cluster;
}

/*
* this function gets and marks (as a right clusterchain) nbr clusters (that were previously free)
* it returns the first cluster on the chain
* note: this is getting the clusters in reverse order : possibly fragmenting disk ?
*/
static u32 fat32fs_gm_free_clusters(u32 nbr, file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	u32 active_cluster = 0;
	u32 last_free_cluster = 0;

	while(nbr)
	{
		u32 table_value = spe->fat_table[active_cluster] & 0x0FFFFFFF;
		while(table_value != 0) 
		{
			active_cluster++;
			table_value = spe->fat_table[active_cluster] & 0x0FFFFFFF;
		}
		//we have found a free cluster
		if(last_free_cluster == 0) spe->fat_table[active_cluster] = 0x0FFFFFFF;
		else spe->fat_table[active_cluster] = last_free_cluster;
		last_free_cluster = active_cluster;
		active_cluster++;
		nbr--;
	}
	return last_free_cluster;
}
