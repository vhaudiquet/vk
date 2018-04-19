#ifndef IO_HEAD
#define IO_HEAD

#include "system.h"
#include "filesystem/fs.h"
#include "filesystem/devfs.h"
#include "memory/mem.h"
#include "termios.h"

// I/O Streams
typedef struct IOSTREAM
{
    u8* buffer;
    volatile u32 count;
    u32 buffer_size;
    u32 attributes;
    fsnode_t* file;
    list_entry_t* waiting_processes;
} io_stream_t;

#define IOSTREAM_ATTR_BLOCKING_READ 1
#define IOSTREAM_ATTR_BLOCKING_WRITE 2
#define IOSTREAM_ATTR_AUTOEXPAND 4
#define IOSTREAM_ATTR_ONE_BYTE 8

io_stream_t* iostream_alloc();
void iostream_free(io_stream_t* iostream);
u8 iostream_getch(io_stream_t* iostream);
error_t iostream_write(u8* buffer, u32 count, io_stream_t* iostream);
error_t iostream_read(u8* buffer, u32 count, io_stream_t* iostream);

// TTYs
typedef struct TTY
{
    u8* buffer;
    u32 count;
    u32 buffer_size;
    u8* canon_buffer;
    u32 canon_buffer_count;
    u32 canon_buffer_size;
    io_stream_t* keyboard_stream;
    fsnode_t* pointer;
    struct termios termio;
    struct pgroup* foreground_processes;
    struct psession* session;
} tty_t;

extern tty_t* current_tty;
extern tty_t* tty1; extern tty_t* tty2; extern tty_t* tty3;

void ttys_init();
tty_t* tty_init(char* name);
error_t tty_write(u8* buffer, u32 count, tty_t* tty);
error_t tty_read(u8* buffer, u32 count, tty_t* tty);
void tty_input(tty_t* tty, u8 c);
void tty_switch(tty_t* tty);
error_t tty_ioctl(tty_t* tty, u32 func, u32 arg);

#endif