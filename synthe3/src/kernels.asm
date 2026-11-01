; SPDX-License-Identifier: GPL-3.0-or-later
; Noyaux 386 en mode réel 16 bits, modèle large Open Watcom, convention C.
; Fenêtre cosinus Q15 interpolée, multiplications 32 bits et filtre Q16.
.model large, c
.386
.code
public s3_window, s3_weighted, s3_highpass, s3_output, s3_unsigned8, s3_dma8

s3_window proc far
    push bp
    mov bp, sp
    sub sp, 4
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push es
    xor eax, eax
    cmp word ptr [bp+16], 0     ; amplitude
    je win_done
    movzx ecx, word ptr [bp+12] ; période source
    test ecx, ecx
    jz win_done
    movzx ebx, word ptr [bp+14] ; période résultante
    test ebx, ebx
    jz win_done
    movsx esi, word ptr [bp+6]  ; x
    mov edi, ecx
    shr edi, 2
    add edi, esi
    cmp word ptr [bp+18], 0
    je win_index
    add edi, ecx
    sub edi, ebx
win_index:
    cmp edi, ecx
    jl win_length
    sub edi, ecx
win_length:
    cmp ebx, ecx
    jbe win_short
    cmp word ptr [bp+18], 0
    je win_local
    add esi, ecx
    sub esi, ebx
    jmp win_local
win_short:
    mov ecx, ebx
win_local:
    test esi, esi
    js win_zero
    cmp esi, ecx
    jae win_zero
    les bx, dword ptr [bp+8]
    add bx, di
    movsx edi, byte ptr es:[bx]
    movzx eax, word ptr [bp+16]
    imul edi, eax
    mov dword ptr [bp-4], edi
    mov eax, esi
    shl eax, 16
    xor edx, edx
    div ecx                    ; phase Q16
    mov edx, eax
    and edx, 255
    shr eax, 8
    shl ax, 1
    mov si, ax
    movzx ecx, word ptr cs:window_q15[si]
    movzx ebx, word ptr cs:window_q15[si+2]
    sub ebx, ecx
    imul ebx, edx
    shr ebx, 8
    add ecx, ebx
    cmp word ptr [bp+18], 0
    jne win_scale
    neg ecx
    add ecx, 32768
win_scale:
    imul ecx, dword ptr [bp-4]
    mov eax, ecx
    test eax, eax
    jns win_shift
    add eax, 32767             ; division signée vers zéro
win_shift:
    sar eax, 15
    jmp win_done
win_zero:
    xor eax, eax
win_done:
    pop es
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    mov sp, bp
    pop bp
    ret
s3_window endp

; Même fenêtre, phase/interpolation déjà préparées pour cette longueur.
; Séparé du noyau scalaire, conservé comme oracle et pour les mesures A/B.
s3_weighted proc far
    push bp
    mov bp, sp
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push es
    push ds
    xor eax, eax
    cmp word ptr [bp+16], 0
    je weighted_done
    movzx ecx, word ptr [bp+12]
    test ecx, ecx
    jz weighted_done
    movzx ebx, word ptr [bp+14]
    test ebx, ebx
    jz weighted_done
    movsx esi, word ptr [bp+6]
    mov edi, ecx
    shr edi, 2
    add edi, esi
    cmp word ptr [bp+18], 0
    je weighted_index
    add edi, ecx
    sub edi, ebx
weighted_index:
    cmp edi, ecx
    jl weighted_length
    sub edi, ecx
weighted_length:
    cmp ebx, ecx
    jbe weighted_short
    cmp word ptr [bp+18], 0
    je weighted_local
    add esi, ecx
    sub esi, ebx
    jmp weighted_local
weighted_short:
    mov ecx, ebx
weighted_local:
    test esi, esi
    js weighted_zero
    cmp esi, ecx
    jae weighted_zero
    les bx, dword ptr [bp+8]
    add bx, di
    movsx edi, byte ptr es:[bx]
    movzx eax, word ptr [bp+16]
    imul edi, eax
    lds bx, dword ptr [bp+20]
    shl si, 1
    movzx ecx, word ptr [bx+si]
    cmp word ptr [bp+18], 0
    jne weighted_scale
    neg ecx
    add ecx, 32768
weighted_scale:
    imul ecx, edi
    mov eax, ecx
    test eax, eax
    jns weighted_shift
    add eax, 32767
