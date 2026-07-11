[org 0x7C00]
[bits 16]

KERNEL_OFFSET  EQU 0x10000
KERNEL_SEGMENT EQU 0x1000
KERNEL_SECTORS EQU 384

FLOPPY_KERNEL_LBA EQU 105

start:
	CLI
	CLD
	XOR AX, AX
	MOV DS, AX
	MOV ES, AX
	MOV SS, AX
	MOV SP, 0x7C00
	STI

	MOV [BOOT_DRIVE], DL
	MOV [0x0500], DL

	MOV AX, 0x0003
	INT 0x10

	MOV AX, 0x1112
	XOR BL, BL
	INT 0x10

	MOV SI, msg_boot
	CALL print16

	MOV SI, msg_type
	CALL print16

	CALL enable_a20
	CALL load_kernel

	MOV SI, msg_loaded
	CALL print16

	MOV SI, msg_prompt
	CALL print16

	XOR AX, AX
	INT 0x16

	CLI
	LGDT [GDT_DESCRIPTOR]

	CALL copy_font8x8
	MOV AX, 0x0012
	INT 0x10

	MOV EAX, CR0
	OR EAX, 1
	MOV CR0, EAX

	JMP DWORD CODE_SEG:init_pm

enable_a20:
	IN AL, 0x92
	OR AL, 00000010B
	AND AL, 11111110B
	OUT 0x92, AL
	RET

print16:
	LODSB
	TEST AL, AL
	JZ .done

	MOV AH, 0x0E
	MOV BH, 0
	MOV BL, 0x1F
	INT 0x10
	JMP print16

.done:
	RET

copy_font8x8:
	MOV AX, 0x1130
	MOV BH, 0x03
	INT 0x10
	PUSH ES
	POP DS
	MOV SI, BP
	XOR AX, AX
	MOV ES, AX
	MOV DI, 0x7000
	MOV CX, 2048
	CLD
	REP MOVSB
	RET

load_kernel:
	XOR AX, AX
	MOV DL, [BOOT_DRIVE]
	INT 0x13
	JC disk_error

	MOV WORD [DAP_COUNT], 1
	MOV WORD [DAP_OFFSET], 0x0000
	MOV WORD [DAP_SEGMENT], KERNEL_SEGMENT

	MOV DWORD [DAP_LBA_LOW], FLOPPY_KERNEL_LBA
	MOV DWORD [DAP_LBA_HIGH], 0

	MOV WORD [SECTORS_LEFT], KERNEL_SECTORS

.loop:
	CMP WORD [SECTORS_LEFT], 0
	JE .done

	; Some BIOSes modify the transferred-sector count,
	; so restore it before every read.
	MOV WORD [DAP_COUNT], 1

	MOV AH, 0x42
	MOV DL, [BOOT_DRIVE]
	MOV SI, DAP
	INT 0x13
	JC disk_error

	; Advance the destination by one 512-byte sector.
	ADD WORD [DAP_OFFSET], 512
	JNC .no_segment_wrap

	; Offset wrapped after 64 KiB.
	; Advance the segment by 0x1000 paragraphs.
	MOV WORD [DAP_OFFSET], 0
	ADD WORD [DAP_SEGMENT], 0x1000

.no_segment_wrap:
	ADD DWORD [DAP_LBA_LOW], 1
	ADC DWORD [DAP_LBA_HIGH], 0

	DEC WORD [SECTORS_LEFT]
	JMP .loop

.done:
	RET

disk_error:
	MOV SI, msg_disk
	CALL print16
	JMP $

DAP:
	DB 0x10
	DB 0x00

DAP_COUNT:
	DW 0

DAP_OFFSET:
	DW 0

DAP_SEGMENT:
	DW 0

DAP_LBA_LOW:
	DD 0

DAP_LBA_HIGH:
	DD 0

SECTORS_LEFT:
	DW 0

[bits 32]

init_pm:
	CLD

	MOV AX, DATA_SEG
	MOV DS, AX
	MOV ES, AX
	MOV FS, AX
	MOV GS, AX
	MOV SS, AX

	MOV ESP, 0x90000

	JMP CODE_SEG:KERNEL_OFFSET

GDT_START:
	DQ 0

GDT_CODE:
	DW 0xFFFF
	DW 0x0000
	DB 0x00
	DB 10011010B
	DB 11001111B
	DB 0x00

GDT_DATA:
	DW 0xFFFF
	DW 0x0000
	DB 0x00
	DB 10010010B
	DB 11001111B
	DB 0x00

GDT_END:

GDT_DESCRIPTOR:
	DW GDT_END - GDT_START - 1
	DD GDT_START

CODE_SEG EQU GDT_CODE - GDT_START
DATA_SEG EQU GDT_DATA - GDT_START

BOOT_DRIVE:
	DB 0

msg_boot:
	DB 13,10,13,10,13,10,13,10
	DB "AneoEngine Boot Loader",13,10,0

msg_type:
	DB "Reading kernel from hard disk/solid state media...",13,10,0

msg_loaded:
	DB "Kernel found...",13,10,0

msg_prompt:
	DB 13,10,"Press any key to continue...",13,10,0

msg_disk:
	DB "Kernel read error!",13,10,0

TIMES 510 - ($ - $$) DB 0
DW 0xAA55
