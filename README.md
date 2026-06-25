# The AneoEngine Operating System

A small, hand-written **32-bit x86 operating system** that boots from BIOS,
enters protected mode, and gives you a text-mode shell with its own
filesystem. Built from scratch in freestanding C and NASM — no libc, no
standard library. Read the [LICENSE](LICENSE) before redistributing.

## What it is

AneoEngine is a **single-task, ring-0 kernel** (~4,800 lines) with:

- A two-stage **BIOS bootloader** (CHS for CD/floppy, LBA for HDD/USB)
  that enables A20, sets up a GDT, and drops into 32-bit protected mode.
- An **IDT + ISR crash handler** that reports CPU exceptions
  (divide-by-zero, GPF, page fault, …) with a full register dump.
- A custom in-RAM **filesystem called AnchorSand**: a tree of up to 64
  nodes with 4 KB files, `ls`/`cd`/`cat`/`edit`/`cp`/`mv`/`rm`, and a
  built-in full-screen text editor.
- A **shell that is also a scripting language** — `run <file>` executes
  stored programs (the bundled "Jeopardy" file plays a song).
- **Disk persistence** via a 32-bit ↔ real-mode BIOS thunk
  (`INT 13h` LBA read/write). AnchorSand does **not** use FAT12/FAT32.

## Requirements

- Any **32-bit x86 CPU**, or a **64-bit CPU with Legacy BIOS boot**
  (no UEFI-only). **ARM-64 and RISC-V are not supported.**
- **512 MB RAM** (hardcoded in the kernel).
- Boots from **CD/DVD, USB, or a VM.** Use writable media (USB/HDD) for
  file persistence — a read-only CD-ROM gives a fresh filesystem each boot.

## Running

```bash
# Recommended: QEMU (AneoEngine is a live image, nothing is installed)
qemu-system-x86_64 -cdrom AneoEngine.ISO
# or:
qemu-system-x86_64 -drive file=AneoEngine.ISO,format=raw
```

Or burn `AneoEngine.ISO` to a CD/DVD, or flash it to a USB drive.

## Building

Requires `nasm`, `gcc` (with `-m32`), `ld` (`elf_i386`), `genisoimage`,
`dd`, `awk`, and `sed`.

```bash
./build.sh              # builds AneoEngine.ISO
./build.sh -RC1         # build, then boot via QEMU (-cdrom)
```

The `Root/` folder is the default filesystem. At build time, `build.sh`
scans it and compiles every file into the kernel as AnchorSand commands,
so they exist in RAM on first boot.

## Learn more

- Press **F1** in the shell for the help menu.
- Run **`addr`** to see the kernel's memory map.
- See `/Help/` and `/Docs/FAQ.TXT` inside the OS.

Creator: Rocco Himel — https://roccohimel.github.io/AneoEngine
