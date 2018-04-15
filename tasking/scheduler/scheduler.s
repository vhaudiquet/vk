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

# SCHEDULER (assembly because we need it optimized/we have to get to the lowest possible level)

.extern current_process
.extern p_ready_queue
.extern queue_take
.extern idle_process
.extern current_page_directory
.extern scheduler_sleep_update
.extern handle_signals
.extern TSS

.global schedule
.global schedule_switch
schedule:
    /* save interrupt context */
    pushal

    /* call sleep update to update time of sleeping processes */
    call scheduler_sleep_update

    /* call handle_signals to handle every incoming process signal */
    call handle_signals

    /* call queue_take to get the next processus (in eax) */
    pushl p_ready_queue
    call queue_take
    add $0x4, %esp

    /* if !eax, and no next thread, we return */
    test %eax, %eax
    jne schedule_save
    movl current_process, %ebx
    movl 0xC(%ebx), %esi # move active thread in esi
    movl 0x8(%ebx), %edi # move threads_count in edi
    subl $1, %edi # substract 1
    cmp %esi, %edi # compare with active_thread
    jle schedule_pop # if less/equal, we have no more processes and no more threads

    schedule_save:
    /* if(current process && current_process != idle_process) we save current process context */
    test %ebx, %ebx
    je schedule_switch
    cmp %ebx, idle_process
    je schedule_switch

    movl (%ebx), %ecx # move threads addr in ecx
    leal (%ecx, %esi, 4), %ecx # move active thread addr in ecx

    /* save general registers */
    popl (%ecx) # edi
    popl 0x4(%ecx) # esi
    popl 0x38(%ecx) # ebp
    popl %ebp # esp, we'll see later
    popl 0x8(%ecx) # ebx
    popl 0xC(%ecx) # edx
    popl 0x10(%ecx) # ecx
    popl 0x14(%ecx) # eax

    /* save eip */
    popl 0x30(%ecx) # eip

    /* get cs in edx and save it*/
    popl %edx
    movl %edx, 0x2C(%ecx)

    /* save flags */
    popl 0x10(%ebx)

    /* 
    * if process was in usermode, save interrupt pushed esp and ss 
    * else save current esp (the one in ecx) and ss
    */
    cmpl $0x08, %edx
    je save_kernelmode

    save_usermode:
    popl 0x34(%ecx) # esp
    popl 0x28(%ecx) # ss
    jmp save_segments

    save_kernelmode:
    movl %ebp, 0x34(%ecx) # esp
    mov %ss, 0x28(%ecx) # ss

    /* save segment registers */
    save_segments:
    mov %ds, 0x18(%ecx)
    mov %es, 0x1C(%ecx)
    mov %fs, 0x20(%ecx)
    mov %gs, 0x24(%ecx)

    schedule_switch:
    movl %eax, current_process # switch current_process

    /* if we can go to next thread, we do */

    movl 0xC(%eax), %esi # move active thread in esi
    movl 0x8(%eax), %edi # move threads_count in edi
    subl $1, %edi # substract 1
    cmp %esi, %edi # compare with active_thread
    jle schedule_restore # if less/equal, we have no more processes and no more threads
    addl $1, 0xC(%eax) # update active_thread
    addl $1, %esi

    schedule_restore:
    movl (%eax), %ecx # move threads addr in ecx
    leal (%ecx, %esi, 4), %ecx # move active thread addr in ecx

    /* restore segment registers */
    mov 0x18(%ecx), %ds
    mov 0x1C(%ecx), %es
    mov 0x20(%ecx), %fs
    mov 0x24(%ecx), %gs

    /* restore page directory */
    movl 0x14(%eax), %esi
    leal 0x40000000(%esi), %edi
    movl %edi, %cr3
    movl %esi, current_page_directory

    /* restore TSS.esp0 (to match process kstack, usefull on a syscall / interrupt) */
    movl 0x3C(%ecx), %esi
    movl $TSS, %edi
    movl %esi, 0x4(%edi)

    /*
    * if process was in usermode, we push esp and ss (and let iret do the job)
    * else we restore esp directly
    */
    cmpl $0x08, 0x2C(%ecx)
    je restore_kernelmode

    restore_usermode:
    pushl 0x28(%ecx) # ss
    pushl 0x34(%ecx) # esp
    jmp restore_end

    restore_kernelmode:
    movl 0x34(%ecx), %esp

    restore_end:
    /* restore interrupt stack (for iret) */
    orl $0x200, 0x10(%eax) # set interrupt flag if it wasnt
    andl $0xffffbeff, 0x10(%eax) # clear nested task and trap flags if they were set
    push 0x10(%eax) # push flags
    push 0x2C(%ecx) # push cs
    push 0x30(%ecx) # push eip

    /* restore general registers */
    movl (%ecx), %edi # edi
    movl 0x4(%ecx), %esi # esi
    movl 0x38(%ecx), %ebp # ebp
    movl 0x8(%ecx), %ebx # ebx
    movl 0xC(%ecx), %edx # edx
    movl 0x14(%ecx), %eax # eax
    movl 0x10(%ecx), %ecx # ecx

    jmp schedule_end

    schedule_pop:
    popal

    schedule_end:
    /* tell the pic that we have handled interrupt */
    push %ax
    mov $0x20, %al
    out %al, $0x20 
    pop %ax

    iret
