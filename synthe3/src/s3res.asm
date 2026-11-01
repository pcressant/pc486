; SPDX-License-Identifier: GPL-3.0-or-later
; S3RES.EXE : vecteurs résidents. Aucun appel DOS ni BIOS, aucun calcul de parole
; ici : INT 8h donne la main à la pompe C sur une pile privée, interruptions
; permises ; l'IRQ du DSP ne fait que tourner l'anneau DMA ; l'API du lecteur
; d'écran n'écrit qu'un octet dans la file ou lit un écho (registres 16 bits).
.model large, c
.386
extrn res_sb:byte, ap_link:byte, res_info:byte
extrn res_pump:far

RS_BASE       equ 0
RS_CHANNEL    equ 2
RS_FLAGS      equ 4
RS_PLAYING    equ 6
RS_READY      equ 8
RS_USED       equ 16
RS_PLAYED     equ 24
RS_COMPLETED  equ 28
RS_SILENT     equ 30
RS_UNDERRUNS  equ 32
RS_SPEAKING   equ 34
RS_DMAOFF     equ 36
RS_IRQ        equ 40
RS_WINDOWS    equ 42
RS_ARRIVE     equ 44
RS_ARRIVED    equ 46
AL_HEAD       equ 0
AL_TAIL       equ 2
AL_CUTS       equ 4
AL_CUTAT      equ 6
AL_EHEAD      equ 8
AL_ETAIL      equ 10
AL_DROPPED    equ 12
AL_ECHO       equ 14
AL_BUF        equ 46
AP_INPUT      equ 1024
AP_ECHOES     equ 32
RES_IDLE      equ 6
RUNNING       equ 1
HALTED        equ 2
RI_STATE      equ 22              ; offsetof(RES_INFO, state), vérifié dans s3res.c
RI_CLIENTS    equ 78              ; offsetof(RES_INFO, clients)
RI_ISR_IRQ    equ 88              ; offsetof(RES_INFO, isr[2])
RI_VECTOR     equ 104             ; offsetof(RES_INFO, vector_irq)
STACK_SIZE    equ 1536              ; 242 octets utilisés au plus mesurés (RES_INFO.stack_low)

.data?
public res_stack
res_stack db STACK_SIZE dup (?)
res_stack_top label byte

.data
public res_mux_es, res_mux_di
res_mux_es dw 0
res_mux_di dw 0

.code
public res_isr08, res_isr2f, res_irq, res_api, res_mux
public res_old08, res_old2f, res_oldirq
res_old08 dd 0
res_old2f dd 0
res_oldirq dd 0
busy db 0
lent db 0                           ; vecteur de l'IRQ rendu pendant Windows
saved_ss dw 0
saved_esp dd 0

; INT 8h : BIOS d'abord (heure, acquittement du PIC), puis la pompe si elle n'est
; pas déjà en cours. Registres 32 bits, FS et GS conservés : le moteur est en 386.
res_isr08 proc far
    pushf
    call dword ptr cs:res_old08
    cmp cs:busy, 0
    jne i08_done
    push ds
    push ax
    mov ax, seg res_sb
    mov ds, ax
    cmp word ptr res_sb[RS_WINDOWS], 0
    pop ax
    pop ds
    jne i08_done
    mov cs:busy, 1
    pushad
    push ds
    push es
    push fs
    push gs
    mov cs:saved_ss, ss
    mov cs:saved_esp, esp
    mov ax, seg res_stack
    mov ss, ax
    mov esp, offset res_stack_top
    mov ds, ax
    mov es, ax
    cld
    sti
    call res_pump
    cli
    mov ss, cs:saved_ss
    mov esp, cs:saved_esp
    pop gs
    pop fs
    pop es
    pop ds
    popad
    mov cs:busy, 0
i08_done:
    iret
res_isr08 endp

; Installation : INT 2Fh avec AX donné ; retour AX, ES:DI dans res_mux_es/di.
res_mux proc far value:word
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, value
    xor bx, bx
    xor di, di
    mov es, di
    int 2fh
    push ds
    push ax
    mov ax, seg res_mux_es
    mov ds, ax
    mov res_mux_es, es
    mov res_mux_di, di
    pop ax
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
res_mux endp

; Couper le son sans attendre : masque DMA, remise à zéro du DSP. DS = DGROUP.
quiet_sb proc near
    push ax
    push dx
    mov al, byte ptr res_sb[RS_CHANNEL]
    or al, 4
    out 0ah, al
    mov dx, word ptr res_sb[RS_BASE]
    add dx, 6
    mov al, 1
    out dx, al
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    xor al, al
    out dx, al
    mov word ptr res_sb[RS_FLAGS], 0
    pop dx
    pop ax
    ret
