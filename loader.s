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

# HIGHER-HALF KERNEL LOADER
# Loaded by the bootloader, setup identity paging + 0-4MiB to 3GB, jumps to 3GB, unmaps identity paging, jump to c (kmain)
.extern kmain

# Multiboot HEADER
.equ MULTIBOOT_PAGE_ALIGN, 1<<0
.equ MULTIBOOT_MEMORY_INFO, 1<<1
.equ MULTIBOOT_HEADER_MAGIC, 0x1BADB002
.equ MULTIBOOT_HEADER_FLAGS, MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
.equ MULTIBOOT_CHECKSUM, -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

# kernel virtual address
.equ KERNEL_VIRTUAL_BASE, 0xC0000000
.equ KERNEL_PAGE_NUMBER, (KERNEL_VIRTUAL_BASE >> 22)

.lcomm temp_page_table, 4096

.section .text
.align 4
multiboot_header:
    .int MULTIBOOT_HEADER_MAGIC
    .int MULTIBOOT_HEADER_FLAGS
    .int MULTIBOOT_CHECKSUM

# .equ _start, start-KERNEL_VIRTUAL_BASE
# .global _start
.global start
start:
    # loop to fill first page table (identity mapping the fisrt 4MiB)
    movl $0, %eax
    .l:
    movl %eax, %edx
    sall $10, %edx
    orl $3, %edx
    movl %edx, temp_page_table-KERNEL_VIRTUAL_BASE(%eax)
    addl $4, %eax
    cmpl $4096, %eax
    jne .l
    # loop to fill second page table (mapping the first 4MiB to 0xC0000000)
    movl $0, %eax
    .l1:
    movl %eax, %edx
    sall $10, %edx
    orl $3, %edx
    movl %edx, kernel_page_table-KERNEL_VIRTUAL_BASE(%eax)
    addl $4, %eax
    cmpl $4096, %eax
    jne .l1
    # putting first page table address into page directory
    movl $temp_page_table-KERNEL_VIRTUAL_BASE, %eax
	orl	$3, %eax
	movl %eax, kernel_page_directory-KERNEL_VIRTUAL_BASE
    # putting second page table address into page directory at 3GB(/4) index
    movl $kernel_page_table-KERNEL_VIRTUAL_BASE, %eax
	orl	$3, %eax
	movl %eax, kernel_page_directory+KERNEL_PAGE_NUMBER*4-KERNEL_VIRTUAL_BASE # page entry is a pointer = 4 bytes unsigned int
    # load page directory address into CR3
    movl $kernel_page_directory-KERNEL_VIRTUAL_BASE, %eax
    mov %eax, %cr3
    # enable paging
    mov %cr0, %eax
    or $0x80000001, %eax # 80000001 looks better than 80000000 (set pe bit if wasnt)
    mov %eax, %cr0
    # jump to higher half code
    lea (_high), %ecx
    jmp *%ecx

_high:
    # unmaps the first 4 MiB, no need anymore as they are mapped to 0xC0000000
    movl $0, (kernel_page_directory)
    invlpg (0)
    # setup the stack
    mov $stack+8192, %esp
    # pussh multiboot structure virt addr and jump to the kernel c code (wtf is wrong with that syntaxic coloring)
    addl $KERNEL_VIRTUAL_BASE, %ebx
    pushl $stack+8192
    pushl %ebx
 	call kmain
    # halt if kernel function returns
    hlt

.lcomm stack, 8192 # 8192
