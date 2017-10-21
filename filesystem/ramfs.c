/*  
    This file is part of VK.

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
#include "error/error.h"

#define RAMFS_EOC 0x1

/*
* This is my first RAMFS implementation ; i did not test it, or debugged it, and it may be working really bad
* The only fs i have implemented before was FAT32 so i took the FAT32 vision to make this one
* This is basically a FAT32 inside RAM, but as we are into ram no need for LFN entries or some other crap like that from FAT32
*/

typedef struct ramfs_fd
{
    char* name;
    u32 addr;
    u8 attributes;
    u32 length;
} ramfs_fd_t;

static list_entry_t* ramfs_get_chain(u32 fsector, ramfs_t* fs, u32* size);
static u32 ramfs_gm_free_sectors(u32 nbr, ramfs_t* fs);
static void ramfs_resize_dirent(file_descriptor_t* file, u32 nsize);

ramfs_t* ramfs_init(u32 size)
{
    ramfs_t* tr =
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(ramfs_t), "RAMFS struct");
    #else
    kmalloc(sizeof(ramfs_t));
    #endif

    void* ramfs_addr =
    #ifdef MEMLEAK_DBG
    kmalloc(size, "RAMFS");
    #else
    kmalloc(size);
    #endif

    memset(ramfs_addr, 0, size);

    tr->base_addr = (u32) ramfs_addr;

    u32 sectorss = size/512;
    u32 addr_used_sectors = sectorss/128+1;
    sectorss -= addr_used_sectors;
    tr->list_size = addr_used_sectors*512;

    u32* first_sector_value = (u32*) ramfs_addr;
    *first_sector_value = RAMFS_EOC;

    tr->size = size;

    tr->root_directory.name = 0;
    tr->root_directory.file_system = tr;
    tr->root_directory.parent_directory = 0;
    tr->root_directory.fs_type = FS_TYPE_RAMFS;
    tr->root_directory.fsdisk_loc = (((u32) ramfs_addr) + tr->list_size);
    tr->root_directory.attributes = FILE_ATTR_DIR;
    tr->root_directory.length = 0;
    tr->root_directory.offset = 0;

    return tr;
}

list_entry_t* ramfs_read_dir(file_descriptor_t* dir, u32* size)
{
    if((dir->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) fatal_kernel_error("Trying to read a file as a directory", "RAMFS_READ_DIR"); //TEMP

    ramfs_t* fs = (ramfs_t*) dir->file_system;
    u32 sector = (u32) dir->fsdisk_loc;

    //get the sector chain for this directory
	u32 cluss = 0;
	list_entry_t* cluslist = ramfs_get_chain(sector, fs, &cluss);
    list_entry_t* clusbuffer = cluslist;
    
    //allocate the first list_entry of the list that we'll return
	list_entry_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(list_entry_t), "ramfs: first dir entry (list_dir) tr");
	#else
	kmalloc(sizeof(list_entry_t));
	#endif
	list_entry_t* lbuf = tr;

    //reading all sectors
    u32 entries = 0;
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
        u32 tcb = *((u32*) clusbuffer->element);
        for(u32 j = 0; j < 512;j+=sizeof(ramfs_fd_t))
        {
            ramfs_fd_t* crfd = (ramfs_fd_t*) tcb+j;

            file_descriptor_t* tf = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(file_descriptor_t), "ramfs:list_dir file_descriptor element");
            #else
            kmalloc(sizeof(file_descriptor_t));
            #endif

            u32 nlen = strlen(crfd->name);
            tf->name = 
            #ifdef MEMLEAK_DBG
            kmalloc(nlen+1, "ramfs:list_dir fd name");
            #else
            kmalloc(nlen+1);
            #endif
            strncpy(tf->name, crfd->name, nlen);
            tf->file_system = fs;
            tf->fs_type = FS_TYPE_RAMFS;
            tf->fsdisk_loc = crfd->addr;
            tf->length = crfd->length;
            tf->attributes = crfd->attributes;
            tf->offset = 0;
            tf->parent_directory = dir;

            lbuf->element = tf;
            
            lbuf->next = 
            #ifdef MEMLEAK_DBG
            kmalloc(sizeof(list_entry_t), "ramfs:list_dir list entry");
            #else
            kmalloc(sizeof(list_entry_t));
            #endif
            lbuf = lbuf->next;
            entries++;
        }
        clusbuffer = clusbuffer->next;
    }
    
    //freeing the last list entry, that is empty and unused
	kfree(lbuf);

    //freeing sector list
    list_free(cluslist, cluss);

    //returning
    *size = entries;
    return tr;
}

