nasm -f bin Boot.asm -o Boot.bin
nasm -f elf32 KernelEntry.asm -o KernelEntry.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel.c -o Kernel.o
ld -m elf_i386 -Ttext 0x1000 -e _start --oformat binary KernelEntry.o Kernel.o -o Kernel.bin
dd if=/dev/zero of=AneoEngine.ISO bs=512 count=2880
dd if=Boot.bin of=AneoEngine.ISO conv=notrunc
dd if=Kernel.bin of=AneoEngine.ISO bs=512 seek=1 conv=notrunc
qemu-system-i386 -fda AneoEngine.ISO