quiet_sb endp

; INT 2Fh : D300h présence (ES:DI entrée de l'API), D301h ES:DI = RES_INFO ;
; 1605h/1606h : Windows prend puis rend la Sound Blaster.
res_isr2f proc far
    cmp ax, 0d300h
    jne f_info
    mov ax, 0d3ffh
    mov bx, 0100h
    mov di, seg res_api
    mov es, di
    mov di, offset res_api
    iret
f_info:
    cmp ax, 0d301h
    jne f_start
    mov ax, 0d3ffh
    mov di, seg res_info
    mov es, di
    mov di, offset res_info
    iret
f_start:
    cmp ax, 1605h
    jne f_end
    push ds
    push es
    push ax
    push di
    mov ax, seg res_sb
    mov ds, ax
    pushf
    cli
    mov word ptr res_sb[RS_WINDOWS], 1
    test word ptr res_sb[RS_FLAGS], RUNNING or HALTED
    jz f_slept
    call quiet_sb
f_slept:
    ; Rendre le vecteur de l'IRQ s'il est encore le nôtre : posé avant Windows, il
    ; ferait renvoyer l'interruption de la carte à la machine DOS active (S3RES
    ; endormi) au lieu du pilote audio de Windows, dont la restitution se figerait.
    xor ax, ax
    mov es, ax
    mov di, word ptr res_info[RI_VECTOR]
    shl di, 2
    mov ax, word ptr res_info[RI_ISR_IRQ]
    cmp ax, es:[di]
    jne f_kept
    mov ax, word ptr res_info[RI_ISR_IRQ+2]
    cmp ax, es:[di+2]
    jne f_kept
    mov ax, word ptr cs:res_oldirq
    mov es:[di], ax
    mov ax, word ptr cs:res_oldirq+2
    mov es:[di+2], ax
    mov cs:lent, 1
f_kept:
    popf
    pop di
    pop ax
    pop es
    pop ds
    jmp f_chain
f_end:
    cmp ax, 1606h
    jne f_chain
    push ds
    push es
    push ax
    push di
    mov ax, seg res_sb
    mov ds, ax
    pushf
    cli
    mov word ptr res_sb[RS_FLAGS], 0
    mov word ptr res_sb[RS_WINDOWS], 0
    cmp cs:lent, 0
    je f_back
    xor ax, ax                        ; Windows terminé : reprendre l'IRQ
    mov es, ax
    mov di, word ptr res_info[RI_VECTOR]
    shl di, 2
    mov ax, word ptr res_info[RI_ISR_IRQ]
    mov es:[di], ax
    mov ax, word ptr res_info[RI_ISR_IRQ+2]
    mov es:[di+2], ax
    mov cs:lent, 0
f_back:
    popf
    pop di
    pop ax
    pop es
    pop ds
f_chain:
    jmp dword ptr cs:res_old2f
res_isr2f endp

