
#!/bin/bash

set -e

CC="gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib"

echo "[*] Counting lines per file... "
git ls-files | grep '\.c' | xargs wc -l
git ls-files | grep '\.ASM' | xargs wc -l

echo "[ASM] Assembling bootloader..."
nasm -f bin Boot/Boot.ASM -o Boot/AEBOOT.BIN

echo "[ASM] Assembling kernel entry point..."
nasm -f elf32 Kernel/KEntry.ASM -o KEntry.o

echo "[CC] Compiling kernel..."
$CC -c Kernel/Kernel.c -o Kernel.o

echo "[CC] Compiling PIT functions..."
$CC -c Kernel/PIT.c -o PIT.o

echo "[CC] Compiling keyboard drivers..."
$CC -c Kernel/Keyboard.c -o Keyboard.o

echo "[CC] Compiling haltage funtions..."
$CC -c Kernel/Haltage.c -o Haltage.o

echo "[CC] Compiling startup funtions..."
$CC -c Kernel/Startup.c -o Startup.o

echo "[CC] Compiling logo string..."
$CC -c Kernel/Logo.c -o Logo.o

echo "[CC] Compiling help menu..."
$CC -c Cmds/Help/Menu.c -o HelpMenu.o

echo "[CC] Compiling Help/Info..."
$CC -c Cmds/Help/Info.c -o HelpInfo.o

echo "[CC] Compiling 'addr' command..."
$CC -c Cmds/Addr.c -o Addr.o

echo "[CC] Compiling utilities menu..."
$CC -c Cmds/Utils/Menu.c -o UtilsMenu.o

echo "[CC] Compiling utilities lister..."
$CC -c Cmds/Utils/List.c -o UtilsList.o

echo "[CC] Compiling 'Entropy' utility..."
$CC -c Utils/Entropy.c -o Entropy.o

echo "[CC] Compiling 'Printer' utility..."
$CC -c Utils/Printer.c -o Printer.o

echo "[LD] Creating kernel binary..."
ld -m elf_i386 -Ttext 0x10000 -e _start --oformat binary KEntry.o Kernel.o PIT.o Haltage.o Keyboard.o Startup.o Logo.o HelpMenu.o Addr.o UtilsMenu.o Printer.o Entropy.o HelpInfo.o UtilsList.o -o Boot/KERNEL.BIN

echo "[DD] Initializing AneoEngine CDROM image"
dd if=/dev/zero of=AneoEngine.ISO bs=512 count=2880

echo "[DD] Adding bootloader to 'AneoEngine.ISO'"
dd if=Boot/AEBOOT.BIN of=AneoEngine.ISO conv=notrunc

echo "[DD] Adding kernel to 'AneoEngine.ISO'..."
dd if=Boot/KERNEL.BIN of=AneoEngine.ISO bs=512 seek=1 conv=notrunc

echo "[*] Removing trash..."
rm *.o

echo "[+] Done!"
echo "[*] Running 'AneoEngine.ISO'..."
qemu-system-x86_64 -fda AneoEngine.ISO

