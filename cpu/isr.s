.section .text
.align 4

.extern irq_handler
.macro IRQ index
    .global _irq\index
    _irq\index:
        cli
        push $\index
        call irq_handler
        add $4, %esp # clear index to restore stack
        push %ax # save ax
        mov $0x20, %al
        out %al, $0x20 # -> tells the PIC that it's OK, we've handled the interrupt, you can send more
        pop %ax # restore ax
        iret
.endm

.global CLOCK_IRQ
CLOCK_IRQ:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    cli
    call schedule
    mov $0x20, %al
    out %al, $0x20 # -> tells the PIC that it's OK, we've handled the interrupt, you can send more
    add $16, %esp
    popa
    iret

.macro ISR_NOERR index
    .global _isr\index
    _isr\index:
        cli
        push $0
        push $\index
        jmp isr_common
.endm

.macro ISR_ERR index
    .global _isr\index
    _isr\index:
        cli
        push $\index
        jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

# IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15
IRQ 16
IRQ 17
IRQ 18
IRQ 19
IRQ 20

.extern fault_handler
.type fault_handler, @function

isr_common:
    /* Push all registers */
    pusha

    /* Save segment registers */
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    cld

    /* Call fault handler */
    push %esp
    call fault_handler
    add $4, %esp

    /* Restore segment registers */
    pop %gs
    pop %fs
    pop %es
    pop %ds

    /* Restore registers */
    popa
    /* Cleanup error code and ISR # */
    add $8, %esp
    /* pop CS, EIP, EFLAGS, SS and ESP */
    iret

.global SYSCALL_H
SYSCALL_H:
    # cli
    # pusha
    pushl %edx
    pushl %ebx
    pushl %eax
    call syscall_global
    popl %eax
    popl %ebx
    popl %edx
    # popa
    iret
