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

.global schedule
schedule:
    /* save interrupt context */
    pushal

    /* call sleep update to update time of sleeping processes */
    call scheduler_sleep_update

    /* call queue_take to get the next processus (in eax) */
    pushl p_ready_queue
    call  queue_take
    add $0x4, %esp

    /* if !eax, we return */
    test %eax, %eax
    je schedule_pop

    /* if(current process && current_process != idle_process) we save current process context */
    mov current_process, %ebx
    test %ebx, %ebx
    je schedule_switch
    cmp %ebx, idle_process
    je schedule_switch

    /* save general registers */
    popl (%ebx) # edi
    popl 0x4(%ebx) # esi
    popl 0x38(%ebx) # ebp
    popl %ecx # esp, we'll see later
    popl 0x8(%ebx) # ebx
    popl 0xC(%ebx) # edx
    popl 0x10(%ebx) # ecx
    popl 0x14(%ebx) # eax

    /* save eip */
    popl 0x30(%ebx) # eip

    /* get cs in edx and save it*/
    popl %edx
    mov %edx, 0x2C(%ebx)

    /* save flags */
    popl 0x3C(%ebx)

    /* 
    * if process was in usermode, save interrupt pushed esp and ss 
    * else save current esp (the one in ecx) and ss
    */
    cmpl $0x08, %edx
    je save_kernelmode

    save_usermode:
    popl 0x34(%ebx) # esp
    popl 0x28(%ebx) # ss
    jmp save_segments

    save_kernelmode:
    mov %ecx, 0x34(%ebx) # esp
    mov %ss, 0x28(%ebx) # ss

    /* save segment registers */
    save_segments:
    mov %ds, 0x18(%ebx)
    mov %es, 0x1C(%ebx)
    mov %fs, 0x20(%ebx)
    mov %gs, 0x24(%ebx)

    schedule_switch:
    mov %eax, current_process # switch current_process

    /* restore segment registers */
    mov 0x18(%eax), %ds
    mov 0x1C(%eax), %es
    mov 0x20(%eax), %fs
    mov 0x24(%eax), %gs

    /* restore page directory */
    mov 0x40(%eax), %esi
    lea 0x40000000(%esi), %edi
    mov %edi, %cr3
    mov %esi, current_page_directory

    /*
    * if process was in usermode, we push esp and ss (and let iret do the job)
    * else we restore esp directly
    */
    cmpl $0x08, 0x2C(%eax)
    je restore_kernelmode

    restore_usermode:
    push 0x28(%eax) # ss
    push 0x34(%eax) # esp
    jmp restore_end

    restore_kernelmode:
    mov 0x34(%eax), %esp

    restore_end:
    /* restore interrupt stack (for iret) */
    orl $0x200, 0x3C(%eax) # set interrupt flag if it wasnt
    andl $0xffffbfff, 0x3C(%eax) # clear nested task flag if it was set
    push 0x3C(%eax) # push flags
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
