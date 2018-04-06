/*  
    This file is part of VK.
    Copyright (C) 2017 Valentin Haudiquet

    VK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    VK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with VK.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "system.h"
#include "tasking/task.h"
#include "video/video.h"
#include "memory/mem.h"
#include "syscalls.h"

static bool ptr_validate(u32 ptr, u32* page_directory);

void* system_calls[] = {0, syscall_open, syscall_close, syscall_read, syscall_write, 
syscall_link, syscall_unlink, syscall_seek, syscall_stat, syscall_rename, syscall_finfo, 
syscall_mount, syscall_umount, syscall_mkdir, syscall_readdir, syscall_openio, syscall_dup, 
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
syscall_fork, syscall_exit, syscall_exec, syscall_wait, syscall_getpinfo, syscall_setpinfo, 
syscall_sig, syscall_sigaction, syscall_sigret, syscall_sbrk};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void syscall_open(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) 
    {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}

    char* path = (char*) ebx;
    if(*path != '/')
    {
        char* opath = path;
        u32 len = strlen(opath);
        u32 dirlen = strlen(current_process->current_dir);
        path = kmalloc(len+dirlen+1);
        strncpy(path, current_process->current_dir, dirlen);
        strncat(path, opath, len);
        *(path+len+dirlen) = 0;    
    }

    fd_t* file = open_file(path, (u8) ecx);
    if(!file) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
            
    if(current_process->files_count == current_process->files_size)
    {
        current_process->files_size*=2; 
        current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));
        memset(current_process->files+current_process->files_size/2, 0, current_process->files_size/2);
    }

    u32 i = 0;
    for(i = 3;i<current_process->files_size;i++)
    {
        if(!current_process->files[i])
        {
            current_process->files[i] = file;
            break;
        }
    }
    current_process->files_count++;
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(i), "N"(ERROR_NONE));
}

void syscall_close(u32 ebx, u32 ecx, u32 edx)
{
    if((ebx >= 3) && (current_process->files_size >= ebx) && (current_process->files[ebx]))
    {
        close_file(current_process->files[ebx]);
        current_process->files[ebx] = 0;
        current_process->files_count--;
    }
}

void syscall_read(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    
    u32 counttr = (u32) current_process->files[ebx]->offset;
    error_t tr = read_file(current_process->files[ebx], (void*) ecx, edx);
    counttr = (u32) (current_process->files[ebx]->offset - counttr);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(counttr), "g"(tr));
}

void syscall_write(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    
    u32 counttr = (u32) current_process->files[ebx]->offset;
    error_t tr = write_file(current_process->files[ebx], (u8*) ecx, edx);
    counttr = (u32) (current_process->files[ebx]->offset - counttr);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(counttr), "g"(tr));
}

void syscall_link(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    char* oldpath = (char*) ebx;
    char* newpath = (char*) ecx;

    error_t tr = link(oldpath, newpath);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr));
}

void syscall_unlink(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    char* path = (char*) ebx;

    error_t tr = unlink(path);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr));
}

void syscall_seek(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    u32 offset = ecx;
    u32 whence = edx;
    //TODO : check file size and offset movement...
    if(whence == 0) current_process->files[ebx]->offset = offset; //SEEK_SET
    else if(whence == 1) current_process->files[ebx]->offset += offset; //SEEK_CUR
    else if(whence == 2) current_process->files[ebx]->offset = flength(current_process->files[ebx])+offset; //SEEK_END
    
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"((u32) current_process->files[ebx]->offset), "N"(ERROR_NONE));
}

void syscall_stat(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    fsnode_t* file = current_process->files[ebx]->file;
            
    u32* ptr = (u32*) edx;
    ptr[0] = (u32) file->file_system->drive;
    ptr[1] = (u32) file->specific; //st_ino
    ptr[2] = 0;//TODO: ptr[2] = current_process->files[ebx]->mode;
    ptr[3] = file->hard_links;
    ptr[4] = 0; // user id
    ptr[5] = 0; // group id
    ptr[6] = 0; // device id
    ptr[7] = (u32) file->length;
    ptr[8] = (u32) file->last_access_time;
    ptr[9] = (u32) file->last_modification_time;
    ptr[10] = (u32) file->last_modification_time;
    ptr[11] = 512; //todo: cluster size or ext2 blocksize
    ptr[12] = (u32) (file->length/512); //todo: clusters or blocks
            
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_rename(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    char* oldpath = (char*) ebx;
    char* newname = (char*) ecx;

    error_t tr = rename(oldpath, newname);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr));
}

void syscall_finfo(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}

    switch(ecx)
    {
        case VK_FINFO_DEVICE_TYPE: 
        {
            fsnode_t* node = current_process->files[ecx]->file;
            if(node->file_system->fs_type == FS_TYPE_DEVFS)
            {
                devfs_node_specific_t* spe = node->specific;
                *((u32*)edx) = spe->device_type;
                break;
            }
            *((u32*)edx) = VK_NOT_A_DEVICE;
            break;
        }
        default:{asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR)); return;}
    }
    
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_mount(u32 ebx, u32 ecx, u32 edx)
{

}

void syscall_umount(u32 ebx, u32 ecx, u32 edx)
{

}

void syscall_mkdir(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}

    fsnode_t* node = create_file((char*) ebx, FILE_ATTR_DIR);
    if(!node) asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR));
    else asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_readdir(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    u8* buffer = (u8*) edx;

    list_entry_t* list = kmalloc(sizeof(list_entry_t));
    u32 size = 0;
    error_t tr = read_directory(current_process->files[ebx], list, &size);
    if((tr != ERROR_NONE) | (ecx >= size))
    {
        if(tr == ERROR_NONE) tr = ERROR_FILE_OUT;
        list_free(list, size);
        asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(tr));
        return;
    }

    list_entry_t* ptr = list;
    u32 i = 0;
    for(i = 0; i < ecx; i++)
    {
        ptr = ptr->next;
    }
    dirent_t* dirent = ptr->element;

    /* assuming POSIX dirent with char name[256] */
    *((u32*) buffer) = dirent->inode;
    u32 minsize = dirent->name_len < 255 ? dirent->name_len : 255;
    memcpy(buffer+sizeof(u32), dirent->name, minsize);
    *(buffer+sizeof(u32)+minsize) = 0;

    list_free(list, size);

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_openio(u32 ebx, u32 ecx, u32 edx)
{
    io_stream_t* iostream = iostream_alloc();
    fd_t* file = kmalloc(sizeof(fd_t));
    file->file = iostream->file; file->offset = 0;

    if(current_process->files_count == current_process->files_size)
    {current_process->files_size*=2; current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));}

    u32 i = 0;
    for(i = 3;i<current_process->files_size;i++)
    {
        if(!current_process->files[i])
        {
            current_process->files[i] = file;
            break;
        }
    }
    current_process->files_count++;

    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(i), "N"(ERROR_NONE));
}

