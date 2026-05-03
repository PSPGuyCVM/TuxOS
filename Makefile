# Makefile
ASM = nasm
CC = gcc
LD = ld
CFLAGS = -m32 -fno-pie -ffreestanding -c
LDFLAGS = -m elf_i386 -T linker.ld

all: os-image

# Bootloader
boot.bin: boot.asm disk.asm gdt.asm pm-switch.asm
	$(ASM) -f bin boot.asm -o boot.bin

# Kernel entry object
kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

# Kernel C object
kernel.o: kernel.c
	$(CC) $(CFLAGS) kernel.c -o kernel.o

# Link kernel ELF
kernel.bin: kernel_entry.o kernel.o
	$(LD) $(LDFLAGS) -o kernel.elf kernel_entry.o kernel.o
	objcopy -O binary kernel.elf kernel.bin

# Build disk image
os-image: boot.bin kernel.bin
	cat boot.bin kernel.bin > os-image.bin
	# Pad to 1.44MB floppy size
	dd if=/dev/zero bs=512 count=2878 >> os-image.bin

run: os-image
	qemu-system-i386 -fda os-image.bin -d int -D qemu.log

clean:
	rm -f *.bin *.o *.elf os-image.bin
