.extern current_process
.global mutex_lock
mutex_lock:
    mov 4(%esp), %eax
    cli
    movl (%eax), %ebx
    test %ebx, %ebx
    jnz lock_end_bad
    movl current_process, %ebx
    movl %ebx, (%eax)
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
    movl (%eax), %ebx
    cmpl %ebx, current_process
    jne unlock_end_bad
    movl $0, (%eax)
    movl $0, %eax
    ret
    unlock_end_bad:
    movl $32, %eax
    ret
