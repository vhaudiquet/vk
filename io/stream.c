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

#define IOSTREAM_DEFAULT_BUFFER_SIZE 100

/*
* Create a new I/O Stream structure
*/
io_stream_t* iostream_alloc()
{
    io_stream_t* tr = kmalloc(sizeof(io_stream_t));
    tr->buffer_size = IOSTREAM_DEFAULT_BUFFER_SIZE;
    tr->buffer = kmalloc(tr->buffer_size);
    tr->count = 0;
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
* CARE : This function can block (if there is no data yet on the stream)
* For now, block is just a while() loop, so it really consumes CPU time !
*/
u8 iostream_getch(io_stream_t* iostream)
{
    if(iostream->count)
    {
        kprintf(""); //i dont know why yet, we need a little delay here if we come from the end loop, so this will do it
        u8 tr = *iostream->buffer;
        iostream->count--;
        memcpy(iostream->buffer, iostream->buffer+1, iostream->count);
        return tr;
    }
    else
    {
        //wait for more data to be written
        while(!iostream->count);
        return iostream_getch(iostream);
    }
}

/*
* Writes to an I/O Stream
*/
u8 iostream_write(u8* buffer, u32 count, io_stream_t* iostream)
{
    if(iostream->count+count > iostream->buffer_size)
    {
        iostream->buffer_size*=2;
        iostream->buffer = krealloc(iostream->buffer, iostream->buffer_size);
    }

    memcpy(iostream->buffer+iostream->count, buffer, count);
    iostream->count += count;
    return 0;
}
