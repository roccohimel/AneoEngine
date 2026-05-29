[bits 32]

extern exception_handler

global idt_load

global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

idt_load:
	mov eax, [esp + 4]
	lidt [eax]
	ret

%macro ISR_NOERR 1
isr%1:
	cli
	push dword 0
	push dword %1
	jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
	cli
	push dword %1
	jmp isr_common
%endmacro

isr_common:
	pusha

	push ds
	push es
	push fs
	push gs

	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	push esp
	call exception_handler
	add esp, 4

	pop gs
	pop fs
	pop es
	pop ds

	popa

	add esp, 8

	iret

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7

ISR_ERR 8

ISR_NOERR 9

ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14

ISR_NOERR 15
ISR_NOERR 16

ISR_ERR 17

ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20

ISR_ERR 21

ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28

ISR_ERR 29
ISR_ERR 30

ISR_NOERR 31