void syscall_dup(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}

    fd_t* oldf = current_process->files[ebx];
    int new = 0;
    if(ecx)
    {
        if(ecx < 3) {asm("mov $0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR)); return;}

        while(current_process->files_size < ecx)
        {
            current_process->files_size*=2; 
            current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t)); 
            memset(current_process->files+current_process->files_size/2, 0, current_process->files_size/2);
        }

        if(current_process->files[ecx]) close_file(current_process->files[ecx]);

        current_process->files[ecx] = oldf;
        new = (int) ecx;
    }
    else
    {
        if(current_process->files_count == current_process->files_size)
        {
            current_process->files_size*=2; 
            current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));
            memset(current_process->files+current_process->files_size/2, 0, current_process->files_size/2);
        }

        u32 i = 0;
        for(i = 3;i<current_process->files_size;i++)
        {
            if(!current_process->files[i])
            {
                current_process->files[i] = oldf;
                break;
            }
        }
        current_process->files_count++;
        new = (int) i;
    }

    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(new), "N"(ERROR_NONE));
}

void syscall_exit(u32 ebx, u32 ecx, u32 edx)
{
    exit_process(current_process, EXIT_CONDITION_USER | ((u8) ebx));
}

void syscall_exec(u32 ebx, u32 ecx, u32 edx)
{
    if((current_process->files_size < ebx) | (!current_process->files[ebx])) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}

    //TODO : UNMAP OLD PROCESS MEMORY !! (but no, what if we fail loading...)
    error_t load = load_executable(current_process, current_process->files[ebx], (int) ecx, (char**) edx);
            
    //TODO : Close file descriptors with 'close_on_exec' flag

    asm("mov %0, %%eax ; mov %0, %%ecx"::"g"(load));
}

void syscall_wait(u32 ebx, u32 ecx, u32 edx)
{

}

