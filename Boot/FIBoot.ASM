[org 0x7C00]
[bits 16]

KERNEL_OFFSET  equ 0x10000
KERNEL_SEGMENT equ 0x1000
KERNEL_SECTORS equ 128

FLOPPY_KERNEL_LBA equ 241

start:
	cli
	cld
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00
	sti

	mov [BOOT_DRIVE], dl

	mov ax, 0x0003
	int 0x10

	mov ax, 0x1112
	xor bl, bl
	int 0x10

	mov si, msg_boot
	call print16
	mov si, msg_type
        call print16

	call enable_a20
	call load_kernel

	mov si, msg_loaded
	call print16

	mov si, msg_prompt
	call print16

	xor ax, ax
	int 0x16

	cli
	lgdt [gdt_descriptor]

	mov eax, cr0
	or eax, 1
	mov cr0, eax

	jmp dword CODE_SEG:init_pm

enable_a20:
	in al, 0x92
	or al, 00000010b
	and al, 11111110b
	out 0x92, al
	ret

print16:
	lodsb
	test al, al
	jz .done
	mov ah, 0x0E
	mov bh, 0
	mov bl, 0x1F
	int 0x10
	jmp print16
.done:
	ret

load_kernel:
	xor ax, ax
	mov dl, [BOOT_DRIVE]
	int 0x13

	push cs
	pop ds

	mov word [dap_count], KERNEL_SECTORS
	mov word [dap_offset], 0x0000
	mov word [dap_segment], KERNEL_SEGMENT
	mov dword [dap_lba_low], FLOPPY_KERNEL_LBA
	mov dword [dap_lba_high], 0

	mov ah, 0x42
	mov dl, [BOOT_DRIVE]
	mov si, dap
	int 0x13
	jc disk_error

	ret

.loop:
	push cx

	mov ah, 0x42
	mov dl, [BOOT_DRIVE]
	mov si, dap
	int 0x13
	jc disk_error

	add word [dap_offset], 512
	inc dword [dap_lba_low]

	pop cx
	loop .loop

	ret

disk_error:
	mov si, msg_disk
	call print16
	jmp $

dap:
	db 0x10
	db 0x00
dap_count:
	dw 0
dap_offset:
	dw 0
dap_segment:
	dw 0
dap_lba_low:
	dd 0
dap_lba_high:
	dd 0

[bits 32]

init_pm:
	cld

	mov ax, DATA_SEG
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	mov esp, 0x90000

	jmp CODE_SEG:KERNEL_OFFSET

gdt_start:
	dq 0

gdt_code:
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10011010b
	db 11001111b
	db 0x00

gdt_data:
	dw 0xFFFF
	dw 0x0000
	db 0x00
	db 10010010b
	db 11001111b
	db 0x00

gdt_end:

gdt_descriptor:
	dw gdt_end - gdt_start - 1
	dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE db 0

msg_boot   db 13,10,13,10,13,10,13,10,"AneoEngine Boot Loader",13,10,0
msg_type   db "Reading kernel from hard disk/solid state media...",13,10,0
msg_loaded db "Kernel found...",13,10,0
msg_prompt db 13,10,"Press any key to continue...",13,10,0
msg_disk   db "Kernel read error!",13,10,0

times 510 - ($ - $$) db 0
dw 0xAA55
