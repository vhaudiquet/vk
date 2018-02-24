# VK
VK is a Kernel

C kernel project ; current architecture : x86

Current status : the kernel can load an ELF executable, statically linked with newlib, and execute the code, 
from an ATA disk with a FAT32/EXT2 fs or an ATAPI cd-rom with an ISO9660 fs.
(even if for now almost every executable cause a page fault from iso9660, idk why...)

The scheduler is working, but i'm pretty sure it still contains some bugs.
I want to get ACPI support, at least for ACPI shutdown but also to use things like HPET...

I already ported Newlib and made my custom (i386-vk-*) toolchain ; i successfully compilated and executed simple
programs like hello world, and tested scanf()/getc().. and everything worked.
I'm now trying to port real software but for that i'll have to add the 'termios' and 'dirent' header to newlib,
and clean up everything in the kernel syscalls / code + in the toolchain.
