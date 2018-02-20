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
#include "task.h"
#include "video/video.h"
#include "memory/mem.h"

bool ptr_validate(u32 ptr, u32* page_directory)
{
    if(ptr >= 0xC0000000) return false;
    if(!is_mapped(ptr, page_directory)) return false;
    return true;
}

void syscall_global(u32 syscall_number, u32 ebx, u32 ecx, u32 edx)
{
    // kprintf("%lSyscall %u : %X, %X, %X\n", 3, syscall_number, ebx, ecx, edx);
    switch(syscall_number)
    {
        //1:Syscall OPEN
        case 1:
        {
            if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov $0, %eax"); return;}
            
            char* path = (char*) ebx;
            fd_t* file = open_file(path, (u8) ecx);
            if(!file) {asm("mov $0, %eax"); return;}
            
            if(current_process->files_count == current_process->files_size)
            {current_process->files_size*=2; current_process->files = krealloc(current_process->files, current_process->files_size*sizeof(fd_t));}

            current_process->files[current_process->files_count] = file;
            current_process->files_count++;
            asm("mov %0, %%eax"::"g"(current_process->files_count-1));
            break;
        }
        //2:Syscall CLOSE
        case 2:
        {
            if(current_process->files_count >= ebx && current_process->files[ebx])
            {
                close_file(current_process->files[ebx]);
                current_process->files_count--;
                memcpy((void*) ((uintptr_t)current_process->files)+ebx*sizeof(uintptr_t), (void*) ((uintptr_t)current_process->files)+(ebx+1)*sizeof(uintptr_t), current_process->files_count-ebx);
            }
            break;
        }
        //3:Syscall READ
        case 3:
        {
            if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov $1, %eax"); return;}
            if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            
            error_t tr = read_file(current_process->files[ebx], (void*) ecx, edx);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //4:Syscall WRITE
        case 4:
        {
            if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov $1, %eax"); return;}
            if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            error_t tr = write_file(current_process->files[ebx], (u8*) ecx, edx);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //5:Syscall RENAME
        case 5:
        {
            if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            char* oldpath = (char*) ebx;
            char* newname = (char*) ecx;

            error_t tr = rename_file(oldpath, newname);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //6:Syscall LINK
        case 6:
        {
            if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            if(!ptr_validate(ecx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            char* oldpath = (char*) ebx;
            char* newpath = (char*) ecx;

            error_t tr = link(oldpath, newpath);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //7:Syscall UNLINK
        case 7:
        {
            if(!ptr_validate(ebx, current_process->page_directory)) {asm("mov $1, %eax"); return;}
            char* path = (char*) ebx;

            error_t tr = unlink(path);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //8:Syscall SEEK
        case 8:
        {
            if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov $1, %eax"); return;}
            u32 offset = ecx;
            u32 whence = edx;
            if(whence == 0) current_process->files[ebx]->offset = offset; //SEEK_SET
            else if(whence == 1) current_process->files[ebx]->offset += offset; //SEEK_CUR
            else if(whence == 2) current_process->files[ebx]->offset = flength(current_process->files[ebx])+offset; //SEEK_END
            
            asm("mov %0, %%eax"::"g"((u32) current_process->files[ebx]->offset));
            break;
        }
        //9:Syscall STAT
        case 9:
        {
            if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov $0, %eax"); return;}
            if(!ptr_validate(edx, current_process->page_directory)) {asm("mov $0, %eax"); return;}
            fsnode_t* file = current_process->files[ebx]->file;
            
            u32* ptr = (u32*) edx;
            ptr[0] = (u32) file->file_system->drive;
            ptr[1] = (u32) file->specific; //st_ino
            ptr[2] = 0;//TODO: ptr[2] = current_process->files[ebx]->mode;
            ptr[3] = 1; //TODO : hard links if ext2, 1 else
            ptr[4] = 0; // user id
            ptr[5] = 0; // group id
            ptr[6] = 0; // device id
            ptr[7] = (u32) file->length;
            ptr[8] = (u32) file->last_access_time;
            ptr[9] = (u32) file->last_modification_time;
            ptr[10] = (u32) file->last_modification_time;
            ptr[11] = 512; //todo: cluster size or ext2 blocksize
            ptr[12] = (u32) (file->length/512); //todo: clusters or blocks
            
            asm("mov $1, %eax");
            break;
        }
        //11:Syscall EXEC
        case 11:
        {
            if((current_process->files_count < ebx) | (!current_process->files[ebx])) {asm("mov $0, %eax"); return;}
            if(!ptr_validate(edx, current_process->page_directory)) {asm("mov $0, %eax"); return;}

            process_t* process = create_process(current_process->files[ebx], (int) ecx, (char**) edx, current_process->tty);
            scheduler_add_process(process);
            break;
        }
        //12:Syscall EXIT
        case 12:
        {
            exit_process(current_process);
            break;
        }
        //13:Syscall FORK
        case 13:
        {
            process_t* new = fork(current_process);
            asm("mov %0, %%eax"::"g"(new->pid));

            new->gregs.eax = 0;
            new->eip = (u32) &&fork_ret;
            scheduler_add_process(new);

            fork_ret:break;
        }
        //17:Syscall SBRK
        case 17:
        {
            u32 tr = sbrk(current_process, ebx);
            asm("mov %0, %%eax"::"g"(tr));
            break;
        }
        //18:Syscall GETPID
        case 18:
        {
            asm("mov %0, %%eax"::"g"(current_process->pid));
            break;
        }
        //19:Syscall KILL
        case 19:
        {
            break;
        }
        //21:Syscall GET_CURENT_TIME
        case 21:
        {
            break;
        }
        //22:Syscall SUSPEND
        case 22:
        {
            break;
        }
    }
}
