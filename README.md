# VK
VK is a Kernel

C kernel project ; current architecture : x86

Current status : the kernel can load an ELF executable, and execute the code

The scheduler is working, but i'm pretty sure it still contains some bugs.

I'm currently working on an ATA DMA driver, because PIO is really slow and DMA is needed for ATAPI. When ATAPI will be supported, the kernel will be able to boot up from a CD-ROM, which means "LIVE" context (currently i can only run it when it is installed on hard drive)

After that, i will have to clean up the code, and add system calls.
Then i'll try to port a libc, like newlib, and binutils/gcc (that would be really great).