file_descriptor_t* ramfs_open_file(char* path, ramfs_t* fs)
{
	//The path should be relative to the mount point (excluded) of this ramfs
	// Step 1 : Split the path on '/'
	u32 i = 0;
	u32 split_size = 0;
	char** spath = strsplit(path, '/', &split_size);
	// Step 2 : iterate from the root directory and continue on as we found dirs/files on the list (splitted)
	u32 dirsize = 0;
	list_entry_t* cdir = ramfs_read_dir(&fs->root_directory, &dirsize);
	list_entry_t* lbuf = cdir;
	u32 j = 0;
	file_descriptor_t* ce = 0;
	while(i < split_size)
	{
		//looking for the i element in the directory
		//kprintf("i");
		j = 0;
		while(j < dirsize)
		{
			ce = lbuf->element;
			//looking at each element in the directory
			if(!ce) break;
			if(!strcmpnc(ce->name, spath[i]))
			{
				//if it is the last entry we need to find, return ; else continue exploring path
				if((i+1) == split_size)
				{
					file_descriptor_t* tr = 
					#ifdef MEMLEAK_DBG
					kmalloc(sizeof(file_descriptor_t), "ramfs:open_file file descriptor struct tr");
					#else
					kmalloc(sizeof(file_descriptor_t));
					#endif
					tr->name = 
					#ifdef MEMLEAK_DBG
					kmalloc(strlen(ce->name)+1, "ramfs:open_file tr file name");
					#else
					kmalloc(strlen(ce->name)+1);
					#endif
					fd_copy(tr, ce);
					fd_list_free(cdir, dirsize);
					kfree(spath[i]);
					kfree(spath);
					return tr;
				}

				//free the current dir list
				fd_list_free(cdir, dirsize);

				if((ce->attributes & FILE_ATTR_DIR) != FILE_ATTR_DIR) return 0;
				cdir = lbuf = ramfs_read_dir(ce, &dirsize);
				//we have found a subdirectory, explore it
				//TODO : if this subdirectory is not a dir but a file (handled in subfonction but better handle)
				break;
			}
			lbuf = lbuf->next;
			j++;
			//at this point, if we havent break yet, there is no such dir/file as we look for ; return 0
			if(j == dirsize) 
			{
				kfree(spath[i]);
				kfree(spath);
				fd_list_free(cdir, dirsize);
				return 0;
			}
		}
		kfree(spath[i]);
		i++;
	}
	kfree(spath[i-1]);
	kfree(spath);
	fd_list_free(cdir, dirsize);
	return 0;
}

u8 ramfs_read_file(file_descriptor_t* file, void* buffer, u64 count)
{
    u64 offset = file->offset;
    ramfs_t* fs = (ramfs_t*) file->file_system;

    u32 sector = (u32) file->fsdisk_loc;
	u32 cluss = 0;
	list_entry_t* cluslist = ramfs_get_chain(sector, fs, &cluss);
    list_entry_t* clusbuffer = cluslist;
    
    while(offset >= 512) {clusbuffer=clusbuffer->next; offset = (u64) (offset - 512);}
    
    //reading all needed clusters inside this buffer
    u32 cluss2 = cluss;
    while(count > 0 && cluss2 > 0)
    {
        u32 csect = *((u32*) clusbuffer->element);
        if(count >= 512)
        {
            memcpy(buffer, (void*) csect+offset, 512);
            count = ((u32)(count - 512));
            if(offset) offset = 0;
        }
        else
        {
            memcpy(buffer, (void*) csect+offset, (size_t) count);
            if(offset) offset = 0;
            break;
        }
        clusbuffer = clusbuffer->next;
        buffer += 512;
        cluss2--;
    }
    
    //freeing the cluster list, we dont need it, we read them already
    list_free(cluslist, cluss);
    
    //returning 0, everything went well
    return 0;
}

