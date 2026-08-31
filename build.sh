#!/bin/bash

#AneoEngine official build script
set -e
#copy repo files to AnchorSand live fs
cp README Root/Home
cp CHANGELOG Root/Home
cp LICENSE Root/Docs
#init vars
ROOT="Root"
KERNEL="Kernel/Startup.AC"
TMP="/tmp/Startup.AC.as"
SEED="/tmp/as_seed.txt"
START="/* ANCHORSAND SEED START */"
END="/* ANCHORSAND SEED END */"

esc_file()
{ #escapes files for formatting
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
{ #escape str
	printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

gen_dir()
{ #prime dirs based on Root/
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

build()
{ #AE compilation/assembly
	#AnchorSand rebuild for live CD
	echo "AnchorSand seed rebuild start..."
	awk -v start="$START" -v seed="$SEED" '
		index($0, start) {
			wipe = 1
			next
		}
		wipe && index($0, "print(\"*AnchorSand seed end*\\n\");") {
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
		index($0, "print(\"*AnchorSand seed end*\\n\");") {
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
	echo "AnchorSand seed rebuilt end"
	CC="gcc -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -nostdlib" #GCC compilation flags
	AC="AneoC/AneoC"                                                              #AneoC compilation flags
	#main ASM/AneoC/GCC build chunk
	echo "ASM Kernel/KEntry.ASM"
	nasm -f elf32 Kernel/KEntry.ASM -o KEntry.o
	echo "ASM Kernel/PortIO.ASM"
        nasm -f elf32 Kernel/PortIO.ASM -o PortIO.o
	echo "AC Kernel/Kernel.AC"
	$AC Kernel/Kernel.AC -o Kernel.o
	echo "AC Kernel/AnchorSand.AC"
	$AC Kernel/AnchorSand.AC -o AnchorSand.o
	echo "ASM Kernel/DiskThunk.ASM"
	nasm -f elf32 Kernel/DiskThunk.ASM -o DiskThunk.o
	echo "CC Kernel/FSSave.c"
        $CC -c Kernel/FSSave.c -o FSSave.o
	echo "AC Kernel/PIT.AC"
	$AC Kernel/PIT.AC -o PIT.o
	echo "AC Kernel/Keyboard.AC"
	$AC Kernel/Keyboard.AC -o Keyboard.o
	echo "AC Kernel/Keyboard.AC"
	$AC Kernel/Haltage.AC -o Haltage.o
	echo "AC Kernel/Startup.AC"
	$AC Kernel/Startup.AC -o Startup.o
	echo "ASM Kernel/Fault.ASM"
	nasm -f elf32 Kernel/Fault.ASM -o Fault.o
	echo "ASM Kernel/ISR.ASM"
	nasm -f elf32 Kernel/ISR.ASM -o ISR.o
	echo "AC Kernel/IDT.AC"
	$AC Kernel/IDT.AC -o IDT.o
	echo "AC Cmds/Addr.AC"
	$AC Cmds/Addr.AC -o Addr.o
	echo "AC Cmds/Help/Menu.AC"
	$AC Cmds/Help/Menu.AC -o HelpMenu.o
	echo "AC Cmds/Tune.AC"
	$AC Cmds/Tune.AC -o Tune.o
	echo "AC Cmds/Entropy.AC"
	$AC Cmds/Entropy.AC -o Entropy.o
	echo "AC Cmds/Convert.AC"
	$AC Cmds/Convert.AC -o Convert.o
	echo "LD *.o -> Kernel.ELF"
	#link main *.o
	ld -m elf_i386 -Ttext 0x10000 --section-start=.bss=0x100000 -e _start \
		KEntry.o \
		PortIO.o \
		Kernel.o \
		DiskThunk.o \
		AnchorSand.o \
		FSSave.o \
		PIT.o \
		Haltage.o \
		Keyboard.o \
		Startup.o \
		Fault.o \
		IDT.o \
		ISR.o \
		Addr.o \
		HelpMenu.o \
		Tune.o \
		Entropy.o \
		Convert.o \
		-o Kernel.ELF
	#BSS cals
	BSS_END_HEX=$(nm -n Kernel.ELF | awk '$3 == "_end" { print $1; exit }')
	if [ -z "$BSS_END_HEX" ]; then
		echo "ERROR: Could not find the kernel .bss end address."
		exit 1
	fi
	BSS_END=$((16#$BSS_END_HEX))
	if [ "$BSS_END" -ge $((0x1F0000)) ]; then
		echo "ERROR: Kernel .bss is too close to the 0x200000 stack."
		echo "ERROR: .bss ends at 0x$BSS_END_HEX."
		exit 1
	fi
	echo "OBJCOPY BIN Kernel.ELF -> Boot/KERNEL.BIN"
	objcopy -O binary Kernel.ELF Boot/KERNEL.BIN
	KERNEL_BYTES=$(stat -c %s Boot/KERNEL.BIN)
	KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))
	if [ "$KERNEL_SECTORS" -gt 2879 ]; then
		echo "ERROR:Kernel no longer fits in the floppy image."
		exit 1
	fi
	echo "Kernel size: $KERNEL_BYTES bytes ($KERNEL_SECTORS sectors)"
	sed -Ei "s/^KERNEL_SECTORS[[:space:]]+EQU[[:space:]]+[0-9]+/KERNEL_SECTORS EQU $KERNEL_SECTORS/" \
		Boot/Boot.ASM Boot/FIBoot.ASM
	#boot-loader ASM
	echo "ASM Boot/Boot.ASM"
	nasm -f bin Boot/Boot.ASM -o Boot/AEBOOT.BIN
	echo "ASM Boot/FIBoot.ASM"
	nasm -f bin Boot/FIBoot.ASM -o Boot/FIAEBOOT.BIN
	#cleanup
	echo "RM *.IMG *.ISO"
	touch Fallback.IMG
	touch Fallback.ISO
	rm *.IMG
	rm *.ISO
	#IMG build
	echo "DD Init Boot/AneoEngine.IMG"
	dd if=/dev/zero of=Boot/AneoEngine.IMG bs=512 count=2880
	echo "DD Boot/AEBOOT.BIN -> Boot/AneoEngine.IMG"
	dd if=Boot/AEBOOT.BIN of=Boot/AneoEngine.IMG conv=notrunc
	echo "DD Boot/KERNEL.BIN -> Boot/AneoEngine.IMG"
	dd if=Boot/KERNEL.BIN of=Boot/AneoEngine.IMG bs=512 seek=1 conv=notrunc
	#ISO build
	echo "GENISOIMAGE"
	genisoimage \
		-V "AneoEngine Media" \
		-o AneoEngine.ISO \
		-b AneoEngine.IMG \
		-c Boot.CAT \
		Boot
	#patch floppy boot-loader to ISO9660 (CD-ROM)
	echo "DD Boot/FIABOOT.BIN -> AneoEngine.ISO"
	dd if=Boot/FIAEBOOT.BIN of=AneoEngine.ISO bs=512 count=1 conv=notrunc
	#move Kernel ELF (for debugging and stuff)
	echo "MV Kernel.ELF -> Boot"
	mv Kernel.ELF Boot
	echo "RM *.o"
	rm *.o
	echo "\nAneoEngine build done\n"
}

###   start   ###
cd AneoC
echo "(AneoC/Compiler.c) CTC"
./CTC.sh
cd ..
echo "(Building image to calculate kernel LBA.)"
build
echo "(Finding kernel LBA.)"
FLOPPY_KERNEL_LBA=""
for LBA in $(seq 0 20000); do
	dd if=AneoEngine.ISO of=/tmp/kernel-test.bin \
		bs=512 skip=$LBA count=1 status=none
	if cmp -n 512 Boot/KERNEL.BIN /tmp/kernel-test.bin >/dev/null; then
		FLOPPY_KERNEL_LBA=$LBA
		break
	fi
done
if [ -z "$FLOPPY_KERNEL_LBA" ]; then
	echo "ERROR: Failed to locate KERNEL.BIN inside ISO!"
	exit 1
fi
echo "FLOPPY_KERNEL_LBA = $FLOPPY_KERNEL_LBA"
echo "Patching FIBoot.ASM"
sed -Ei "s/^FLOPPY_KERNEL_LBA[[:space:]]+EQU[[:space:]]+[0-9]+/FLOPPY_KERNEL_LBA EQU $FLOPPY_KERNEL_LBA/" Boot/FIBoot.ASM
echo "ASM Boot/FIBoot.ASM"
nasm -f bin Boot/FIBoot.ASM -o Boot/FIAEBOOT.BIN
dd if=Boot/FIAEBOOT.BIN of=AneoEngine.ISO \
	bs=512 count=1 conv=notrunc
echo "(Building final image.)"
build
RC1="qemu-system-x86_64 -cdrom AneoEngine.ISO -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0"
RC2="qemu-system-x86_64 -drive file=AneoEngine.ISO,format=raw -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0"
echo "========================"
echo "QEMU run commands:"
echo "$RC1 - use this if you want to boot with a CD-ROM."
echo "$RC2 - use this if you want to boot with a standard drive."
echo " Both commands work with ...-i386 aswell."
echo " "
if [ "$1" = "-RC1" ]; then
	echo "Running 'AneoEngine.ISO' QEMU CD-ROM"
	eval "$RC1"
elif [ "$1" = "-RC2" ]; then
	echo "Running 'AneoEngine.ISO' QEMU Drive"
	eval "$RC2"
fi

