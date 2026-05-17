[bits 32]

global _start
extern kmain

_start:
	mov esp, 0x90000
	call kmain

halt:
	cli
	hlt
	jmp halt
