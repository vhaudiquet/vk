#include "system.h"
#include "filesystem/fs.h"
#include "memory/mem.h"

//QUEUES
queue_t* queue_init(u32 size)
{
    queue_t* tr = kmalloc(sizeof(queue_t));
    tr->front = kmalloc(size*sizeof(void*));
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

//FILESYSTEM UTILS

void fd_free(file_descriptor_t* fd)
{
    kfree(fd->name);
    kfree(fd);
}

void fd_list_free(list_entry_t* list, u32 list_size)
{
    u32 i = 0;
    for(i = 0;i < list_size;i++)
    {
        file_descriptor_t* fd = (file_descriptor_t*) list->element;
        if(fd)
            fd_free(fd);
        void* buf = list;
        list = list->next;
        kfree(buf);
    }
}

void fd_copy(file_descriptor_t* dest, file_descriptor_t* src)
{
    strcpy(dest->name, src->name);
    dest->file_system = src->file_system;
    dest->fs_type = src->fs_type;
    dest->fsdisk_loc = src->fsdisk_loc;
    dest->attributes = src->attributes;
    dest->length = src->length;
    dest->offset = src->offset;
    dest->parent_directory = src->parent_directory;
}