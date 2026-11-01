; SPDX-License-Identifier: GPL-3.0-or-later
; Fin du batch de mesure 86Box : COM1 vers le journal privé, aucun Apollo.
.model tiny
.code
org 100h
start:
    mov dx, 3fbh
    mov al, 80h
    out dx, al
    mov dx, 3f8h
    mov al, 12
    out dx, al
    inc dx
    xor al, al
    out dx, al
    mov dx, 3fbh
    mov al, 3
    out dx, al
    mov si, offset message
next:
    lodsb
    test al, al
    jz done
    mov bl, al
    mov cx, 65535
    mov dx, 3fdh
wait_tx:
    in al, dx
    test al, 20h
    jnz write_tx
    loop wait_tx
    jmp done
write_tx:
    mov dx, 3f8h
    mov al, bl
    out dx, al
    jmp next
done:
    mov ax, 4c00h
    int 21h
message db 13,10,'S3_BOX_FIN',13,10,0
end start
