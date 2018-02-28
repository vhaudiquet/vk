#ifndef SYNC_HEAD
#define SYNC_HEAD

typedef u32* mutex_t;
error_t mutex_lock(mutex_t mutex);
error_t mutex_unlock(mutex_t mutex);
#define mutex_alloc() kmalloc(sizeof(u32))
#define mutex_free(mutex) kfree(mutex)

#endif