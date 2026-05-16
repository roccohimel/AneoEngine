[org 0x7C00]
[bits 16]
KERNEL_OFFSET equ 0x1000
KERNEL_SECTORS equ 32
SECTORS_PER_TRACK equ 18
start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00
	sti
	mov [BOOT_DRIVE], dl
	mov ax, 0x0003       ;set 80x25
	int 0x10
	mov ax, 0x1112       ;load 8x8 fioeuffjwgfgregrg
	xor bl, bl           ;font block 0
	int 0x10
	mov si, msg_boot
	call print16
	call load_kernel
	mov si, msg_loaded
	call print16
	call switch_pm
	jmp $
print16:
	lodsb
	cmp al, 0
	je print16_done
	mov ah, 0x0E
	mov bh, 0
	mov bl, 0x1F
	int 0x10
	jmp print16
print16_done:
	ret
load_kernel:
	xor ax, ax
	mov es, ax
	mov bx, KERNEL_OFFSET
	mov ah, 0x00
	mov dl, [BOOT_DRIVE]
	int 0x13
	jc disk_error
	mov ah, 0x02
	mov al, KERNEL_SECTORS
	mov ch, 0
	mov cl, 2
	mov dh, 0
	mov dl, [BOOT_DRIVE]
	int 0x13
	jc disk_error
	ret
load_loop:
	push cx
	mov ah, 0x02
	mov al, 1
	mov ch, [track]
	mov cl, [sector]
	mov dh, [head]
	mov dl, [BOOT_DRIVE]
	int 0x13
	jc disk_error
	add bx, 512
	inc byte [sector]
	cmp byte [sector], SECTORS_PER_TRACK + 1
	jne load_next
	mov byte [sector], 1
	inc byte [head]
	cmp byte [head], 2
	jne load_next
	mov byte [head], 0
	inc byte [track]
load_next:
	pop cx
	loop load_loop
	ret
disk_error:
	mov si, msg_disk
	call print16
	jmp $
switch_pm:
	cli
	lgdt [gdt_descriptor]
	mov eax, cr0
	or eax, 1
	mov cr0, eax
	jmp CODE_SEG:init_pm
[bits 32]
init_pm:
	mov ax, DATA_SEG
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, 0x90000
	jmp KERNEL_OFFSET
halt:
	cli
	hlt
	jmp halt
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
sector db 0
head db 0
track db 0
msg_boot   db "AneoEngine Boot Loader", 13, 10, 0
msg_loaded db "Kernel found..", 13, 10, 0
msg_disk   db "Kernel read error!", 13, 10, 0
times 510 - ($ - $$) db 0
dw 0xAA55
