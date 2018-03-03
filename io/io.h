#ifndef IO_HEAD
#define IO_HEAD

#include "system.h"
#include "filesystem/fs.h"
#include "memory/mem.h"
#include "termios.h"

// I/O Streams
typedef struct IOSTREAM
{
    u8* buffer;
    volatile u32 count;
    u32 buffer_size;
} io_stream_t;

io_stream_t* iostream_alloc();
void iostream_free(io_stream_t* iostream);
u8 iostream_getch(io_stream_t* iostream);
u8 iostream_write(u8* buffer, u32 count, io_stream_t* iostream);

// TTYs
typedef struct TTY
{
    u8* buffer;
    u32 count;
    u32 buffer_size;
    io_stream_t* keyboard_stream;
    fsnode_t* pointer;
    struct termios termio;
} tty_t;

extern tty_t* current_tty;
extern tty_t* tty1; extern tty_t* tty2; extern tty_t* tty3;

void ttys_init();
tty_t* tty_init(char* name);
error_t tty_write(u8* buffer, u32 count, tty_t* tty);
error_t tty_read(u8* buffer, u32 count, tty_t* tty);
void tty_switch(tty_t* tty);

#endif