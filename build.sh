
#!/bin/bash

set -e

cp README Root/Home
cp CHANGELOG Root/Home
cp LICENSE Root/Docs

#!/bin/sh

ROOT="Root"
KERNEL="Kernel/Kernel.c"
TMP="/tmp/Kernel.c.as"
SEED="/tmp/as_seed.txt"

START="/* ANCHORSAND SEED START */"
END="/* ANCHORSAND SEED END */"

esc_file()
{
	awk '
	{
		gsub(/\\/, "\\\\")
		gsub(/"/, "\\\"")
		gsub(/\t/, "\\t")

		if(NR > 1)
			printf "\\n"

		printf "%s", $0
	}
	' "$1"
}

esc_name()
{
	printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

gen_dir()
{
	dir="$1"

	find "$dir" -mindepth 1 -maxdepth 1 -type d | sort | while read d
	do
		name=$(basename "$d")
		cname=$(esc_name "$name")

		echo "	as_mkdir(\"$cname\");"
		echo "	as_cd(\"$cname\");"

		gen_dir "$d"

		echo "	as_cd(\"..\");"
	done

	find "$dir" -mindepth 1 -maxdepth 1 -type f | sort | while read f
	do
		name=$(basename "$f")
		cname=$(esc_name "$name")
		text=$(esc_file "$f")

		echo "	as_touch(\"$cname\");"
		echo "	as_write(\"$cname\", \"$text\");"
	done
}

{
	echo "	$START"
	echo "	as_cd(\"/\");"

	if [ -d "$ROOT" ]; then
		gen_dir "$ROOT"
	fi

	echo "	$END"
} > "$SEED"

awk -v start="$START" -v seed="$SEED" '
	index($0, start) {
		wipe = 1
		next
	}

	wipe && index($0, "shell();") {
		while((getline line < seed) > 0)
			print line
		close(seed)

		print $0
		wipe = 0
		next
	}

	wipe {
		next
	}

	index($0, "shell();") {
		while((getline line < seed) > 0)
			print line
		close(seed)

		print $0
		next
	}

	{
		print
	}
' "$KERNEL" > "$TMP"

mv "$TMP" "$KERNEL"

echo "AnchorSand seed rebuilt cleanly"

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

echo "[CC] Compiling AnchorSand..."
$CC -c Kernel/AnchorSand.c -o AnchorSand.o

echo "[CC] Compiling PIT functions..."
$CC -c Kernel/PIT.c -o PIT.o

echo "[CC] Compiling keyboard drivers..."
$CC -c Kernel/Keyboard.c -o Keyboard.o

echo "[CC] Compiling haltage funtions..."
$CC -c Kernel/Haltage.c -o Haltage.o

echo "[CC] Compiling startup funtions..."
$CC -c Kernel/Startup.c -o Startup.o

echo "[CC] Compiling IDT funtions..."
nasm -f elf32 Kernel/ISR.ASM -o ISR.o
$CC -c Kernel/IDT.c -o IDT.o

echo "[CC] Compiling help menu..."
$CC -c Cmds/Help/Menu.c -o HelpMenu.o

echo "[CC] Compiling 'addr' command..."
$CC -c Cmds/Addr.c -o Addr.o

echo "[CC] Compiling F4 run funtion..."
$CC -c Cmds/F4.c -o F4.o

echo "[CC] Compiling utilities menu..."
$CC -c Cmds/Utils/Menu.c -o UtilsMenu.o

echo "[CC] Compiling utilities lister..."
$CC -c Cmds/Utils/List.c -o UtilsList.o

echo "[CC] Compiling 'Entropy' utility..."
$CC -c Utils/Entropy.c -o Entropy.o

echo "[CC] Compiling 'Printer' utility..."
$CC -c Utils/Printer.c -o Printer.o

echo "[LD] Creating kernel binary..."
ld -m elf_i386 -Ttext 0x10000 -e _start --oformat binary KEntry.o Kernel.o AnchorSand.o PIT.o Haltage.o Keyboard.o Startup.o IDT.o ISR.o HelpMenu.o Addr.o F4.o UtilsMenu.o Printer.o Entropy.o UtilsList.o -o Boot/KERNEL.BIN

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


