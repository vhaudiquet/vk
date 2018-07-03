/*  
    This file is part of VK.
    Copyright (C) 2018 Valentin Haudiquet

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

.extern current_process
.extern mutex_unlock_wakeup
.global mutex_lock
mutex_lock:
    mov 4(%esp), %eax
    cli
    movl (%eax), %ecx
    test %ecx, %ecx
    jnz lock_end_bad
    movl current_process, %ecx
    movl %ecx, (%eax)
    sti
    movl $0, %eax
    ret

    lock_end_bad:
    sti
    movl $31, %eax 
    ret

.global mutex_unlock
mutex_unlock:
    mov 4(%esp), %eax
    movl (%eax), %ecx
    cmpl %ecx, current_process
    jne unlock_end_bad
    movl $0, (%eax)

    # waking up other processes
    pushl %eax
    call mutex_unlock_wakeup
    addl $0x4, %esp

    movl $0, %eax
    ret
    unlock_end_bad:
    movl $32, %eax
    ret
