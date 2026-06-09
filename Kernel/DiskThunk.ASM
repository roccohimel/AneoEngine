[BITS 32]

global bios_disk_op

THUNK_BASE equ 0x8000
DAP_ADDR   equ 0x0600
BUF_ADDR   equ 0x7000

PM16_LOW   equ THUNK_BASE + (pm16_entry - thunk_start)
REAL_LOW   equ THUNK_BASE + (real_entry - thunk_start)
PM32_LOW   equ THUNK_BASE + (pm32_entry - thunk_start)

DRIVE_LOW  equ THUNK_BASE + (bios_drive - thunk_start)
WRITE_LOW  equ THUNK_BASE + (bios_write - thunk_start)
RESULT_LOW equ THUNK_BASE + (bios_result - thunk_start)
ESP_LOW    equ THUNK_BASE + (saved_esp - thunk_start)
IDT_LOW    equ THUNK_BASE + (saved_idt - thunk_start)
RMIDT_LOW  equ THUNK_BASE + (real_idt - thunk_start)

bios_disk_op:
	cli

	mov [saved_esp], esp
	sidt [saved_idt]

	mov esi, thunk_start
	mov edi, THUNK_BASE
	mov ecx, thunk_end - thunk_start
	cld
	rep movsb

	mov byte  [DAP_ADDR + 0], 16
	mov byte  [DAP_ADDR + 1], 0
	mov word  [DAP_ADDR + 2], 1
	mov word  [DAP_ADDR + 4], BUF_ADDR
	mov word  [DAP_ADDR + 6], 0

	mov eax, [esp + 8]
	mov dword [DAP_ADDR + 8], eax
	mov dword [DAP_ADDR + 12], 0

	mov al, [esp + 4]
	mov [DRIVE_LOW], al

	mov eax, [esp + 12]
	mov [WRITE_LOW], eax

	mov dword [RESULT_LOW], 0

	mov eax, [saved_esp]
	mov [ESP_LOW], eax

	lgdt [gdt_ptr]

	jmp 0x18:PM16_LOW

thunk_start:

[BITS 16]

pm16_entry:
	mov ax, 0x20
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov sp, 0x5000

	mov eax, cr0
	and eax, 0xFFFFFFFE
	mov cr0, eax

	jmp 0x0000:REAL_LOW

real_entry:
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov sp, 0x5000

	lidt [RMIDT_LOW]

	mov dl, [DRIVE_LOW]
	mov si, DAP_ADDR

	cmp dword [WRITE_LOW], 0
	je .read

.write:
	mov ah, 0x43
	jmp .go

.read:
	mov ah, 0x42

.go:
	int 0x13
	jc .fail

	mov dword [RESULT_LOW], 1
	jmp .back

.fail:
	mov dword [RESULT_LOW], 0

.back:
	cli

	mov eax, cr0
	or eax, 1
	mov cr0, eax

	jmp 0x08:PM32_LOW

[BITS 32]

pm32_entry:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	lidt [IDT_LOW]

	mov esp, [ESP_LOW]
	mov eax, [RESULT_LOW]

	ret

bios_drive db 0
bios_write dd 0
bios_result dd 0
saved_esp dd 0

saved_idt:
	dw 0
	dd 0

real_idt:
	dw 0x03FF
	dd 0x00000000

thunk_end:

align 8

gdt:
	dq 0

	; 0x08 32-bit code
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10011010b
	db 11001111b
	db 0x00

	; 0x10 32-bit data
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10010010b
	db 11001111b
	db 0x00

	; 0x18 16-bit code
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10011010b
	db 00001111b
	db 0x00

	; 0x20 16-bit data
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10010010b
	db 00001111b
	db 0x00

gdt_end:

gdt_ptr:
	dw gdt_end - gdt - 1
	dd gdt
