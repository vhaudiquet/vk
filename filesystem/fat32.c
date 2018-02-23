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
static u32 fat32fs_gm_free_clusters(u32 nbr, file_system_t* fs);

static error_t fat32fs_read_fat(file_system_t* fs);
static error_t fat32fs_write_fat(file_system_t* fs);

static error_t fat32fs_update_dirent(fsnode_t* file);
static fsnode_t* fat32_dirent_normalize_cache(fat32_dir_entry_t* dirent, u32 dir_cluster, file_system_t* fs);

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

	tr->inode_cache = 0;
	tr->inode_cache_size = 0;

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
	tr->root_dir = fat32_dirent_normalize_cache(&root_dirent, 0, tr);

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
			// kprintf("looking %s for %s...\n", lfn_name, name);
			if(!strcmpnc(lfn_name, name)) 
			{
				fsnode_t* tr = fat32_dirent_normalize_cache(&dirents[i], cluster, fs);
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
				fsnode_t* tr = fat32_dirent_normalize_cache(&dirents[i], cluster, fs);
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
* Read all the files and folders inside a directory into a linked list
*/
error_t fat32fs_list_dir(list_entry_t* tr, fsnode_t* dir, u32* size)
{
	if((dir->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) return ERROR_FILE_IS_NOT_DIRECTORY;

	fat32_node_specific_t* fspe = dir->specific;
	file_system_t* fs = dir->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	u32 cluster = fspe->cluster;

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

	//get the first list_entry of the list that we'll return
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

		//TODO: check if file_cluster == cache->files->file_cluster (if the file is already cached)

		dirent_t* tf;
	
		if(lfn_name != 0)
		{
			//long file name
			//TODO : check the checksum kprintf("checksum is %d (0x%X)\n", lfn_checksum, lfn_checksum);
			u32 lfn_len = strlen(lfn_name);
			tf = kmalloc(sizeof(dirent_t) + lfn_len);
			tf->name_len = lfn_len;
			strcpy(tf->name, lfn_name);
			lfn_name = 0;
			lfn_offset = 0;
		}
		else
		{
			tf = kmalloc(sizeof(dirent_t) + FAT_NAME_MAX);
			//legacy FAT32 no lfn support
			tf->name_len = FAT_NAME_MAX;
			strncpy(tf->name, (char*) dirents[i].name, 8);
			strtrim(tf->name);
			if(dirents[i].extension[0] != ' ' && dirents[i].extension[0] != 0)
			{
				strncat(tf->name, ".", 1);
				strncat(tf->name, (char*) dirents[i].extension, 3);
			}
		}

		//kprintf("%lfname : %s\n", 3, tf->name);

		u32 file_cluster = (((u32)dirents[i].first_cluster_high) << 16) | ((u32)dirents[i].first_cluster_low);
		file_cluster &= 0x0FFFFFFF;
		tf->inode = file_cluster;

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
	return ERROR_NONE;
}

/*
* This function reads count bytes from file fd to buffer
*/
error_t fat32_read_file(fd_t* fd, void* buffer, u64 count)
{
	fsnode_t* file = fd->file;
	fat32_node_specific_t* fspe = file->specific;
	u64 offset = fd->offset;
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	if(count+offset < spe->bpb->sectors_per_cluster*512)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, fspe->cluster);
		return block_read_flexible(pos, (u32) offset, (u8*) buffer, count, fs->drive);
	}
	
	//alright, we want to read more than 1 cluster, so we'll need to get the whole cluster chain
	u32 cluster = fspe->cluster;
	u64 cluss = 0;

	list_entry_t* cluslist = fat32fs_get_cluster_chain(cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	while(offset >= spe->bpb->sectors_per_cluster*512) {clusbuffer=clusbuffer->next; offset = (u64) (offset - ((u64) (spe->bpb->sectors_per_cluster*512)));}

	//reading all needed clusters inside this buffer
	error_t readop = ERROR_NONE;
	u64 cluss2 = cluss;
	while(count > 0 && cluss2 > 0)
	{
		u64 pos = fat32fs_cluster_to_lba(fs, *((u32*) clusbuffer->element));
		if(count >= spe->bpb->sectors_per_cluster*512)
		{
			readop = block_read_flexible(pos, (u32) offset, (u8*) buffer, (u64) spe->bpb->sectors_per_cluster*512, fs->drive);
			if(readop != ERROR_NONE){list_free(cluslist, (u32) cluss); return readop;}
			count = ((u32)(count - ((u64)spe->bpb->sectors_per_cluster*512)));
			if(offset) offset = 0;
		}
		else
		{
			readop = block_read_flexible(pos, (u32) offset, (u8*) buffer, count, fs->drive);
			if(readop != ERROR_NONE){list_free(cluslist, (u32) cluss); return readop;}
			if(offset) offset = 0;
			break;
		}
		clusbuffer = clusbuffer->next;
		buffer += (spe->bpb->sectors_per_cluster*512);
		cluss2--;
	}

	//freeing the cluster list, we dont need it, we read them already
	list_free(cluslist, (u32) cluss);

	//returning error_none, everything went well
	return ERROR_NONE;	
}

/*
* This function writes count bytes from buffer to file
*/
error_t fat32_write_file(fd_t* fd, void* buffer, u64 count)
{
	fsnode_t* file = fd->file;
	fat32_node_specific_t* fspe = file->specific;
	u64 count_bckp = count;
	u32 cluster = fspe->cluster;
	u64 offset = fd->offset;
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

	if(count_bckp+fd->offset > file->length)
	{
		file->length = count_bckp+fd->offset;
		//here we assume the function can't fail, because we wrote to the file so it obviously exists
		fat32fs_update_dirent(file);
	}

	return ERROR_NONE;
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

/*
* This function updates a dirent to match fsnode file
*/
static error_t fat32fs_update_dirent(fsnode_t* file)
{
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	fat32_node_specific_t* fspe = file->specific;

	//get the parent directory cluster
	u32 dir_cluster = fspe->dir_cluster;

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

		//calcultate cluster
		u32 file_cluster = (((u32)dirents[i].first_cluster_high) << 16) | ((u32)dirents[i].first_cluster_low);
		file_cluster &= 0x0FFFFFFF;

		if(lfn_name)
		{
			if(file_cluster == fspe->cluster)
			{
				//we have found the right 8.3 entry
				dirents[i].file_size = (u32) file->length;

				if(file->attributes & FILE_ATTR_DIR) dirents[i].attributes |= FAT_ATTR_DIRECTORY;
				else dirents[i].attributes &= (u8)~FAT_ATTR_DIRECTORY;
				if(file->attributes & FILE_ATTR_HIDDEN) dirents[i].attributes |= FAT_ATTR_HIDDEN;
				else dirents[i].attributes &= (u8)~FAT_ATTR_HIDDEN;

				#pragma GCC diagnostic push
    			#pragma GCC diagnostic ignored "-Wconversion"
				//set time
				file->last_modification_time = file->last_access_time = get_current_time_utc();
				u8 secs, mins, hours, days, months, years;
				convert_to_readable_time(file->creation_time, &secs, &mins, &hours, &days, &months, &years);
				dirents[i].creation_time = (hours << 11);
				dirents[i].creation_time |= (mins << 5);
				dirents[i].creation_time |= (secs/2);
				dirents[i].creation_date = ((years-80) << 9);
				dirents[i].creation_date |= (months << 5);
				dirents[i].creation_date |= (days);
				convert_to_readable_time(file->last_modification_time, &secs, &mins, &hours, &days, &months, &years);
				dirents[i].last_modification_time = (hours << 11);
				dirents[i].last_modification_time |= (mins << 5);
				dirents[i].last_modification_time |= (secs/2);
				dirents[i].last_modification_date = ((years-80) << 9);
				dirents[i].last_modification_date |= (months << 5);
				dirents[i].last_modification_date |= (days);
				convert_to_readable_time(file->last_access_time, &secs, &mins, &hours, &days, &months, &years);
				dirents[i].last_access_date = ((years-80) << 9);
				dirents[i].last_access_date |= (months << 5);
				dirents[i].last_access_date |= (days);
				#pragma GCC diagnostic pop

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
		return ERROR_FILE_NOT_FOUND;
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

	return ERROR_NONE;
}

static fsnode_t* fat32_dirent_normalize_cache(fat32_dir_entry_t* dirent, u32 dir_cluster, file_system_t* fs)
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

	std_node->file_system = fs;

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
	spe->dir_cluster = dir_cluster;
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
