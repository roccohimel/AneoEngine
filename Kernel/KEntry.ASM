;Kernel entry point
[bits 32]

BSS_START EQU 0x100000
STACK_TOP EQU 0x200000

global _start
extern kmain
extern _end

_start:
	cld

	; The raw kernel file does not contain .bss. Clear the entire
	; high-memory .bss area before any C/AneoC globals are used.
	mov edi, BSS_START
	mov ecx, _end
	cmp ecx, edi
	jbe .bss_ready
	sub ecx, edi
	xor eax, eax
	rep stosb

.bss_ready:
	mov esp, STACK_TOP
	call kmain ;run the kernel

halt:
	cli
	hlt
	jmp halt
