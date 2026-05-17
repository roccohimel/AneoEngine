nasm -f bin Boot/Boot.ASM -o Boot/AEBOOT.BIN
nasm -f elf32 Kernel/KEntry.ASM -o KEntry.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Kernel.c -o Kernel.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/PIT.c -o PIT.o
ld -m elf_i386 -Ttext 0x1000 -e _start --oformat binary KEntry.o Kernel.o PIT.o -o Boot/KERNEL.BIN
dd if=/dev/zero of=AneoEngine.ISO bs=512 count=2880
dd if=Boot/AEBOOT.BIN of=AneoEngine.ISO conv=notrunc
dd if=Boot/KERNEL.BIN of=AneoEngine.ISO bs=512 seek=1 conv=notrunc
qemu-system-i386 -fda AneoEngine.ISO
