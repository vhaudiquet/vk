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
static void fat32fs_free_cluster_chain(u32 fcluster, file_system_t* fs);
static u32 fat32fs_gm_free_clusters(u32 nbr, file_system_t* fs);
static void fat32fs_get_old_name(u8* oldname, u8* oldext, u8* name, list_entry_t* dirnames, u32 dirsize);
static u8 fat32fs_lfn_checksum (u8* pFcbName);

static error_t fat32fs_read_fat(file_system_t* fs);
static error_t fat32fs_write_fat(file_system_t* fs);

static error_t fat32fs_create_dirent(fsnode_t* file, char* name, fsnode_t* dir);
static error_t fat32fs_update_dirent(fsnode_t* file);
static error_t fat32fs_delete_dirent(fsnode_t* file, fsnode_t* dir);
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
error_t fat32_list_dir(list_entry_t* tr, fsnode_t* dir, u32* size)
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
* This function renames a file
*/
error_t fat32_rename(fsnode_t* src_file, char* src_file_name, char* new_file_name, fsnode_t* dir)
{
	//delete old dirent
	error_t old_dirent = fat32fs_delete_dirent(src_file, dir);
	if(old_dirent != ERROR_NONE) return old_dirent;

	//create new dirent
	error_t new_dirent = fat32fs_create_dirent(dir, new_file_name, src_file);
	if(new_dirent != ERROR_NONE)
	{
		fat32fs_create_dirent(dir, src_file_name, src_file);
		return new_dirent;
	}

	return ERROR_NONE;
}

/*
* This function removes a file
*/
error_t fat32_unlink(char* file_name, fsnode_t* dir)
{
	fsnode_t* file = fat32_open(dir, file_name);
	file_system_t* fs = file->file_system;
	fat32_node_specific_t* fspe = file->specific;

	error_t dirent = fat32fs_delete_dirent(file, dir);
	if(dirent != ERROR_NONE) return dirent;

	fat32fs_free_cluster_chain(fspe->cluster, fs);

	/* free node from the cache */
    //TODO: here we assume that node we want to free is not the first in the cache (cause the first is theorically root dir)
    //this is a little risquy tho
	list_entry_t* iptr = fs->inode_cache;
    list_entry_t* last = 0;
    u32 isize = fs->inode_cache_size;
    while(iptr && isize)
    {
        fsnode_t* element = iptr->element;
        fat32_node_specific_t* spe = element->specific;
        if(spe->cluster == fspe->cluster) {last->next = iptr->next; kfree(element->specific); kfree(element); kfree(iptr);}
        last = iptr;
        iptr = iptr->next;
        isize--;
    }

	return ERROR_NONE;
}

