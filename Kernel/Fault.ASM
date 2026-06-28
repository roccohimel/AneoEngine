[bits 32]

global triple_fault

triple_fault:
        cli

        lidt [bad_idt]

        int 0x03

.hang:
        hlt
        jmp .hang

bad_idt:
        dw 0
        dd 0
