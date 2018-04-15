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

    /* call queue_take to get the next processus (in edx) */
    pushl p_ready_queue
    call queue_take
    add $0x4, %esp
    mov %eax, %edx

    /* if !edx, and no next thread, we return */
    movl current_process, %ebx

    test %edx, %edx
    jnz get_next_thread # we have a process switch to do

    /* get old process new thread in eax */
    pushl 0x4(%ebx)
    call queue_take
    add $0x4, %esp

    test %eax, %eax
    jz schedule_pop # if zero we have no more processes and no more threads
    
    mov %ebx, %edx # we don't switch processes, just threads
    jmp schedule_save

    /* if we are switching processes, get new process new thread in eax */
    get_next_thread:
    pushl 0x4(%edx)
    call queue_take
    add $0x4, %esp
    test %eax, %eax
    jnz schedule_save # there is another thread, we'll switch to that

    /* if new process has only one thread, we take back the active thread in eax */
    old_thread:
    mov (%edx), %eax

    schedule_save:
    /* if(current process && current_process != idle_process) we save current process context */
    test %ebx, %ebx
    je schedule_switch
    cmp %ebx, idle_process
    je schedule_switch

    movl (%ebx), %ecx # move active thread in ecx

    /* save general registers */
    popl (%ecx) # edi
    popl 0x4(%ecx) # esi
    popl 0x38(%ecx) # ebp
    popl %esi # esp, we'll see later
    popl 0x8(%ecx) # ebx
    popl 0xC(%ecx) # edx
    popl 0x10(%ecx) # ecx
    popl 0x14(%ecx) # eax

    /* save eip */
    popl 0x30(%ecx) # eip

    /* get cs in ebp and save it*/
    popl %ebp
    movl %ebp, 0x2C(%ecx)

    /* save flags */
    popl 0xC(%ebx)

    /* 
    * if process was in usermode, save interrupt pushed esp and ss 
    * else save current esp (the one in esi) and ss
    */
    cmpl $0x08, %ebp
    je save_kernelmode

    save_usermode:
    popl 0x34(%ecx) # esp
    popl 0x28(%ecx) # ss
    jmp save_segments

    save_kernelmode:
    movl %esi, 0x34(%ecx) # esp
    mov %ss, 0x28(%ecx) # ss

    /* save segment registers */
    save_segments:
    mov %ds, 0x18(%ecx)
    mov %es, 0x1C(%ecx)
    mov %fs, 0x20(%ecx)
    mov %gs, 0x24(%ecx)

    /* put the process back in queue */
    cmp %edx, %ebx # if we are not switching process, just go to switch
    je schedule_switch

    pushl %ebx
    pushl p_ready_queue
    call queue_add
    add $0x8, %esp

    schedule_switch:
    movl %edx, current_process # switch current_process
    movl %eax, (%edx) # switch active_thread

    /* restore segment registers */
    mov 0x18(%eax), %ds
    mov 0x1C(%eax), %es
    mov 0x20(%eax), %fs
    mov 0x24(%eax), %gs

    /* restore page directory */
    movl 0x10(%edx), %esi
    leal 0x40000000(%esi), %edi
    movl %edi, %cr3
    movl %esi, current_page_directory

    /* restore TSS.esp0 (to match process kstack, usefull on a syscall / interrupt) */
    movl 0x3C(%eax), %esi
    movl $TSS, %edi
    movl %esi, 0x4(%edi)

    /*
    * if process was in usermode, we push esp and ss (and let iret do the job)
    * else we restore esp directly
    */
    cmpl $0x08, 0x2C(%eax)
    je restore_kernelmode

    restore_usermode:
    pushl 0x28(%eax) # ss
    pushl 0x34(%eax) # esp
    jmp restore_end

    restore_kernelmode:
    movl 0x34(%eax), %esp

    restore_end:
    /* restore interrupt stack (for iret) */
    orl $0x200, 0xC(%edx) # set interrupt flag if it wasnt
    andl $0xffffbeff, 0xC(%edx) # clear nested task and trap flags if they were set
    push 0xC(%edx) # push flags
    push 0x2C(%eax) # push cs
    push 0x30(%eax) # push eip

    /* restore general registers */
    movl (%eax), %edi # edi
    movl 0x4(%eax), %esi # esi
    movl 0x38(%eax), %ebp # ebp
    movl 0x8(%eax), %ebx # ebx
    movl 0xC(%eax), %edx # edx
    movl 0x10(%eax), %ecx # ecx
    movl 0x14(%eax), %eax # eax

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
