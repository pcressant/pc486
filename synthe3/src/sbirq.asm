; SPDX-License-Identifier: GPL-3.0-or-later
; IRQ DSP et publication des demi-tampons, mode réel 386, aucune fonction C appelée.
.model large, c
.386
extrn sb_state:byte
.code
public s3_sb_irq, s3_sb_commit, s3_sb_wait

SB_BASE       equ 0
SB_CHANNEL    equ 2
SB_FLAGS      equ 4
SB_PLAYING    equ 6
SB_READY      equ 8
SB_USED       equ 12
SB_FINAL      equ 16
SB_COMPLETED  equ 20
SB_PLAYED     equ 22
SB_UNDERRUNS  equ 26

s3_sb_irq proc far
    push eax
    push ebx
    push edx
    push ds
    mov ax, seg sb_state
    mov ds, ax
    mov dx, word ptr sb_state[SB_BASE]
    add dx, 14
    in al, dx                         ; acquitter le DSP 8 bits
    test word ptr sb_state[SB_FLAGS], 1
    jz irq_eoi
    mov bx, word ptr sb_state[SB_PLAYING]
    shl bx, 1
    mov word ptr sb_state[SB_READY+bx], 0
    mov ax, word ptr sb_state[SB_USED+bx]
    add word ptr sb_state[SB_PLAYED], ax
    adc word ptr sb_state[SB_PLAYED+2], 0
    inc word ptr sb_state[SB_COMPLETED]
    cmp word ptr sb_state[SB_FINAL+bx], 0
    jne irq_done
    xor word ptr sb_state[SB_PLAYING], 1
    xor bx, 2
    cmp word ptr sb_state[SB_READY+bx], 0
    jne irq_eoi
    inc word ptr sb_state[SB_UNDERRUNS]
    mov word ptr sb_state[SB_FLAGS], 4 ; sous-alimentation : ne pas rejouer l'ancien bloc
    jmp irq_stop
irq_done:
    mov word ptr sb_state[SB_FLAGS], 2
irq_stop:
    mov ax, word ptr sb_state[SB_CHANNEL]
    or al, 4
    out 0ah, al                       ; arrêter immédiatement les requêtes DMA
irq_eoi:
    mov al, 20h
    out 20h, al
    pop ds
    pop edx
    pop ebx
    pop eax
    iret
s3_sb_irq endp

; commit(bank, nombre réel, dernier) : publication atomique après remplissage.
; Une IRQ pendant le calcul peut avoir arrêté le DMA : ne jamais publier alors.
s3_sb_commit proc far
    push bp
    mov bp, sp
    pushf
    cli
    push bx
    push ds
    mov ax, seg sb_state
    mov ds, ax
    xor ax, ax
    test word ptr sb_state[SB_FLAGS], 6
    jnz commit_done
    mov bx, word ptr [bp+6]
    cmp bx, 1
    ja commit_done
    test word ptr sb_state[SB_FLAGS], 1
    jz commit_bank
    cmp bx, word ptr sb_state[SB_PLAYING]
    je commit_done
commit_bank:
    shl bx, 1
    cmp word ptr sb_state[SB_READY+bx], 0
    jne commit_done
    mov ax, word ptr [bp+8]
    mov word ptr sb_state[SB_USED+bx], ax
    mov ax, word ptr [bp+10]
    mov word ptr sb_state[SB_FINAL+bx], ax
    mov word ptr sb_state[SB_READY+bx], 1
    mov ax, 1
commit_done:
    pop ds
    pop bx
    popf
    pop bp
    ret
s3_sb_commit endp

; Attendre une IRQ sans boucle active et sans perdre le réveil entre test et HLT.
; need_refill==0 : une fin de source est déjà en file, attendre seulement sa lecture.
s3_sb_wait proc far
    push bp
    mov bp, sp
    pushf
    cli
    push ax
    push bx
    push ds
    push es
    test word ptr ss:[bp-2], 200h
    jz wait_done                     ; ne pas activer les IRQ d'un appelant qui les masque
    mov ax, seg sb_state
    mov ds, ax
    test word ptr sb_state[SB_FLAGS], 1
    jz wait_done
    ; Une touche peut être arrivée pendant le remplissage : son IRQ est déjà
    ; passée. Ne pas dormir en attendant une nouvelle IRQ dans ce cas.
    mov ax, 40h
    mov es, ax
    test byte ptr es:[17h], 4
    jnz wait_done
    mov ax, word ptr es:[1ah]
    cmp ax, word ptr es:[1ch]
    jne wait_done
    cmp word ptr [bp+6], 0
    je wait_sleep
    mov bx, word ptr sb_state[SB_PLAYING]
    xor bx, 1
    shl bx, 1
    cmp word ptr sb_state[SB_READY+bx], 0
    je wait_done
wait_sleep:
    sti
    hlt                             ; STI prend effet après HLT : pas de réveil perdu
wait_done:
    pop es
    pop ds
    pop bx
    pop ax
    popf
    pop bp
    ret
s3_sb_wait endp
end
