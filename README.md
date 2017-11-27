# VK
VK is a Kernel

C kernel project ; current architecture : x86

Current status : the kernel can load an ELF executable, and execute the code

The scheduler is working, but i'm pretty sure it still contains some bugs.

I'm currently working on iso9660 support, to be able to boot and load executables from a cd.
I also plan to add timing support, so that the ATA DMA driver doesn't hang if it doesn't receive IRQ.

After that, i will have to clean up the code, and add system calls.
Then i'll try to port a libc, like newlib, and binutils/gcc (that would be really great).