; IRQ du DSP, fin d'un bloc de 512 : compter ce qui a été joué, passer au bloc
; suivant ; s'il n'est pas prêt, y écrire du silence avant que le DMA ne l'atteigne
; (il vient d'en lire au plus un octet). Silence prolongé : arrêt du DMA.
res_irq proc far
    push ax
    push ds
    mov ax, seg res_sb
    mov ds, ax
    test word ptr res_sb[RS_FLAGS], RUNNING or HALTED
    jnz irq_ours
    pop ds
    pop ax
    jmp dword ptr cs:res_oldirq
irq_ours:
    push bx
    push cx
    push dx
    push di
    push es
    mov dx, word ptr res_sb[RS_BASE]
    add dx, 0eh
    in al, dx                         ; acquitter le DSP 8 bits
    test word ptr res_sb[RS_FLAGS], RUNNING
    jz irq_eoi                        ; Ctrl-X : la pompe finira l'arrêt
    mov bx, word ptr res_sb[RS_PLAYING]
    shl bx, 1
    mov ax, word ptr res_sb[RS_USED+bx]
    add word ptr res_sb[RS_PLAYED], ax
    adc word ptr res_sb[RS_PLAYED+2], 0
    mov word ptr res_sb[RS_USED+bx], 0
    mov word ptr res_sb[RS_READY+bx], 0
    inc word ptr res_sb[RS_COMPLETED]
    shr bx, 1
    inc bx
    and bx, 3
    mov word ptr res_sb[RS_PLAYING], bx
    shl bx, 1
    cmp word ptr res_sb[RS_READY+bx], 0
    jne irq_have
    les di, dword ptr res_sb[RS_DMAOFF]
    mov ax, bx
    shl ax, 8                         ; bloc * 512
    add di, ax
    mov cx, 256
    mov ax, 8080h
    cld
    rep stosw
    inc word ptr res_sb[RS_SILENT]
    cmp word ptr res_sb[RS_SPEAKING], 0
    je irq_quiet
    inc word ptr res_sb[RS_UNDERRUNS]
    jmp irq_eoi
irq_quiet:
    cmp word ptr res_sb[RS_SILENT], RES_IDLE
    jb irq_eoi
    mov al, byte ptr res_sb[RS_CHANNEL]
    or al, 4
    out 0ah, al                       ; le DSP sera remis à zéro au prochain départ
    mov word ptr res_sb[RS_FLAGS], 0
    jmp irq_eoi
irq_have:
    mov word ptr res_sb[RS_SILENT], 0
irq_eoi:
    mov al, 20h
    cmp word ptr res_sb[RS_IRQ], 8
    jb irq_master
    out 0a0h, al
irq_master:
    out 20h, al
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ds
    pop ax
    iret
res_irq endp

; API du lecteur d'écran, appel far, registres 16 bits, tous conservés sauf AX.
;   AL=0 : déposer l'octet BL ; AX = 1, ou 0 file pleine (réessayer plus tard).
;          Ctrl-X (18h) masque aussitôt le DMA : silence sans attendre la pompe.
;   AL=1 : AX = écho @E rendu, ou FFFFh.
;   AL=2 : AX = état (RES_INFO.state ; bit 8 : Windows en cours).
;   AL=3 : attacher un lecteur (le retrait à chaud est alors refusé) ; AX = 1.
res_api proc far
    push ds
    push si
    push di
    mov si, seg ap_link
    mov ds, si
    cmp al, 1
    je api_echo
    ja api_more
    cmp bl, 18h
    jne api_text
    ; Ctrl-X : jamais refusé ni stocké ; sa position suffit à la pompe, qui
    ; abandonne tout ce qui le précède. Le DMA est masqué aussitôt.
    mov si, word ptr ap_link[AL_HEAD]
    mov word ptr ap_link[AL_CUTAT], si
    inc word ptr ap_link[AL_CUTS]
    pushf
    cli
    test word ptr res_sb[RS_FLAGS], RUNNING
    jz api_unmasked
    mov al, byte ptr res_sb[RS_CHANNEL]
    or al, 4
    out 0ah, al
    mov word ptr res_sb[RS_FLAGS], HALTED
api_unmasked:
    popf
    jmp api_ok
api_text:
    mov si, word ptr ap_link[AL_HEAD]
    lea di, [si+1]
    and di, AP_INPUT-1
    cmp di, word ptr ap_link[AL_TAIL]
    je api_full
    cmp si, word ptr ap_link[AL_TAIL]
    jne api_store
    push es                           ; file vide : instant d'arrivée (mesures)
    mov ax, 40h
    mov es, ax
    mov ax, word ptr es:[6ch]
    pop es
    mov word ptr res_sb[RS_ARRIVE], ax
    mov word ptr res_sb[RS_ARRIVED], 1
api_store:
    mov byte ptr ap_link[AL_BUF+si], bl
    mov word ptr ap_link[AL_HEAD], di
api_ok:
    mov ax, 1
    jmp api_done
api_full:
    inc word ptr ap_link[AL_DROPPED]
    xor ax, ax
    jmp api_done
api_echo:
    mov si, word ptr ap_link[AL_ETAIL]
    mov ax, 0ffffh
    cmp si, word ptr ap_link[AL_EHEAD]
    je api_done
    xor ax, ax
    mov al, byte ptr ap_link[AL_ECHO+si]
    inc si
    and si, AP_ECHOES-1
    mov word ptr ap_link[AL_ETAIL], si
    jmp api_done
api_more:
    cmp al, 3
    jne api_state
    inc word ptr res_info[RI_CLIENTS]
    mov ax, 1
    jmp api_done
api_state:
    mov ax, word ptr res_info[RI_STATE]
    cmp word ptr res_sb[RS_WINDOWS], 0
    je api_done
    or ah, 1
api_done:
    pop di
    pop si
    pop ds
    ret
res_api endp
end
