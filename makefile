LDOBJ=kernel.o ckernel.o lib.o gdt.o cpu.o idt.o vga_text.o video.o isrs.o isr.o paging.o error.o pic.o kheap.o physical.o kpageheap.o ata_pio.o block_devices.o pci.o fat32.o vfs.o args.o elf.o syscalls.o process.o keyboard.o ramfs.o data_structs.o scheduler.o
AS=as
CC=gcc
AFLAGS=--32
CFLAGS=-c -m32 -Wall -Wextra -Wconversion -Wstack-protector -fno-stack-protector -fno-builtin -nostdinc -O -g -I.
LDFLAGS=-melf_i386 -nostdlib -T link.ld
EXEC=run

all: $(EXEC)

async:
	make run > /dev/null 2>&1 &

run: kernel
	qemu-system-i386 -kernel ../kernel.elf -drive id=disk,file=../disk.img,index=0,media=disk,format=raw
	rm ../kernel.elf

hddboot: kernel
	sudo losetup /dev/loop1 ../disk.img -o 1048576
	sudo mount /dev/loop1 /mnt
	sudo cp ../kernel.elf /mnt/boot/kernel.elf
	sync
	sudo umount /dev/loop1
	sudo losetup -d /dev/loop1
	rm ../kernel.elf
	qemu-system-i386 -drive id=disk,file=../disk.img,index=0,media=disk,format=raw

isoboot: iso
	qemu-system-i386 -boot d -cdrom ../os.iso -drive id=disk,file=../disk.img,index=0,media=disk,format=raw
	rm ../os.iso

kernelc: kernel
	cp ../kernel.elf /media/valentin/MULTIBOOT/files/kernel/kernel.elf
	rm ../kernel.elf

isoc: iso
	cp ../os.iso /media/valentin/MULTIBOOT/multiboot/ISOS/os.iso
	rm ../os.iso

iso: kernel
	cp ../kernel.elf ../iso/boot/kernel.elf
	genisoimage -R                              \
                -b boot/grub/stage2_eltorito    \
                -no-emul-boot                   \
                -boot-load-size 4               \
                -A os                           \
                -input-charset utf8             \
                -quiet                          \
                -boot-info-table                \
                -o ../os.iso                       \
                ../iso
	rm ../kernel.elf

kernel: asmobjects objects
	# Why does ld needs such an order ?
	ld $(LDFLAGS) $(LDOBJ) -o ../kernel.elf
	make clean

asmobjects: 
	$(AS) $(AFLAGS) loader.s -o kernel.o
	$(AS) $(AFLAGS) cpu/isr.s -o isr.o

objects:
	find . -name "*.c"|while read F; do $(CC) $(CFLAGS) $$F; done

clean:
	rm *.o