u8 ramfs_write_file(file_descriptor_t* file, u8* buffer, u64 count)
{
    u64 count_bckp = count;
	u32 cluster = (u32) file->fsdisk_loc;
	u64 offset = file->offset;
	ramfs_t* fs = (ramfs_t*) file->file_system;

	//get the cluster chain for this file
	u64 cluss = 0;
	list_entry_t* cluslist = ramfs_get_chain(cluster, fs, (u32*) &cluss);
	list_entry_t* clusbuffer = cluslist;

	//get the first cluster to write
	u32 fclus_tw = 0;
	while(offset > 512) {offset = (u64) (offset - 512); fclus_tw++;}

	//get the last cluster to write
	u32 lclus_tw = fclus_tw;
	while(count > 512) {count = (u64) (count - 512); lclus_tw++;}

	if(lclus_tw > cluss)
	{
		//expand file size
		u32 needed_clusters = lclus_tw - fclus_tw;
		if(!needed_clusters) needed_clusters++;

		u32 fnc = ramfs_gm_free_sectors(needed_clusters, fs);
		
		u32 clus_temp_i;
		for(clus_temp_i = 0; clus_temp_i < cluss-1; clus_temp_i++) 
		{
			clusbuffer = clusbuffer->next;
		}
		u32* last_cluster = (u32*) clusbuffer->element;

		u32* nclv = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "Cluster number, ramfs.c:write_file"); 
		#else
		kmalloc(sizeof(u32)); 
		#endif
		*nclv = fnc; 

		clusbuffer->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "new cluster list element, ramfs.c:write_file"); 
		#else 
		kmalloc(sizeof(list_entry_t)); 
		#endif
		clusbuffer->next->element = nclv;
		
		cluss++;

		*((u32*)fs->base_addr+((*last_cluster)*4)) = fnc;
	}

	u32 i = fclus_tw;

	clusbuffer = cluslist;
	while(i) {clusbuffer = clusbuffer->next; i--;}

	for(i = fclus_tw; i <= lclus_tw; i++)
	{
        u32 cstw = *((u32*)clusbuffer->element);
        if(i == fclus_tw)
            memcpy((void*)cstw+offset, buffer, (size_t) (i == lclus_tw ? count : 512));
        else
            memcpy((void*)cstw, buffer, (size_t) (i == lclus_tw ? count : 512));
		
		clusbuffer = clusbuffer->next;	
	}

	//freeing the cluster list
	list_free(cluslist, (u32) cluss);

	if(count_bckp+file->offset > file->length)
	{
		file->length = count_bckp+file->offset;
		ramfs_resize_dirent(file, ((u32)count_bckp)+((u32) file->offset));
	}

	return 0;
}

//TODO : EXPAND DIR IF THERE IS NO ROOM FOR A FILE ENTRY
//TODO : CHECK FOR NAME CONFLICTS
file_descriptor_t* ramfs_create_file(u8* name, u8 attributes, file_descriptor_t* dir)
{ 
    ramfs_t* fs = (ramfs_t*) dir->file_system;
    u32 sector = (u32) dir->fsdisk_loc;
        
    //get the sector chain for this directory
    u32 cluss = 0;
    list_entry_t* cluslist = ramfs_get_chain(sector, fs, &cluss);
    list_entry_t* clusbuffer = cluslist;
            
    //reading all sectors
    u32 i = 0;
    for(i = 0; i < cluss; i++)
    {
        u32 tcb = *((u32*) clusbuffer->element);
        for(u32 j = 0; j < 512;j+=sizeof(ramfs_fd_t))
        {
            ramfs_fd_t* crfd = (ramfs_fd_t*) tcb+j;
            if(!crfd->addr)
            {
                u32 nlen = strlen((char*) name);
                crfd->name =
                #ifdef MEMLEAK_DBG
                kmalloc(nlen+1, "RAMFS file NAME");
                #else
                kmalloc(nlen+1);
                #endif

                strncpy(crfd->name, (char*) name, nlen);

                crfd->addr = ramfs_gm_free_sectors(1, fs);
                crfd->attributes = attributes;
                crfd->length = 0;

                file_descriptor_t* tf = 
                #ifdef MEMLEAK_DBG
                kmalloc(sizeof(file_descriptor_t), "ramfs:new_file file_descriptor element");
                #else
                kmalloc(sizeof(file_descriptor_t));
                #endif
    
                tf->name = 
                #ifdef MEMLEAK_DBG
                kmalloc(nlen+1, "ramfs:new_file fd name");
                #else
                kmalloc(nlen+1);
                #endif
                strncpy(tf->name, crfd->name, nlen);
                tf->file_system = fs;
                tf->fs_type = FS_TYPE_RAMFS;
                tf->fsdisk_loc = crfd->addr;
                tf->length = crfd->length;
                tf->attributes = crfd->attributes;
                tf->offset = 0;
                tf->parent_directory = dir;
            
                list_free(cluslist, cluss);
                return tf;
            }
        }
        clusbuffer = clusbuffer->next;
    }

    list_free(cluslist, cluss);
    return 0;
}

