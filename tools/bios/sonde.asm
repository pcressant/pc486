; Sonde d'amorçage pour essayer un BIOS (scripts/bios-86box, docs/bios-86box.md).
; Écrit sur COM1 (9600 8N1, INT 14h) : « N<nb disques BIOS> <C:> <D:> Z », chaque disque
; en « cylindre max/tête max/secteurs » (hex, INT 13h AH=08h), « E » si absent.
; nasm -f bin -o sonde.bin sonde.asm
bits 16
org 7C00h
start:  cli
        xor ax, ax
        mov ds, ax
        mov ss, ax
        mov sp, 7C00h
        sti
        mov ax, 00E3h       ; COM1 9600 8N1
        xor dx, dx
        int 14h
        mov al, 'N'
        call putc
        mov al, [0475h]     ; nombre de disques durs vus par le BIOS
        call hex
        mov dl, 80h
        call geo
        mov dl, 81h
        call geo
        mov al, 'Z'
        call putc
        mov al, 13
        call putc
.halt:  hlt
        jmp .halt
geo:    mov al, ' '
        call putc
        mov ah, 08h
        push dx
        int 13h
        jc .err
        push dx
        mov al, ch          ; cylindre max (8 bits bas)
        push cx
        mov al, cl
        shr al, 6
        call hex            ; bits hauts du cylindre
        pop cx
        mov al, ch
        push cx
        call hex
        mov al, '/'
        call putc
        pop cx
        pop dx
        mov al, dh          ; tête max
        call hex
        mov al, '/'
        call putc
        mov al, cl
        and al, 3Fh         ; secteurs
        call hex
        pop dx
        ret
.err:   mov al, 'E'
        call putc
        pop dx
        ret
hex:    push ax
        shr al, 4
        call nib
        pop ax
nib:    and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe putc
        add al, 7
putc:   push dx
        mov ah, 01h
        xor dx, dx
        int 14h
        pop dx
        ret
        times 510-($-$$) db 0
        dw 0AA55h
