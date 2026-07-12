BITS 32

GLOBAL outb
GLOBAL inb
GLOBAL outw
GLOBAL inw
GLOBAL outl
GLOBAL inl

SECTION .text

outb:
    push ebp
    mov ebp, esp
    mov dx, [ebp + 8]
    mov al, [ebp + 12]
    out dx, al
    pop ebp
    ret

inb:
    push ebp
    mov ebp, esp
    xor eax, eax
    mov dx, [ebp + 8]
    in al, dx
    pop ebp
    ret

outw:
    push ebp
    mov ebp, esp
    mov dx, [ebp + 8]
    mov ax, [ebp + 12]
    out dx, ax
    pop ebp
    ret

inw:
    push ebp
    mov ebp, esp
    xor eax, eax
    mov dx, [ebp + 8]
    in ax, dx
    pop ebp
    ret

outl:
    push ebp
    mov ebp, esp
    mov dx, [ebp + 8]
    mov eax, [ebp + 12]
    out dx, eax
    pop ebp
    ret

inl:
    push ebp
    mov ebp, esp
    mov dx, [ebp + 8]
    in eax, dx
    pop ebp
    ret
