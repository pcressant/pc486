; SPDX-License-Identifier: GPL-3.0-or-later
; Sonde invitée : mode VGA 13h, retour au texte après une touche.
.8086
.model tiny
.code
org 100h
start:
    mov ax,13h
    int 10h
    xor ax,ax
    int 16h
    mov ax,3
    int 10h
    mov dx,offset done
    mov ah,9
    int 21h
    mov ax,4c00h
    int 21h
done db 'RETOUR TEXTE',13,10,'$'
end start