void syscall_getpinfo(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    
    process_t* process = 0;
    if(!ebx) process = current_process;
    else process = processes[ebx];
    //TODO : check for process existence and permissions

    switch(ecx)
    {
        case VK_PINFO_PID: {*((int*)edx) = process->pid; break;}
        case VK_PINFO_PPID: {if(process->parent) *((int*)edx) = process->parent->pid; else *((int*)edx) = -1; break;}
        case VK_PINFO_WORKING_DIRECTORY: {strcpy((char*) edx, process->current_dir); break;}
        case VK_PINFO_GID: {*((int*)edx) = process->group->gid; break;}
        default: {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR)); return;}
    }

    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_setpinfo(u32 ebx, u32 ecx, u32 edx)
{
    if(!ptr_validate(edx, current_process->page_directory)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PTR)); return;}
    
    process_t* process = 0;
    if(!ebx) process = current_process;
    else process = processes[ebx];
    //TODO : check for process existence and permissions

    switch(ecx)
    {
        case VK_PINFO_WORKING_DIRECTORY:
        {
            char* newdir = (char*) edx;
            u32 len = strlen(newdir); if(len >= 99) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_OUT)); return;}
            fd_t* t = open_file(newdir, 0);
            if(!t) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_FILE_NOT_FOUND)); return;}
            close_file(t);
            strncpy(process->current_dir, newdir, len);
            *(process->current_dir+len) = 0;
            break;
        }
        default: {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(UNKNOWN_ERROR)); return;}
    }
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_sig(u32 ebx, u32 ecx, u32 edx)
{
    int pid = (int) ebx;
    if((pid <= 0) | (pid > (int) processes_size)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID)); return;}
    if(!processes[pid]) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_PID)); return;}

    int sig = (int) ecx;
    if((sig <= 0) | (sig >= NSIG)) {asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL)); return;}

    send_signal(pid, sig);
    asm("mov %0, %%eax ; mov %0, %%ecx"::"N"(ERROR_NONE));
}

void syscall_sigaction(u32 ebx, u32 ecx, u32 edx)
{
    int sig = (int) ebx;

    if((sig >= NSIG) | (sig <= 0)) {asm("mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL)); return;}
    if((sig == SIGKILL) | (sig == SIGSTOP)) {asm("mov %0, %%ecx"::"N"(ERROR_INVALID_SIGNAL)); return;}
    
    u32 old_handler = (uintptr_t) current_process->signal_handlers[sig];
    current_process->signal_handlers[sig] = (void*) ecx;
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(old_handler), "N"(ERROR_NONE));
}

void syscall_sigret(u32 ebx, u32 ecx, u32 edx)
{
    if(!current_process->sighandler.eip) return;
    current_process->sighandler.eip = 0;
    kfree((void*) current_process->sighandler.base_kstack);
}

void syscall_sbrk(u32 ebx, u32 ecx, u32 edx)
{
    u32 tr = sbrk(current_process, ebx);
    asm("mov %0, %%eax ; mov %1, %%ecx"::"g"(tr), "N"(ERROR_NONE));
}

#pragma GCC diagnostic pop

__asm__(".extern fork_ret \n \
.global syscall_fork \n \
syscall_fork: \n \
push %esp /* push esp to be restored later on the forked process */ \n \
push current_process /* push current process as fork() argument */ \n \
call fork /* call fork() */ \n \
add $0x4, %esp /* clean esp after fork() call */ \n \
mov %ebp, 0x38(%eax) /* restoring unalterable registers (ebp, esi, edi) */ \n \
mov %edi, (%eax) \n \
mov %esi, 0x4(%eax) \n \
movl $fork_ret, 0x30(%eax) /* moving fork_ret addr to EIP of forked process (in eax) */ \n \
pop %ecx /* poping esp to restore in ecx */ \n \
mov current_process, %edx /* move current_process to edx */ \n \
subl 0x4C(%edx), %ecx /* substract base_kstack of current_process to esp */ \n \
addl 0x4C(%eax), %ecx /* add base_kstack of new process to esp */ \n \
mov %ecx, 0x34(%eax) /* restore new process esp */ \n \
push 0x70(%eax) /* push new process pid */ \n \
push %eax /* push new process as argument of scheduler_add_process() */ \n \
call scheduler_add_process /* call scheduler_add_process() */ \n \
add $0x4, %esp /* clean esp after scheduler_add_process() call */ \n \
pop %eax /* pop new process pid into eax (as return value) */ \n \
ret");

int fork_ret()
{
    return 0;
}

static bool ptr_validate(u32 ptr, u32* page_directory)
{
    if(ptr >= 0xC0000000) return false;
    if(!is_mapped(ptr, page_directory)) return false;
    return true;
}
