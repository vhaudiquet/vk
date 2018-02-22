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

#include "fat32.h"

static u64 fat32fs_cluster_to_lba(file_system_t* fs, u32 cluster);
static list_entry_t* fat32fs_get_cluster_chain(u32 fcluster, file_system_t* fs, u32* size);
static error_t fat32fs_read_fat(file_system_t* fs);
static error_t fat32fs_write_fat(file_system_t* fs);
static fsnode_t* fat32_dirent_normalize_cache(fat32_dir_entry_t* dirent, file_system_t* fs);

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
	while(block_read_flexible(offset, 0, (u8*) bpb, sizeof(bpb_t), drive) != ERROR_NONE)
	{
		attempts++;
		if(attempts > 2) {kfree(bpb); return 0;}
	}

	//check if it is a valid fat32 fs
	if(*bpb->system_id_string != 'F' || *(bpb->system_id_string+1) != 'A' || *(bpb->system_id_string+2) != 'T' || *(bpb->system_id_string+3) != '3' || *(bpb->system_id_string+4) != '2')
	{
		kfree(bpb);
		return 0;
	}

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

	fat32_dir_entry_t root_dirent;
	root_dirent.first_cluster_low = bpb->root_directory_cluster & 0xFFFF;
	root_dirent.first_cluster_high = (u16) bpb->root_directory_cluster >> 16;
	tr->root_dir = fat32_dirent_normalize_cache(&root_dirent, tr);

	//returing the fat32 fs struct, setup done
	return tr;
}

/*
* This function opens node corresponding to file 'name' in directory 'dir'
*/
fsnode_t* fat32_open(fsnode_t* dir, char* name)
{
	file_system_t* fs = dir->file_system;
	fat32fs_specific_t* spe = fs->specific;
	fat32_node_specific_t* node_spe  = dir->specific;
	u32 cluster = node_spe->cluster;

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
	
		if(lfn_name != 0)
		{
			//long file name
			//TODO : check the checksum kprintf("checksum is %d (0x%X)\n", lfn_checksum, lfn_checksum);
			if(!strcmpnc(lfn_name, name)) 
			{
				fsnode_t* tr = fat32_dirent_normalize_cache(&dirents[i], fs);
				kfree(dirents);
				return tr;
			}
		}
		else
		{
			char t_name[FAT_NAME_MAX] = {0};

			strncpy(t_name, (char*) dirents[i].name, 8);
			strtrim(t_name);
			if(dirents[i].extension[0] != ' ' && dirents[i].extension[0] != 0)
			{
				strncat(t_name, ".", 1);
				strncat(t_name, (char*) dirents[i].extension, 3);
			}

			if(!strcmpnc(t_name, name)) 
			{
				fsnode_t* tr = fat32_dirent_normalize_cache(&dirents[i], fs);
				kfree(dirents);
				return tr;
			}
		}
	}
	
	//freeing the first dirents buffer, unused cause we have processed the entries
	kfree(dirents);

	return 0;
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

		if(!cchain) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (->0) !\n", 0b00000110);
		else if(cchain == 1) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (reserved cluster)\n", 0b00000110);
		else if(cchain == 0x0FFFFFF7) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (bad cluster)\n", 0b00000110);

		cluster = cchain;
		(*size)++;
	} while((cchain != 0) && !(cchain >= 0x0FFFFFF8));

	//free the last entry, unused cause empty
	kfree(lbuf);

	return tr;
}

/*
* this function reads the fat from disk
*/
static error_t fat32fs_read_fat(file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 fat_size = spe->bpb->fat_size * spe->bpb->bytes_per_sector;
	u32 fat_sector = spe->bpb_offset + spe->bpb->reserved_sectors;
	//note : same as part->start_lba

	u8 attempts = 0;
	error_t berr = ERROR_NONE;
	while((berr = block_read_flexible(fat_sector, 0, (u8*) spe->fat_table, fat_size, fs->drive)) != ERROR_NONE)
	{
		attempts++;
		if(attempts > 2) return berr;
	}
	return ERROR_NONE;
}

/*
* this function writes the cached fat on disk
*/
static error_t fat32fs_write_fat(file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 fat_size = spe->bpb->fat_size * spe->bpb->bytes_per_sector;
	u32 fat_sector = spe->bpb_offset + spe->bpb->reserved_sectors;
	return block_write_flexible(fat_sector, 0, (u8*) spe->fat_table, fat_size, fs->drive);
}

static fsnode_t* fat32_dirent_normalize_cache(fat32_dir_entry_t* dirent, file_system_t* fs)
{
	u32 file_cluster = (((u32)dirent->first_cluster_high) << 16) | ((u32)dirent->first_cluster_low);
	file_cluster &= 0x0FFFFFFF;

	/* try to read node from the cache */
    list_entry_t* iptr = fs->inode_cache;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        fat32_node_specific_t* espe = element->specific;
        if(espe->cluster == file_cluster) return element;
        iptr = iptr->next;
        isize--;
    }

	/* parse inode from dirent */
	fsnode_t* std_node = kmalloc(sizeof(fsnode_t));

	std_node->hard_links = 1;

	std_node->length = dirent->file_size;

	std_node->attributes = 0;
	if(dirent->attributes & FAT_ATTR_DIRECTORY) std_node->attributes |= FILE_ATTR_DIR;
	if(dirent->attributes & FAT_ATTR_HIDDEN) std_node->attributes |= FILE_ATTR_HIDDEN;
		
	//gets and converts creation/access/modification time to std time
	//we know there can't be overflow here, and we are handling conversion mannually, so disable -Wconversion
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wconversion"
	u8 chour = dirent->creation_time >> 11;
	u8 cmins = (dirent->creation_time & 0x7FF) >> 5;
	u8 csecs = (dirent->creation_time & 0x1F)*2;
	u8 cday = dirent->creation_date & 0x1F;
	u8 cmonth = (dirent->creation_date & 0x1FF) >> 5;
	u8 cyear = (dirent->creation_date >> 9) + 80; //0 = 1980 ; adding 80
	std_node->creation_time = convert_to_std_time(csecs, cmins, chour, cday, cmonth, cyear);

	u8 laday = dirent->last_access_date & 0x1F;
	u8 lamonth = (dirent->last_access_date & 0x1FF) >> 5;
	u8 layear = (dirent->last_access_date >> 9) + 80; //same
	std_node->last_access_time = convert_to_std_time(0, 0, 0, laday, lamonth, layear);

	u8 lmhour = dirent->last_modification_time >> 11;
	u8 lmmins = (dirent->last_modification_time & 0x7FF) >> 5;
	u8 lmsecs = (dirent->last_modification_time & 0x1F)*2;
	u8 lmday = dirent->last_modification_date & 0x1F;
	u8 lmmonth = (dirent->last_modification_date & 0x1FF) >> 5;
	u8 lmyear = (dirent->last_modification_date >> 9) + 80; //same
	std_node->last_modification_time = convert_to_std_time(lmsecs, lmmins, lmhour, lmday, lmmonth, lmyear);
	#pragma GCC diagnostic pop

	fat32_node_specific_t* spe = kmalloc(sizeof(fat32_node_specific_t));
	spe->cluster = file_cluster;
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