static list_entry_t* ramfs_get_chain(u32 fsector, ramfs_t* fs, u32* size)
{
	u32 sector = fsector;
	u32 cchain = 0;
	list_entry_t* tr = 
	#ifdef MEMLEAK_DBG
	kmalloc(sizeof(list_entry_t), "ramfs: sector chain first entry");
	#else
	kmalloc(sizeof(list_entry_t));
	#endif
	list_entry_t* lbuf = tr;
	*size = 0;

	do
	{
		cchain = *((u32*) fs->base_addr+(sector*4));

		u32* cl = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(u32), "ramfs: sector chain entry element (u32)");
		#else
		kmalloc(sizeof(u32));
		#endif
		*cl = sector;
		lbuf->element = cl;

		lbuf->next = 
		#ifdef MEMLEAK_DBG
		kmalloc(sizeof(list_entry_t), "ramfs: sector chain list entry");
		#else
		kmalloc(sizeof(list_entry_t));
		#endif
		lbuf = lbuf->next;

		sector = cchain;
		(*size)++;
	} while((cchain != 0) && !(cchain >= RAMFS_EOC));

	//free the last entry, unused cause empty
	kfree(lbuf);

	return tr;
}

//this is getting the clusters in reverse order : possibly fragmenting memory ?
static u32 ramfs_gm_free_sectors(u32 nbr, ramfs_t* fs)
{
	//this function gets and marks (as a right clusterchain) nbr clusters (that were previously free)
	u32 active_cluster = 0;
	u32 last_free_cluster = 0;

	while(nbr)
	{
		u32 table_value = *((u32*)fs->base_addr+(active_cluster*4));
		while(table_value != 0) 
		{
			active_cluster++;
			table_value = *((u32*)fs->base_addr+(active_cluster*4));
		}
		//we have found a free cluster
		if(last_free_cluster == 0) *((u32*)fs->base_addr+(active_cluster*4)) = RAMFS_EOC;
		else *((u32*)fs->base_addr+(active_cluster*4)) = last_free_cluster;
		last_free_cluster = active_cluster;
		active_cluster++;
		nbr--;
	}
	return last_free_cluster;
}

static void ramfs_resize_dirent(file_descriptor_t* file, u32 nsize)
{
    file_descriptor_t* dir = file->parent_directory;

    ramfs_t* fs = (ramfs_t*) dir->file_system;
    u32 sector = (u32) dir->fsdisk_loc;
    
    //get the sector chain for this directory
    u32 cluss = 0;
    list_entry_t* cluslist = ramfs_get_chain(sector, fs, &cluss);
    list_entry_t* clusbuffer = cluslist;
        
    //reading all sectors
	u32 i = 0;
	for(i = 0; i < cluss; i++)
	{
        u32 tcb = *((u32*) clusbuffer->element);
        for(u32 j = 0; j < 512;j+=sizeof(ramfs_fd_t))
        {
            ramfs_fd_t* crfd = (ramfs_fd_t*) tcb+j;
            if(!strcmp(crfd->name, file->name))
            {
                crfd->length = nsize;
                goto free;
            }
        }
        clusbuffer = clusbuffer->next;
    }

    free:
    //freeing sector list
    list_free(cluslist, cluss);
    
    return;
}