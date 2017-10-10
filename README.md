# kernel
C kernel project ; current architecture : x86
Current status : the kernel can load an ELF executable, and execute the code
There are some problems with the scheduler that i'm trying to fix, but soon enough multitasking will be supported
Then i will have to make an ATA DMA and ATAPI driver, because currently i have only ATA PIO (and read only, the write functions are broken)
With the ATAPI and ATA DMA, i'll be able to support both LIVE and INSTALLED context.
After that, i plan to port a libc (newlib ?) or write one (not really, i'm bad), and then porting gcc.
