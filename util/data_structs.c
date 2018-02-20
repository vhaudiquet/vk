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

#include "system.h"
#include "memory/mem.h"

/* Some basic data structures
* (note : the linked list implementation was written during the FAT32fs dev, and is good for performance, but bad on other parts
*  of the kernel (if there is no loop in the piece of code, we have to make one to fill the list))
*/

//QUEUES
queue_t* queue_init(u32 size)
{
    queue_t* tr = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(queue_t), "queue_t");
    #else
    kmalloc(sizeof(queue_t));
    #endif
    tr->front = 
    #ifdef MEMLEAK_DBG
    kmalloc(size*sizeof(void*), "queue data");
    #else
    kmalloc(size*sizeof(void*));
    #endif
    tr->rear = tr->front - sizeof(void*);
    tr->size = size;
    return tr;
}

void queue_add(queue_t* queue, void* element)
{
    if(queue->rear == (queue->front+queue->size*sizeof(void*))) //if(queue_is_full)
    {queue->front = krealloc(queue->front, queue->size*sizeof(void*)*2); queue->size*=2;}

    queue->rear += sizeof(void*);
    *(queue->rear) = element;
}

void* queue_take(queue_t* queue)
{ 
    if(queue->rear < queue->front) return 0; //if(queue_is_empty)
    
    void* tr = *(queue->front);

    memcpy(queue->front, queue->front+sizeof(void*), sizeof(void*)*(queue->size-1));
    queue->rear -= sizeof(void*);

    return tr;
}

void queue_remove(queue_t* queue, void* element)
{
    if(queue->rear < queue->front) return; //if(queue_is_empty)

    u32 i = 0;

    for(i = 0; i < queue->size ; i++)
    {
        if(*(queue->front+sizeof(void*)*i) == element) break;
    }

    memcpy(queue->front, queue->front+sizeof(void*)*i, sizeof(void*)*(queue->size-1-i));
    queue->rear -= sizeof(void*);
}

//LISTS
void list_free(list_entry_t* list, u32 list_size)
{
    u32 i = 0;
    for(i = 0;i < list_size;i++)
    {
        kfree(list->element);
        void* buf = list;
        list = list->next;
        kfree(buf);
    }
}

void list_free_eonly(list_entry_t* list, u32 list_size)
{
    u32 i = 0;
    for(i = 0;i < list_size;i++)
    {
        void* buf = list;
        list = list->next;
        kfree(buf);
    }
}
