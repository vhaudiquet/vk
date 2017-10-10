#ifndef MEM_HEAD
#define MEM_HEAD

//uncomment to enable kmalloc comments, and debug output
//#define MEMLEAK_DBG

#include "multiboot.h"

//KHEAP
//The struct must be MULTIPLE-OF-4 sized, so that all the addresses on the heap are 4bytes-aligned
typedef struct
{
    u32 size;
    u16 magic;
    u16 status;
    #ifdef MEMLEAK_DBG
    char* comment;
    #endif
} __attribute__ ((packed)) block_header_t;
#define BLOCK_HEADER_MAGIC 0xB1
#define KHEAP_BASE_SIZE 0x400000 // 4MiB
extern u32 KHEAP_BASE_START;
extern u32 KHEAP_BASE_END;
extern u32 KHEAP_PHYS_START;
void kheap_install();
#ifdef MEMLEAK_DBG
void* kmalloc(u32 size, char* comment);
#else
void* kmalloc(u32 size);
#endif
void kfree(void* pointer);
void* krealloc(void* pointer, u32 newsize);

//KPHEAP (page heap)
extern u8 kpheap_blocks[1024];
void install_page_heap();
u32* pt_alloc();
void pt_free(u32* pt);

//Physical memory
#define PHYS_FREE_BLOCK_TYPE 1
#define PHYS_HARD_BLOCK_TYPE 2
#define PHYS_KERNEL_BLOCK_TYPE 10
#define PHYS_KERNELF_BLOCK_TYPE 11
#define PHYS_USER_BLOCK_TYPE 20
typedef struct p_block
{
    u32 base_addr;
    u32 size;
    struct p_block* next;
    struct p_block* prev;
    u32 type;
} p_block_t;
void physmem_get(multiboot_info_t* mbt);
void print_mem_map(); //DEBUG
u32 get_free_mem();
extern u64 detected_memory;
extern u32 detected_memory_below32;
u32 reserve_block(u32 size, u8 type);
u32 reserve_specific(u32 addr, u32 size, u8 type);
void free_block(u32 base_addr);
p_block_t* get_block(u32 some_addr);

//Paging
extern u32 kernel_page_directory[1024];
extern u32 kernel_page_table[1024];
void finish_paging();
void pd_switch(u32* pd);
u32* get_kernel_pd_clone();
void map_memory(u32 size, u32 virt_addr, u32* page_directory);
bool is_mapped(u32 virt_addr, u32* page_directory);
u32 get_physical(u32 virt_addr, u32* page_directory);
//void map_physical(p_block_t* tm, u32 virt_addr);

#endif