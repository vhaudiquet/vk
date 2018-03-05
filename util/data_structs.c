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

#define QUEUE_STACK_DEFAULT_SIZE 10

//QUEUES
queue_t* queue_init()
{
    queue_t* tr = 
    #ifdef MEMLEAK_DBG
    kmalloc(sizeof(queue_t), "queue_t");
    #else
    kmalloc(sizeof(queue_t));
    #endif
    tr->front = 
    #ifdef MEMLEAK_DBG
    kmalloc(QUEUE_STACK_DEFAULT_SIZE*sizeof(void*), "queue data");
    #else
    kmalloc(QUEUE_STACK_DEFAULT_SIZE*sizeof(void*));
    #endif
    tr->rear = tr->front - sizeof(void*);
    tr->size = QUEUE_STACK_DEFAULT_SIZE;
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

//STACKS
stack_t* stack_init()
{
    stack_t* tr = kmalloc(sizeof(stack_t));
    tr->buffer_size = QUEUE_STACK_DEFAULT_SIZE;
    tr->buffer = kmalloc(sizeof(void*)*QUEUE_STACK_DEFAULT_SIZE);
    tr->count = 0;
    return tr;
}

void stack_add(stack_t* stack, void* element)
{
    if(stack->count*sizeof(void*) >= stack->buffer_size)
    {stack->buffer_size*=2; stack->buffer = krealloc(stack->buffer, stack->buffer_size*sizeof(void*));}

    *((void**) stack->buffer_size+stack->count*sizeof(void*)) = element;
    stack->count++;
}

void* stack_take(stack_t* stack)
{
    if(!stack->count) return 0;

    void* tr = *((void**) stack->buffer_size+(stack->count-1)*sizeof(void*));
    stack->count--;

    return tr;
}

void* stack_look(stack_t* stack, u32 position)
{
    if(stack->count <= position) return 0;

    void* tr = *((void**) stack->buffer_size+position*sizeof(void*));
    return tr;
}

void stack_remove(stack_t* stack, void* element)
{
    if(!stack->count) return;

    u32 i = 0;

    for(i = 0; i < stack->count ; i++)
    {
        if(*(stack->buffer+sizeof(void*)*i) == element) break;
    }

    memcpy(stack->buffer, stack->buffer+sizeof(void*)*i, sizeof(void*)*(stack->count-1-i));
    stack->count--;
}

//LISTS
void list_free(list_entry_t* list, u32 list_size)
{
    if(!list_size) if(list) kfree(list);
    u32 i = 0;
    for(i = 0;i < list_size;i++)
    {
        if(list->element) kfree(list->element);
        void* buf = list;
        list = list->next;
        kfree(buf);
        if(!list) break;
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
        if(!list) break;
    }
}
