git ls-files | grep '\.c' | xargs wc -l
git ls-files | grep '\.ASM' | xargs wc -l
nasm -f bin Boot/Boot.ASM -o Boot/AEBOOT.BIN
nasm -f elf32 Kernel/KEntry.ASM -o KEntry.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Kernel.c -o Kernel.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/PIT.c -o PIT.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Keyboard.c -o Keyboard.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Haltage.c -o Haltage.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Startup.c -o Startup.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Kernel/Logo.c -o Logo.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Cmds/Help/Menu.c -o HelpMenu.o
gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib -c Cmds/Addr.c -o Addr.o
ld -m elf_i386 -Ttext 0x1000 -e _start --oformat binary KEntry.o Kernel.o PIT.o Haltage.o Keyboard.o Startup.o Logo.o HelpMenu.o Addr.o -o Boot/KERNEL.BIN
dd if=/dev/zero of=AneoEngine.ISO bs=512 count=2880
dd if=Boot/AEBOOT.BIN of=AneoEngine.ISO conv=notrunc
dd if=Boot/KERNEL.BIN of=AneoEngine.ISO bs=512 seek=1 conv=notrunc
rm *.o
qemu-system-i386 -fda AneoEngine.ISO
