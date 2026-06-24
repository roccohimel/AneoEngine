#!/bin/bash

set -e
shopt -s inherit_errexit

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
	local dir
	local d
	local name
	local cname
	local text
	dir="$1"

	# shellcheck disable=SC2312
	find "${dir}" -mindepth 1 -maxdepth 1 -type d | sort | while read -r d
	do
		name=$(basename "${d}")
		cname=$(esc_name "${name}")

		echo "	as_mkdir(\"${cname}\");"
		echo "	as_cd(\"${cname}\");"

		gen_dir "${d}"

		echo "	as_cd(\"..\");"
	done

	# shellcheck disable=SC2312
	find "${dir}" -mindepth 1 -maxdepth 1 -type f | sort | while read -r f
	do
		name=$(basename "${f}")
		cname=$(esc_name "${name}")
		text=$(esc_file "${f}")

		echo "	as_touch(\"${cname}\");"
		echo "	as_write(\"${cname}\", \"${text}\");"
	done
}

{
	echo "	${START}"
	echo "	as_cd(\"/\");"

	if [[ -d "${ROOT}" ]]; then
		gen_dir "${ROOT}"
	fi

	echo "	${END}"
} > "${SEED}"

build()
{
	echo "[*] Rebuiling AnchorSand seed..."
	awk -v start="${START}" -v seed="${SEED}" '
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
	' "${KERNEL}" > "${TMP}"

	mv "${TMP}" "${KERNEL}"

	echo "[+] AnchorSand seed rebuilt cleanly"

	CC="gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib"

	echo "[ASM] Assembling CD-ROM bootloader..."
	nasm -f bin Boot/Boot.ASM -o Boot/AEBOOT.BIN

	echo "[ASM] Assembling floppy disk/USB bootloader..."
	nasm -f bin Boot/FIBoot.ASM -o Boot/FIAEBOOT.BIN

	echo "[ASM] Assembling kernel entry point..."
	nasm -f elf32 Kernel/DiskThunk.ASM -o DiskThunk.o

	echo "[CC] Compiling kernel..."
	${CC} -c Kernel/Kernel.c -o Kernel.o

	echo "[CC] Compiling AnchorSand..."
	${CC} -c Kernel/AnchorSand.c -o AnchorSand.o

	echo "[ASM] Assembling AnchorSand disk drivers..."
	nasm -f elf32 Kernel/KEntry.ASM -o KEntry.o

	echo "[CC] Compiling AnchorSand disk save funtions..."
        ${CC} -c Kernel/FSSave.c -o FSSave.o

	echo "[CC] Compiling PIT functions..."
	${CC} -c Kernel/PIT.c -o PIT.o

	echo "[CC] Compiling keyboard drivers..."
	${CC} -c Kernel/Keyboard.c -o Keyboard.o

	echo "[CC] Compiling haltage funtions..."
	${CC} -c Kernel/Haltage.c -o Haltage.o

	echo "[CC] Compiling startup funtions..."
	${CC} -c Kernel/Startup.c -o Startup.o

	echo "[CC] Compiling IDT funtions..."
	nasm -f elf32 Kernel/ISR.ASM -o ISR.o
	${CC} -c Kernel/IDT.c -o IDT.o

	echo "[CC] Compiling help menu..."
	${CC} -c Cmds/Help/Menu.c -o HelpMenu.o

	echo "[CC] Compiling 'addr' command..."
	${CC} -c Cmds/Addr.c -o Addr.o

	echo "[CC] Compiling F4 run funtion..."
	${CC} -c Cmds/F4.c -o F4.o

	echo "[CC] Compiling utilities menu..."
	${CC} -c Cmds/Utils/Menu.c -o UtilsMenu.o

	echo "[CC] Compiling utilities lister..."
	${CC} -c Cmds/Utils/List.c -o UtilsList.o

	echo "[CC] Compiling 'Entropy' utility..."
	${CC} -c Utils/Entropy.c -o Entropy.o

	echo "[CC] Compiling 'Printer' utility..."
	${CC} -c Utils/Printer.c -o Printer.o

	echo "[LD] Creating kernel binary..."
	ld -m elf_i386 -Ttext 0x10000 -e _start --oformat binary \
		KEntry.o \
		Kernel.o \
		AnchorSand.o \
		DiskThunk.o \
		FSSave.o \
		PIT.o \
		Haltage.o \
		Keyboard.o \
		Startup.o \
		IDT.o \
		ISR.o \
		HelpMenu.o \
		Addr.o \
		F4.o \
		UtilsMenu.o \
		Printer.o \
		Entropy.o \
		UtilsList.o \
		-o Boot/KERNEL.BIN

	echo "[*] Removing existing disk images..."
	touch Fallback.IMG
	touch Fallback.ISO
	rm -- *.IMG
	rm -- *.ISO

	echo "[DD] Initializing AneoEngine floppy disk image..."
	dd if=/dev/zero of=Boot/AneoEngine.IMG bs=512 count=2880

	echo "[DD] Adding bootloader to 'AneoEngine.IMG'..."
	dd if=Boot/AEBOOT.BIN of=Boot/AneoEngine.IMG conv=notrunc

	echo "[DD] Adding kernel to 'AneoEngine.IMG'..."
	dd if=Boot/KERNEL.BIN of=Boot/AneoEngine.IMG bs=512 seek=1 conv=notrunc

	genisoimage \
		-V "AneoEngine Media" \
		-o AneoEngine.ISO \
		-b AneoEngine.IMG \
		-c Boot.CAT \
		Boot

	echo "[DD] Patching floppy disk/USB bootloader boot sector into ISO..."
	dd if=Boot/FIAEBOOT.BIN of=AneoEngine.ISO bs=512 count=1 conv=notrunc

	echo "[*] Removing trash..."
	rm -- *.o

	echo "[+] Done!"
}

