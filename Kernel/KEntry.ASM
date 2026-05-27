;Kernel entry point
[bits 32]

global _start
extern kmain

_start:
	cld
	mov esp, 0x90000
	call kmain ;run the kernel

halt:
	cli
	hlt
	jmp halt
