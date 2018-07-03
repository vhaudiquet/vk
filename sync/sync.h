#ifndef SYNC_HEAD
#define SYNC_HEAD
#include "system.h"

typedef struct mutex
{
    struct PROCESS* locked_by;
    list_entry_t* waiting;
} mutex_t;

error_t mutex_lock(mutex_t* mutex);
error_t mutex_unlock(mutex_t* mutex);
void mutex_wait(mutex_t* mutex);

#endif