/*
* This function creates a new file
*/
fsnode_t* fat32_create_file(fsnode_t* dir, char* name, u8 attributes)
{
	file_system_t* fs = dir->file_system;
	fsnode_t* file = kmalloc(sizeof(fsnode_t));

	/* cache the object */
    if(!fs->inode_cache)
    {
        fs->inode_cache = kmalloc(sizeof(list_entry_t));
        fs->inode_cache->element = file;
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
        ptr->element = file;
        last->next = ptr;
        fs->inode_cache_size++;
    }

	/* fill the object informations */
	file->file_system = fs;
	file->length = 0;
	file->attributes = attributes;
	file->hard_links = 0;
	fat32_node_specific_t* specific = kmalloc(sizeof(fat32_node_specific_t));
	specific->cluster = fat32fs_gm_free_clusters(1, fs);
	file->specific = specific;

	/* create dirent */
	fat32fs_create_dirent(file, name, dir);

	return file;
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
* this function frees all the cluster of a cluster chain
*/
static void fat32fs_free_cluster_chain(u32 fcluster, file_system_t* fs)
{
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;

	u32 cluster = fcluster;
	u32 cchain = 0;

	do
	{
		cchain = spe->fat_table[cluster] & 0x0FFFFFFF;

		spe->fat_table[cluster] = 0; 

		if(!cchain) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (->0) !\n", 0b00000110);
		else if(cchain == 1) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (reserved cluster)\n", 0b00000110);
		else if(cchain == 0x0FFFFFF7) kprintf("%v[WARNING] [SEVERE] FAT32 Filesystem might be corrupted (bad cluster)\n", 0b00000110);

		cluster = cchain;
	} while((cchain != 0) && !(cchain >= 0x0FFFFFF8));
	spe->fat_table[cluster] = 0;

	fat32fs_write_fat(fs);
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
* This function creates a dirent for a file
*/
static error_t fat32fs_create_dirent(fsnode_t* file, char* name, fsnode_t* dir)
{
	if(file->hard_links) return ERROR_FILE_FS_INTERNAL;

	file_system_t* fs = dir->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	fat32_node_specific_t* fspe = file->specific;

	//get the parent directory cluster
	fat32_node_specific_t* dirspe = dir->specific;
	u32 dir_cluster = dirspe->cluster;

	//get the cluster chain for the parent directory
	u64 cluss = 0;
	list_entry_t* cluslist = fat32fs_get_cluster_chain(dir_cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//here we have the full dir size, and we allocate an equivalent buffer
	u64 dir_size = cluss * spe->bpb->sectors_per_cluster * 512;
	fat32_dir_entry_t* dirents = 
	#ifdef MEMLEAK_DBG
	kmalloc((u32) dir_size, "fat32:create_dirent dir entries buffer");
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
	kmalloc(sizeof(list_entry_t), "fat32:create_dirents name storing (first)");
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
				kmalloc((u32) 13*lfn_nbr+1, "fat32:create_dirent lfn name store");
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
					return ERROR_FILE_NAME_ALREADY_EXISTS;
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
				kfree(dirents); list_free_eonly(names, namess); kfree(namesb); 
				list_free(cluslist, (u32) cluss); 
				return ERROR_FILE_NAME_ALREADY_EXISTS;
			}
			namesb->element = dirents[i].name;
			namess++;
			namesb->next = 
			#ifdef MEMLEAK_DBG
			kmalloc(sizeof(list_entry_t), "fat32:create_dirent names storing");
			#else
			kmalloc(sizeof(list_entry_t));
			#endif
			namesb = namesb->next;
		}
	}
	kfree(namesb);

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
		clusbuffer = cluslist;
		u32 clus_temp_i;
		for(clus_temp_i = 0; clus_temp_i < cluss-1; clus_temp_i++) 
		{
			clusbuffer = clusbuffer->next;
		}
		u32* last_cluster = (u32*) clusbuffer->element;
		u32 new_cluster = fat32fs_gm_free_clusters(1, fs);
		
		u32* nclv = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "Cluster number, fat32.c:create_dirent"); 
		#else
		kmalloc(sizeof(u32)); 
		#endif
		*nclv = new_cluster; 
		clusbuffer->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "new cluster list element, fat32.c:create_dirent"); 
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
	if(file->attributes & FILE_ATTR_HIDDEN) {dirents[i+needed_lfns].attributes |= FAT_ATTR_HIDDEN;}
	if(file->attributes & FILE_ATTR_DIR) {dirents[i+needed_lfns].attributes |= FAT_ATTR_DIRECTORY;}
	fat32fs_get_old_name(dirents[i+needed_lfns].name, dirents[i+needed_lfns].extension, (u8*) name, names, namess);
	dirents[i+needed_lfns].file_size = (u32) file->length;
	dirents[i+needed_lfns].first_cluster_low = (u16) (fspe->cluster << 16 >> 16);
	dirents[i+needed_lfns].first_cluster_high = (u16) (fspe->cluster >> 16);
	
	#pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wconversion"
	//set time
	u8 secs, mins, hours, days, months, years;
	file->creation_time = get_current_time_utc();
	file->last_modification_time = file->creation_time;
	file->last_access_time = file->creation_time;
	convert_to_readable_time(file->creation_time, &secs, &mins, &hours, &days, &months, &years);
	dirents[i+needed_lfns].creation_time = (hours << 11);
	dirents[i+needed_lfns].creation_time |= (mins << 5);
	dirents[i+needed_lfns].creation_time |= (secs/2);
	dirents[i+needed_lfns].creation_date = ((years-80) << 9);
	dirents[i+needed_lfns].creation_date |= (months << 5);
	dirents[i+needed_lfns].creation_date |= (days);
	convert_to_readable_time(file->last_modification_time, &secs, &mins, &hours, &days, &months, &years);
	dirents[i+needed_lfns].last_modification_time = (hours << 11);
	dirents[i+needed_lfns].last_modification_time |= (mins << 5);
	dirents[i+needed_lfns].last_modification_time |= (secs/2);
	dirents[i+needed_lfns].last_modification_date = ((years-80) << 9);
	dirents[i+needed_lfns].last_modification_date |= (months << 5);
	dirents[i+needed_lfns].last_modification_date |= (days);
	convert_to_readable_time(file->last_access_time, &secs, &mins, &hours, &days, &months, &years);
	dirents[i+needed_lfns].last_access_date = ((years-80) << 9);
	dirents[i+needed_lfns].last_access_date |= (months << 5);
	dirents[i+needed_lfns].last_access_date |= (days);
	#pragma GCC diagnostic pop

	//free the dir old names
	list_free_eonly(names, namess);

	//back to the lfn entry
	u32 j = 0;
	
	//getting checksum
	u8 lfn_checksum = fat32fs_lfn_checksum(dirents[i+needed_lfns].name);

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
		while(fff < 5) {currlfn->firstn[fff] = *((u8*) name+fff+curr_name_offset); fff++;}
		fff = 0;
		while(fff < 6) {currlfn->nextn[fff] = *((u8*) name+fff+curr_name_offset+5); fff++;}
		fff = 0;
		while(fff < 2) {currlfn->lastn[fff] = *((u8*) name+fff+curr_name_offset+11); fff++;}

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

	file->hard_links++;

	return ERROR_NONE;
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

/*
* This function removes a dirent
*/
static error_t fat32fs_delete_dirent(fsnode_t* file, fsnode_t* dir)
{
	file_system_t* fs = file->file_system;
	fat32fs_specific_t* spe = (fat32fs_specific_t*) fs->specific;
	fat32_node_specific_t* dirspe = dir->specific;
	fat32_node_specific_t* fspe = file->specific;

	//get the parent directory cluster
	u32 dir_cluster = dirspe->cluster;

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

		u32 file_cluster = (((u32)dirents[i].first_cluster_high) << 16) | ((u32)dirents[i].first_cluster_low);
		file_cluster &= 0x0FFFFFFF;

		if(lfn_name)
		{
			if(file_cluster == fspe->cluster)
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
