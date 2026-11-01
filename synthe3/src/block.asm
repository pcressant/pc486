; SPDX-License-Identifier: GPL-3.0-or-later
; Noyaux par période (option S3_BLOCK), 386 en mode réel 16 bits, modèle large,
; convention C. Mêmes résultats que block.c, s3_weighted et s3_output : les
; indices, bornes et arrondis (division signée vers zéro) sont conservés.
.model large, c
.386
.data
public s3_block_impl
s3_block_impl db "asm", 0
.code
public s3_mix, s3_output_block

; void s3_mix(short *mix, const char *segment, unsigned source_period,
;             unsigned result_period, unsigned amplitude, unsigned rising,
;             const unsigned short *weights)
; [bp+6] mix, [bp+10] segment, [bp+14] source, [bp+16] result, [bp+18] amplitude,
; [bp+20] rising, [bp+22] weights. Locaux : [bp-4] amplitude 32 bits,
; [bp-6] compteur, [bp-8] décalage de fenêtre (shift).
s3_mix proc far
    push bp
    mov bp, sp
    sub sp, 8
    push ds
    push es
    push fs
    push ebx
    push ecx
    push edx
    push esi
    push edi
    mov ax, word ptr [bp+18]
    test ax, ax
    jz near ptr mix_done
    movzx eax, ax
    mov dword ptr [bp-4], eax
    mov cx, word ptr [bp+14]        ; période source
    test cx, cx
    jz near ptr mix_done
    mov dx, word ptr [bp+16]        ; période résultante
    test dx, dx
    jz near ptr mix_done
    mov ax, dx                      ; length = min(result, source)
    cmp ax, cx
    jbe mix_length
    mov ax, cx
mix_length:
    xor si, si                      ; shift = 0
    cmp dx, cx
    jbe mix_shift
    cmp word ptr [bp+20], 0
    je mix_shift
    mov si, cx
    sub si, dx                      ; source - result < 0
mix_shift:
    mov word ptr [bp-8], si
    xor di, di                      ; first = max(0, -shift)
    test si, si
    jns mix_first
    mov di, si
    neg di
mix_first:
    mov bx, ax                      ; last = min(length - shift, result)
    sub bx, si
    cmp bx, dx
    jbe mix_last
    mov bx, dx
mix_last:
    sub bx, di                      ; compteur = last - first
    jle near ptr mix_done
    mov word ptr [bp-6], bx
    mov ax, cx                      ; at = first + source/4 (+ source - result si montant)
    shr ax, 2
    add ax, di
    cmp word ptr [bp+20], 0
    je mix_at
    add ax, cx
    sub ax, dx
mix_at:
    cmp ax, cx
    jb mix_at_ok
    sub ax, cx
mix_at_ok:
    add si, di                      ; indice de fenêtre = first + shift
    shl si, 1
    add si, word ptr [bp+22]
    les bx, dword ptr [bp+10]
    mov dx, bx
    add dx, cx                      ; fin du segment (offset)
    add bx, ax                      ; segment + at
    shl di, 1
    add di, word ptr [bp+6]         ; mix + first
    mov fs, word ptr [bp+8]
    mov ds, word ptr [bp+24]        ; segment des poids ; [bp+n] reste sur SS
    cmp word ptr [bp+20], 0
    je mix_fall
mix_rise:
    movzx ecx, word ptr [si]        ; poids Q15
    movsx eax, byte ptr es:[bx]     ; échantillon du diphone
    imul eax, dword ptr [bp-4]      ; × amplitude
    imul eax, ecx                   ; × poids
    test eax, eax
    jns mix_rise_shift
    add eax, 32767                  ; division signée vers zéro
mix_rise_shift:
    sar eax, 15
    add word ptr fs:[di], ax
    add si, 2
    add di, 2
    inc bx
    cmp bx, dx
    jb mix_rise_next
    sub bx, word ptr [bp+14]
mix_rise_next:
    dec word ptr [bp-6]
    jnz near ptr mix_rise
    jmp mix_done
mix_fall:
    mov ecx, 32768
    movzx eax, word ptr [si]
    sub ecx, eax                    ; 32768 - poids
    movsx eax, byte ptr es:[bx]
    imul eax, dword ptr [bp-4]
    imul eax, ecx
    test eax, eax
    jns mix_fall_shift
    add eax, 32767
mix_fall_shift:
    sar eax, 15
    add word ptr fs:[di], ax
    add si, 2
    add di, 2
    inc bx
    cmp bx, dx
    jb mix_fall_next
    sub bx, word ptr [bp+14]
mix_fall_next:
    dec word ptr [bp-6]
    jnz near ptr mix_fall
mix_done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop fs
    pop es
    pop ds
    mov sp, bp
    pop bp
    ret
s3_mix endp

; void s3_output_block(const short *mix, short *out, unsigned count, long *state)
; [bp+6] mix, [bp+10] out, [bp+14] count, [bp+16] state. Gain ×8, filtre
; historique Q16 à produit 64 bits et écrêtage ±32766, comme s3_output.
hp_k dd 16471
s3_output_block proc far
    push bp
    mov bp, sp
    push ds
    push es
    push fs
    push ebx
    push ecx
    push edx
    push esi
    push edi
    cmp word ptr [bp+14], 0
    je out_done
    lds si, dword ptr [bp+6]
    les di, dword ptr [bp+10]
    lfs bx, dword ptr [bp+16]
out_next:
    movsx ecx, word ptr [si]
    shl ecx, 3
    add ecx, 32768
    jns out_input
    inc ecx                         ; arrondi historique vers zéro après +0,5
out_input:
    mov eax, dword ptr fs:[bx+4]    ; état 1
    sub eax, 32768
    imul dword ptr cs:hp_k          ; EDX:EAX = (s1 - 32768) × 16471
    test edx, edx
    jns out_shift
    add eax, 65535
    adc edx, 0
out_shift:
    shrd eax, edx, 16
    mov edx, ecx                    ; sortie = entrée - s0 + s1 - terme
    sub edx, dword ptr fs:[bx]
    add edx, dword ptr fs:[bx+4]
    sub edx, eax
    mov dword ptr fs:[bx], ecx
    mov dword ptr fs:[bx+4], edx
    sub edx, 32768
    cmp edx, 32766
    jle out_low
    mov edx, 32766
out_low:
    cmp edx, -32766
    jge out_store
    mov edx, -32766
out_store:
    mov word ptr es:[di], dx
    add si, 2
    add di, 2
    dec word ptr [bp+14]
    jnz out_next
out_done:
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    pop fs
    pop es
    pop ds
    pop bp
    ret
s3_output_block endp
end