echo "[*] Building image to calculate kernel LBA..."
build
echo "[ISO] Finding KERNEL.BIN LBA..."

FLOPPY_KERNEL_LBA=""

for LBA in $(seq 0 20000); do
	dd if=AneoEngine.ISO of=/tmp/kernel-test.bin \
		bs=512 skip="${LBA}" count=1 status=none

	if cmp -n 512 Boot/KERNEL.BIN /tmp/kernel-test.bin >/dev/null; then
		FLOPPY_KERNEL_LBA=${LBA}
		break
	fi
done

if [[ -z "${FLOPPY_KERNEL_LBA}" ]]; then
	echo "[ERROR] Failed to locate KERNEL.BIN inside ISO!"
	exit 1
fi

echo "[ISO] FLOPPY_KERNEL_LBA = ${FLOPPY_KERNEL_LBA}"

echo "[ISO] Patching FIBoot.ASM..."

sed -i "s/^FLOPPY_KERNEL_LBA equ .*/FLOPPY_KERNEL_LBA equ ${FLOPPY_KERNEL_LBA}/" Boot/FIBoot.ASM

echo "[ASM] Assembling floppy disk/USB bootloader..."
nasm -f bin Boot/FIBoot.ASM -o Boot/FIAEBOOT.BIN

dd if=Boot/FIAEBOOT.BIN of=AneoEngine.ISO \
	bs=512 count=1 conv=notrunc

echo "[*] Building final image..."
build

RC1="qemu-system-x86_64 -cdrom AneoEngine.ISO"
RC2="qemu-system-x86_64 -drive file=AneoEngine.ISO,format=raw"

echo "========================"
echo "QEMU run commands:"
echo "${RC1} - use this if you want to boot with a standard drive"
echo "${RC2} - use this if you want to boot with a CD-ROM"
echo " Both commands work with ...-i386 aswell."
echo " "

if [[ "$1" = "-RC1" ]]; then
	echo "[*] Running 'AneoEngine.ISO'..."
	eval "${RC1}"
elif [[ "$1" = "-RC2" ]]; then
	echo "[*] Running 'AneoEngine.ISO'..."
	eval "${RC2}"
fi
 
