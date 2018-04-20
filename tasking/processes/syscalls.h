#ifndef SYSCALL_HEAD
#define SYSCALL_HEAD

#define SYSCALL_OPEN 1
#define SYSCALL_CLOSE 2
#define SYSCALL_READ 3
#define SYSCALL_WRITE 4
#define SYSCALL_LINK 5
#define SYSCALL_UNLINK 6
#define SYSCALL_SEEK 7
#define SYSCALL_STAT 8
#define SYSCALL_RENAME 9
#define SYSCALL_FINFO 10
#define SYSCALL_MOUNT 11
#define SYSCALL_UMOUNT 12
#define SYSCALL_MKDIR 13
#define SYSCALL_READDIR 14
#define SYSCALL_OPENIO 15
#define SYSCALL_DUP 16

#define SYSCALL_FORK 31
#define SYSCALL_EXIT 32
#define SYSCALL_EXEC 33
#define SYSCALL_WAIT 34
#define SYSCALL_GETPINFO 35
#define SYSCALL_SETPINFO 36
#define SYSCALL_SIG 37
#define SYSCALL_SIGACTION 38
#define SYSCALL_SIGRET 39
#define SYSCALL_SBRK 40

//SYSCALL_FINFO values
#define VK_FINFO_DEVICE_TYPE 1
#define VK_NOT_A_DEVICE 1

//SYSCALL_GETPINFO / SYSCALL_SETPINFO values
#define VK_PINFO_PID 1
#define VK_PINFO_PPID 2
#define VK_PINFO_GID 4
#define VK_PINFO_WORKING_DIRECTORY 3

//SYSCALL_FSINFO values
#define VK_FSINFO_MOUNTED_FS_NUMBER 1
#define VK_FSINFO_MOUNTED_FS_ALL 2

void syscall_open(u32 ebx, u32 ecx, u32 edx);
void syscall_close(u32 ebx, u32 ecx, u32 edx);
void syscall_read(u32 ebx, u32 ecx, u32 edx);
void syscall_write(u32 ebx, u32 ecx, u32 edx);
void syscall_link(u32 ebx, u32 ecx, u32 edx);
void syscall_unlink(u32 ebx, u32 ecx, u32 edx);
void syscall_seek(u32 ebx, u32 ecx, u32 edx);
void syscall_stat(u32 ebx, u32 ecx, u32 edx);
void syscall_rename(u32 ebx, u32 ecx, u32 edx);
void syscall_finfo(u32 ebx, u32 ecx, u32 edx);
void syscall_mount(u32 ebx, u32 ecx, u32 edx);
void syscall_umount(u32 ebx, u32 ecx, u32 edx);
void syscall_mkdir(u32 ebx, u32 ecx, u32 edx);
void syscall_readdir(u32 ebx, u32 ecx, u32 edx);
void syscall_openio(u32 ebx, u32 ecx, u32 edx);
void syscall_dup(u32 ebx, u32 ecx, u32 edx);
void syscall_fsinfo(u32 ebx, u32 ecx, u32 edx);

void syscall_fork(u32 ebx, u32 ecx, u32 edx);
void syscall_exit(u32 ebx, u32 ecx, u32 edx);
void syscall_exec(u32 ebx, u32 ecx, u32 edx);
void syscall_wait(u32 ebx, u32 ecx, u32 edx);
void syscall_getpinfo(u32 ebx, u32 ecx, u32 edx);
void syscall_setpinfo(u32 ebx, u32 ecx, u32 edx);
void syscall_sig(u32 ebx, u32 ecx, u32 edx);
void syscall_sigaction(u32 ebx, u32 ecx, u32 edx);
void syscall_sigret(u32 ebx, u32 ecx, u32 edx);
void syscall_sbrk(u32 ebx, u32 ecx, u32 edx);

void syscall_ioctl(u32 ebx, u32 ecx, u32 edx);

#endif