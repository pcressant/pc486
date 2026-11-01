; SPDX-License-Identifier: GPL-3.0-or-later
; Horloge de banc : BIOS + PIT 8254, sans changer le canal 0.
.model large, c
.386
.data
public s3_clock_fault
s3_clock_fault dw 0
.code
public s3_ticks, s3_clock_init

s3_clock_init proc far
    pushf
    cli
    mov al, 0e2h                  ; latch status seul, canal 0
    out 43h, al
    in al, 40h
    mov ah, al
    and ah, 31h                  ; accès LSB/MSB, binaire
    cmp ah, 30h
    jne clock_bad
    shr al, 1
    and ax, 7
    cmp ax, 2
    je clock_good
    cmp ax, 3
    je clock_good
clock_bad:
    xor ax, ax
    popf
    ret
clock_good:
    mov ax, 1
    popf
    ret
s3_clock_init endp

s3_ticks proc far
    push bp
    mov bp, sp
    pushf
    sub sp, 2
    mov word ptr ss:[bp-4], 64
    cli
    push ebx
    push ecx
    push esi
    push edi
    push ds
    push es
    mov ax, seg s3_clock_fault
    mov ds, ax
    mov ax, 40h
    mov es, ax
    call clock_raw
    mov edi, ecx
    mov dx, 8
clock_retry:
    call clock_raw
    mov eax, ecx
    sub eax, edi
    cmp eax, 32768
    jb clock_return
    ; Le status OUT et le compte ne sont pas toujours pris au même instant
    ; dans les émulateurs. Refuser le saut de demi-période, refaire une paire.
    mov edi, ecx
    dec dx
    jnz clock_retry
    mov word ptr s3_clock_fault, 1 ; mesure invalidée, jamais une correction silencieuse
clock_return:
    ; À la frontière de période, certains PIC émulés ne montrent pas encore
    ; IRQ0 alors que le compteur PIT a déjà rebouclé. Hors ISR, laisser passer
    ; l'IRQ puis reprendre une mesure, plutôt que renvoyer un tick BIOS en retard.
    cmp esi, 64
    jae clock_ready
    test bh, 1
    jnz clock_ready
    test word ptr ss:[bp-2], 200h
    jz clock_uncertain
    dec word ptr ss:[bp-4]
    jz clock_uncertain
    sti
    nop
    cli
    call clock_raw
    mov edi, ecx
    mov dx, 8
    jmp clock_retry
clock_uncertain:
    mov word ptr s3_clock_fault, 1
clock_ready:
    mov eax, ecx
    mov edx, ecx
    shr edx, 16                   ; retour C 16 bits DX:AX
    pop es
    pop ds
    pop edi
    pop esi
    pop ecx
    pop ebx
    add sp, 2
    popf
    pop bp
    ret
s3_ticks endp

; Classe du processeur pour les rapports : 3 = 386, 4 = 486 ou ultérieur
; (bit AC d'EFLAGS modifiable). Ne lit pas CPUID, absent des premiers 486.
public s3_cpu_class
s3_cpu_class proc far
    pushfd
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 40000h
    push eax
    popfd
    pushfd
    pop eax
    popfd
    xor eax, ecx
    test eax, 40000h
    mov ax, 3
    jz cpu_done
    mov ax, 4
cpu_done:
    ret
s3_cpu_class endp

clock_raw proc near
    movzx ecx, word ptr es:[6ch]  ; les 16 bits bas du compteur BIOS suffisent
    mov al, 0c2h                  ; latch status et compteur, canal 0
    out 43h, al
    in al, 40h
    mov bl, al                    ; OUT et mode au moment du latch
    in al, 40h
    mov ah, al
    in al, 40h
    xchg al, ah                   ; AX = compteur, lecture LSB puis MSB
    movzx eax, ax
    test eax, eax
    jnz clock_count
    mov eax, 65536
clock_count:
    mov esi, 65536
    sub esi, eax
    mov al, bl
    shr al, 1
    and al, 7
    cmp al, 3
    jne clock_pending
    shr esi, 1                    ; mode 3 : compteur décrémenté de deux par cycle
    test bl, 80h
    jnz clock_pending
    add esi, 32768                ; seconde demi-période, OUT bas
clock_pending:
    mov al, 0ah                   ; sélectionner et lire IRR, sans changer le masque
    out 20h, al
    in al, 20h
    mov bh, al
    test al, 1
    jz clock_combine
    cmp esi, 32768                ; débordement avant le latch, pas juste après
    jae clock_combine
    inc cx
clock_combine:
    shl ecx, 16
    add ecx, esi
    ret
clock_raw endp
end
