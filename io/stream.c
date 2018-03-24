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

#include "io.h"
#include "tasking/task.h"

#define IOSTREAM_DEFAULT_BUFFER_SIZE 100

static void iostream_wait(io_stream_t* iostream);
static void iostream_wake(io_stream_t* iostream);

/*
* Create a new I/O Stream structure
*/
io_stream_t* iostream_alloc()
{
    io_stream_t* tr = kmalloc(sizeof(io_stream_t));
    tr->buffer_size = IOSTREAM_DEFAULT_BUFFER_SIZE;
    tr->buffer = kmalloc(tr->buffer_size);
    tr->count = 0;
    tr->attributes = IOSTREAM_ATTR_BLOCKING_READ | IOSTREAM_ATTR_AUTOEXPAND;
    tr->waiting_processes = 0;

    tr->file = kmalloc(sizeof(fsnode_t));
    tr->file->file_system = devfs;
    tr->file->length = 0;
    tr->file->attributes = 0;
    tr->file->hard_links = 0;
    tr->file->creation_time = 0;
    tr->file->last_access_time = 0;
    tr->file->last_modification_time = 0;
    devfs_node_specific_t* spe = kmalloc(sizeof(devfs_node_specific_t));
    spe->device_struct = tr;
    spe->device_type = DEVFS_TYPE_IOSTREAM;
    spe->device_info = 0;
    tr->file->specific = spe;
    
    return tr;
}

/*
* Free an I/O Stream structure
*/
void iostream_free(io_stream_t* iostream)
{
    kfree(iostream->buffer);
    kfree(iostream);
}

/*
* Reads a character from an I/O Stream
* CARE : This function can block (if there is no data yet on the stream and BLOCKING_READ is set)
*/
u8 iostream_getch(io_stream_t* iostream)
{
    if(iostream->count)
    {
        u8 tr = *iostream->buffer;
        iostream->count--;
        memcpy(iostream->buffer, iostream->buffer+1, iostream->count);
        return tr;
    }
    else
    {
        if(!iostream->attributes & IOSTREAM_ATTR_BLOCKING_READ) return 0;

        //wait for more data to be written
        iostream_wait(iostream);
        return iostream_getch(iostream);
    }
}

/*
* Writes to an I/O Stream
* CARE : This function can block (if the stream is full and BLOCKING_WRITE is set)
* For now, block is just a while() loop, so it really consumes CPU time !
*/
error_t iostream_write(u8* buffer, u32 count, io_stream_t* iostream)
{
    if(iostream->count+count > iostream->buffer_size)
    {
        if(iostream->attributes & IOSTREAM_ATTR_AUTOEXPAND)
        {
            iostream->buffer_size*=2;
            iostream->buffer = krealloc(iostream->buffer, iostream->buffer_size);
        }
        else if(iostream->attributes & IOSTREAM_ATTR_BLOCKING_WRITE)
        {
            while(iostream->count+count > iostream->buffer_size);
            return iostream_write(buffer, count, iostream);
        }
        else return ERROR_FILE_OUT;
    }

    memcpy(iostream->buffer+iostream->count, buffer, count);
    iostream->count += count;
    
    iostream_wake(iostream);
    
    return ERROR_NONE;
}

/*
* Reads from an I/O Stream
* CARE : This function can block (if there is no data on the stream and BLOCKING_READ is set)
*/
error_t iostream_read(u8* buffer, u32 count, io_stream_t* iostream)
{
    if((iostream->attributes | IOSTREAM_ATTR_ONE_BYTE) && (count > 1)) return ERROR_FILE_FS_INTERNAL;

    if(iostream->count)
    {   
        u32 countmin = count > iostream->count ? iostream->count : count;
        
        memcpy(buffer, iostream->buffer, countmin);
        if(iostream->count < count) memset(buffer+count, 0, count-iostream->count);

        iostream->count -= countmin;
        memcpy(iostream->buffer, iostream->buffer+countmin, iostream->count);

        return ERROR_NONE;
    }
    else
    {
        if(!iostream->attributes & IOSTREAM_ATTR_BLOCKING_READ) return ERROR_EOF;

        //wait for more data to be written
        iostream_wait(iostream);
        return iostream_read(buffer, count, iostream);
    }
}

static void iostream_wait(io_stream_t* iostream)
{
    //pointer to the list
    list_entry_t** ptr = &iostream->waiting_processes;
    //while there are elements in the list, we iterate through
    while(*ptr) ptr = &((*ptr)->next);
    //we allocate space and set entry data (our iostream and associated process)
    (*ptr) = kmalloc(sizeof(list_entry_t));
    (*ptr)->next = 0;
    (*ptr)->element = current_process;

    //we remove current process from scheduler
    current_process->status = PROCESS_STATUS_ASLEEP_IO;
    scheduler_remove_process(current_process);
}

static void iostream_wake(io_stream_t* iostream)
{
    list_entry_t* ptr = iostream->waiting_processes;
    while(ptr)
    {
        scheduler_add_process(ptr->element);
        list_entry_t* tfree = ptr;
        ptr = ptr->next;
        kfree(tfree);
    }

    iostream->waiting_processes = 0;
}