weighted_shift:
    sar eax, 15
    jmp weighted_done
weighted_zero:
    xor eax, eax
weighted_done:
    pop ds
    pop es
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop bp
    ret
s3_weighted endp

s3_highpass proc far
    push bp
    mov bp, sp
    push ebx
    push ecx
    push esi
    push edi
    push es
    les si, dword ptr [bp+10]
    mov ecx, dword ptr [bp+6]
    call hp_filter
    mov edx, eax
    shr edx, 16                ; retour long DX:AX
    pop es
    pop edi
    pop esi
    pop ecx
    pop ebx
    pop bp
    ret
s3_highpass endp

; Gain, filtre et écrêtage : aucune opération flottante par échantillon.
s3_output proc far
    push bp
    mov bp, sp
    push ebx
    push ecx
    push esi
    push edi
    push es
    les si, dword ptr [bp+8]
    movsx ecx, word ptr [bp+6]
    shl ecx, 3
    add ecx, 32768
    jns output_input
    inc ecx                    ; arrondi historique vers zéro après ajout de 0,5
output_input:
    call hp_filter
    sub eax, 32768
    cmp eax, 32766
    jle output_low
    mov eax, 32766
output_low:
    cmp eax, -32766
    jge output_done
    mov eax, -32766
output_done:
    pop es
    pop edi
    pop esi
    pop ecx
    pop ebx
    pop bp
    ret
s3_output endp

; Entrée ECX, état ES:SI ; sortie EAX. Produit signé 64 bits EDX:EAX,
; pour conserver la précision même sur une transition de forte amplitude.
hp_filter proc near
    mov eax, dword ptr es:[si+4]
    mov ebx, eax
    sub eax, 32768
    mov edi, 16471
    imul edi
    test edx, edx
    jns hp_shift
    add eax, 65535
    adc edx, 0
hp_shift:
    shrd eax, edx, 16
    sub ebx, eax
    add ebx, ecx
    sub ebx, dword ptr es:[si]
    mov dword ptr es:[si], ecx
    mov dword ptr es:[si+4], ebx
    mov eax, ebx
    ret
hp_filter endp

; PCM signé 16 -> PCM non signé 8 bits, sans modifier le PCM source.
; DS doit être conservé : les pointeurs C sont far, DS reste celui du programme.
s3_unsigned8 proc far
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push cx
    lds si, dword ptr [bp+6]
    les di, dword ptr [bp+10]
    mov cx, word ptr [bp+14]
    jcxz u8_done
    cld
u8_next:
    lodsw
    mov al, ah
    xor al, 80h
    stosb
    loop u8_next
u8_done:
    pop cx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret
s3_unsigned8 endp

; 8237, émission 8 bits, canal 0, 1 ou 3 validé par l'appelant.
; Quatrième argument : 0 simple, 1 anneau auto-initialisé.
; L'adresse physique et la taille ont été bornées pour ne pas franchir 64 Kio.
s3_dma8 proc far
    push bp
    mov bp, sp
    push ax
    push bx
    push cx
    push dx
    pushf
    cli
    mov bx, word ptr [bp+6]
    mov al, bl
    or al, 4
    out 0ah, al                 ; masquer ce canal seulement
    xor al, al
    out 0ch, al                 ; première moitié des registres 16 bits
    mov al, bl
    or al, 48h
    cmp word ptr [bp+14], 0
    je dma_mode
    or al, 10h
dma_mode:
    out 0bh, al                 ; lecture mémoire -> périphérique
    mov dx, bx
    shl dx, 1
    mov ax, word ptr [bp+8]
    out dx, al
    mov al, ah
    out dx, al
    inc dx
    mov ax, word ptr [bp+12]
    dec ax
    out dx, al
    mov al, ah
    out dx, al
    mov dx, 83h
    cmp bx, 1
    je dma_page
    mov dx, 87h                ; canal 0 : page 87h
    cmp bx, 0
    je dma_page
    mov dx, 82h                ; canal 3 : page 82h
dma_page:
    mov al, byte ptr [bp+10]
    out dx, al
    mov al, bl
    out 0ah, al
    popf
    pop dx
    pop cx
    pop bx
    pop ax
    pop bp
    ret
s3_dma8 endp

include window.inc
end
