LDOBJ=kernel.o ckernel.o lib.o gdt.o cpu.o idt.o vga_text.o video.o isrs.o isr.o paging.o error.o pic.o kheap.o physical.o kpageheap.o ata_pio.o block_devices.o pci.o fat32.o vfs.o args.o elf.o syscalls.o process.o keyboard.o data_structs.o scheduler.o ata_dma.o ata_common.o atapi.o iso_9660.o kvmheap.o time.o ext2.o devfs.o stream.o ttys.o asm_scheduler.o asm_mutex.o mutex.o signal.o groups.o threads.o
CPATH=/home/valentin/Programmes/i386-elf-7.2.0/bin
CC=$(CPATH)/i386-elf-gcc -std=gnu11
AS=$(CPATH)/i386-elf-as
LD=$(CPATH)/i386-elf-ld
AFLAGS=--32
CFLAGS=-c -m32 -Wall -Wextra -Wconversion -Wstack-protector -fno-stack-protector -fno-builtin -nostdinc -O -I.
LDFLAGS=-melf_i386 -nostdlib -T link.ld
EXEC=run
QEMU=qemu-system-i386 -enable-kvm # kvm
VBVMNAME=VK
MEDIAPATH=/run/media/valentin/VALOU3433/boot/iso
DISKIMAGE=../extdisk.img
VBOXDISKIMAGE=../disk.vdi

all: $(EXEC)

async:
	make run > /dev/null 2>&1 &

run: kernel
	$(QEMU) -kernel ../kernel.elf -drive id=disk,file=$(DISKIMAGE),index=0,media=disk,format=raw
	rm ../kernel.elf

hddboot: hddimage
	$(QEMU) -drive id=disk,file=$(DISKIMAGE),index=0,media=disk,format=raw

isoboot: iso
	$(QEMU) -boot d -cdrom ../os.iso
	rm ../os.iso

virtualbox: hddimage
	-VBoxManage storageattach $(VBVMNAME) --storagectl "IDE" --device 0 --port 0 --type hdd --medium none
	-VBoxManage closemedium disk $(VBOXDISKIMAGE) --delete
	-rm $(VBOXDISKIMAGE)
	VBoxManage convertdd $(DISKIMAGE) $(VBOXDISKIMAGE)
	VBoxManage storageattach $(VBVMNAME) --storagectl "IDE" --device 0 --port 0 --type hdd --medium $(VBOXDISKIMAGE)
	VBoxManage startvm $(VBVMNAME)

hddimage: kernel
	sudo losetup /dev/loop1 $(DISKIMAGE) -o 1048576
	sudo mount /dev/loop1 /mnt
	-sudo mkdir /mnt/boot
	-sudo mkdir /mnt/sys
	-sudo mkdir /mnt/dev
	sudo cp ../kernel.elf /mnt/sys/kernel.elf
	sync
	sudo umount /dev/loop1
	sudo losetup -d /dev/loop1
	rm ../kernel.elf

kernelc: kernel
	cp ../kernel.elf $(MEDIAPATH)/kernel.elf
	rm ../kernel.elf

isoc: iso
	cp ../os.iso $(MEDIAPATH)/os.iso
	rm ../os.iso

iso: kernel
	cp ../kernel.elf ../iso/sys/kernel.elf
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
	$(LD) $(LDFLAGS) $(LDOBJ) -o ../kernel.elf
	make clean

asmobjects: 
	$(AS) $(AFLAGS) loader.s -o kernel.o
	$(AS) $(AFLAGS) cpu/isr.s -o isr.o
	$(AS) $(AFLAGS) tasking/scheduler/scheduler.s -o asm_scheduler.o
	$(AS) $(AFLAGS) sync/mutex.s -o asm_mutex.o

objects:
	# compile every .c file in every subfolder
	find . -name "*.c"|while read F; do $(CC) $(CFLAGS) $$F; done

clean:
	rm *.o
