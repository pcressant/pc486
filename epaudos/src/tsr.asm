; SPDX-License-Identifier: GPL-3.0-or-later
; .COM 8086 : un segment, pile privée commune aux appels résidents.
.8086
.model tiny
.code
org 100h
public _old15, _old08, _old28, _old2f
public _old0c, _old71, _isr0c, _isr71
public _isr15, _isr08, _isr28, _isr2f
public _uart_out, _uart_in, _port_in, _port_out
public _dos_open, _dos_create, _dos_read, _dos_write, _dos_close
public _dos_psp, _dos_setpsp, _dos_free, _dos_print, _dos_vector
public _dos_setvector, _mux, _keep, _bss_start
public _dos_setfarvector, _dos_indos, _fail24
public _hw_lock, _hw_unlock
extrn _resident_end:byte, stack_top:byte
extrn _install:near, _timer:near, _idle:near, _ed_scan:near
extrn _ed_windows:byte, _ed_cfg:byte, _ed_resume:byte
extrn _uart_head:byte, _uart_tail:byte, _uart_buf:byte
extrn _uart_rxhead:byte, _uart_rxtail:byte, _uart_rxbuf:byte

start:
    mov ax,cs
    mov ds,ax
    mov es,ax
    ; Garder la pile de chargement DOS (haut du bloc) : après pose des
    ; vecteurs, une interruption peut déjà utiliser la pile privée.
    cld
    mov di,offset _bss_start
    mov cx,offset _resident_end
    sub cx,di
    xor ax,ax
    rep stosb
    call _install
    mov ax,4c00h
    int 21h

_old15 dd 0
_old08 dd 0
_old28 dd 0
_old2f dd 0
_old0c dd 0
_old71 dd 0
busy db 0
taken db 0
saved_ss dw 0
saved_sp dw 0
key_ss dw 0
key_sp dw 0

enter_private macro
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov cs:busy,1
    mov cs:saved_ss,ss
    mov cs:saved_sp,sp
    mov ax,cs
    mov ss,ax
    mov sp,offset stack_top
    mov ds,ax
    mov es,ax
    cld
endm
leave_private macro
    cli
    mov ss,cs:saved_ss
    mov sp,cs:saved_sp
    mov cs:busy,0
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
endm

_isr15 proc far
    cmp ah,4fh
    jne pass15
    push ax
    push es
    mov ax,40h
    mov es,ax
    mov al,es:[49h]
    cmp al,3
    jbe keytext
    cmp al,7
    je keytext
    pop es
    pop ax
    jmp pass15
keytext:
    pop es
    pop ax
    ; Conserver le scancode avant enter_private qui emploie AX.
    push ax
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov cs:key_ss,ss
    mov cs:key_sp,sp
    mov ax,cs
    mov ss,ax
    mov sp,offset key_stack_top
    mov ds,ax
    mov es,ax
    cld
    mov bx,cs:key_sp
    mov es,cs:key_ss
    mov ax,es:[bx+18]     ; AX ajouté avant les neuf registres
    xor ah,ah
    push ax
    mov ax,40h
    mov es,ax
    xor ax,ax
    mov al,es:[17h]
    pop bx
    push ax              ; second argument : indicateurs BIOS
    push bx              ; premier argument : scancode
    call _ed_scan
    add sp,4
    mov cs:taken,al
    cli
    mov ss,cs:key_ss
    mov sp,cs:key_sp
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop ax
    cmp cs:taken,0
    je pass15
    push bp
    mov bp,sp
    and word ptr ss:[bp+6],0fffeh
    pop bp
    iret
pass15:
    jmp dword ptr cs:_old15
_isr15 endp

_isr08 proc far
    pushf
    call dword ptr cs:_old08
    ; Le BIOS a terminé INT 8h et acquitté le PIC : les IRQ UART peuvent
    ; préempter le moteur. Le clavier possède sa propre petite pile.
    cmp cs:busy,0
    jne done1c
    enter_private
    sti
    call _timer
    leave_private
done1c:
    iret
_isr08 endp

_isr28 proc far
    pushf
    call dword ptr cs:_old28
    cmp cs:busy,0
    jne done28
    enter_private
    sti
    call _idle
    leave_private
done28:
    iret
_isr28 endp

; Émission au rythme de l'UART, pas au rythme 18,2 Hz du timer. Pas de C,
; pas de pile privée et au plus un octet par condition THRE (8250 inclus).
; Toutes les conditions en attente sont traitées avant l'IRET : la ligne
; d'interruption reste haute tant que l'IIR signale quelque chose, et le 8259
; (front) ne redéclencherait pas un THRE resté derrière une réception.
uart_isr macro symbol,base,index,previous,slave
local passthrough,empty,finish,receive,status,store,again,next,modem
symbol proc far
    push ax
    push bx
    push dx
    push ds
    mov ax,cs
    mov ds,ax
    cmp _ed_windows,0
    jne passthrough
    mov dx,base+2
    in al,dx
    and al,0fh
    test al,1
    jnz passthrough
again:
    cmp al,4
    je receive
    cmp al,0ch
    je receive
    cmp al,6
    je status
    cmp al,0
    je modem
    cmp al,2
    jne finish
    xor bx,bx
    mov bl,_uart_tail+index
    cmp bl,_uart_head+index
    je empty
    mov al,_uart_buf+index*256[bx]
    inc bl
    mov _uart_tail+index,bl
    mov dx,base
    out dx,al
    jmp next
empty:
    mov dx,base+1
    mov al,1
    out dx,al
    jmp next
receive:
    mov dx,base
    in al,dx
    xor bx,bx
    mov bl,_uart_rxhead+index
    mov ah,bl
    inc ah
    cmp ah,_uart_rxtail+index
    je next
    mov _uart_rxbuf+index*256[bx],al
    mov _uart_rxhead+index,ah
    jmp next
status:
    mov dx,base+5
    in al,dx
    jmp next
modem:
    mov dx,base+6
    in al,dx
next:
    mov dx,base+2
    in al,dx
    and al,0fh
    test al,1
    jz again
finish:
    mov al,20h
    if slave
        out 0a0h,al
    endif
    out 20h,al
    pop ds
    pop dx
    pop bx
    pop ax
    iret
passthrough:
    pop ds
    pop dx
    pop bx
    pop ax
    jmp dword ptr cs:previous
symbol endp
endm
uart_isr _isr0c,3f8h,0,_old0c,0
uart_isr _isr71,3e8h,1,_old71,1

_isr2f proc far
    cmp ax,0ed00h
    jne notcheck
    mov ax,0edffh
    mov bx,0100h
    iret
notcheck:
    cmp ax,0ed01h
    jne notconfig
    push cs
    pop es
    mov bx,offset _ed_cfg
    iret
notconfig:
    cmp ax,1605h
    jne notstart
    mov cs:_ed_windows,1
    ; Couper avant de céder COM1 à Windows ; aucune attente UART.
    push ax
    push dx
    mov dx,3f9h
    xor al,al
    out dx,al
    mov dx,3e9h
    out dx,al
    mov cs:_uart_head,al
    mov cs:_uart_tail,al
    mov cs:_uart_head+1,al
    mov cs:_uart_tail+1,al
    mov dx,3fdh
    in al,dx
    test al,20h
    jz muted
    mov dx,3f8h
    mov al,24
    out dx,al
muted:
    pop dx
    pop ax
notstart:
    cmp ax,1606h
    jne pass2f
    mov cs:_ed_windows,0
    mov cs:_ed_resume,1
pass2f:
    jmp dword ptr cs:_old2f
_isr2f endp

; Petits adaptateurs cdecl : le compilateur ne lie aucune bibliothèque C.
_port_in proc near
    push bp
    mov bp,sp
    mov dx,[bp+4]
    xor ax,ax
    in al,dx
    pop bp
    ret
_port_in endp
_port_out proc near
    push bp
    mov bp,sp
    mov dx,[bp+4]
    mov ax,[bp+6]
    out dx,al
    pop bp
    ret
_port_out endp
_uart_out proc near
    push bp
    mov bp,sp
    mov dx,[bp+4]
    add dx,5
    in al,dx
    test al,20h
    jz txbusy
    sub dx,5
    mov ax,[bp+6]
    out dx,al
    mov ax,1
    jmp txdone
txbusy:
    xor ax,ax
txdone:
    pop bp
    ret
_uart_out endp
_uart_in proc near
    push bp
    mov bp,sp
    mov dx,[bp+4]
    add dx,5
    in al,dx
    test al,1
    jz rxempty
    sub dx,5
    xor ax,ax
    in al,dx
    jmp rxdone
rxempty:
    mov ax,-1
rxdone:
    pop bp
    ret
_uart_in endp

_hw_lock proc near
    pushf
    pop ax
    cli
    ret
_hw_lock endp
_hw_unlock proc near
    push bp
    mov bp,sp
    push word ptr [bp+4]
    popf
    pop bp
    ret
_hw_unlock endp

dos_file macro symbol, fn
symbol proc near
    push bp
    mov bp,sp
    push bx
    push cx
    push dx
    mov dx,[bp+4]
    xor cx,cx
    mov ax,fn
    int 21h
    jnc @F
    mov ax,-1
@@:
    pop dx
    pop cx
    pop bx
    pop bp
    ret
symbol endp
endm
dos_file _dos_open,3d00h
dos_file _dos_create,3c00h

dos_io macro symbol, fn
symbol proc near
    push bp
    mov bp,sp
    push bx
    push cx
    push dx
    mov bx,[bp+4]
    mov dx,[bp+6]
    mov cx,[bp+8]
    mov ah,fn
    int 21h
    jnc @F
    mov ax,-1
@@:
    pop dx
    pop cx
    pop bx
    pop bp
    ret
symbol endp
endm
dos_io _dos_read,3fh
dos_io _dos_write,40h

_dos_close proc near
    push bp
    mov bp,sp
    push bx
    mov bx,[bp+4]
    mov ah,3eh
    int 21h
    pop bx
    pop bp
    ret
_dos_close endp
_dos_psp proc near
    push bx
    mov ah,51h
    int 21h
    mov ax,bx
    pop bx
    ret
_dos_psp endp
_dos_setpsp proc near
    push bp
    mov bp,sp
    push bx
    mov bx,[bp+4]
    mov ah,50h
    int 21h
    pop bx
    pop bp
    ret
_dos_setpsp endp
_dos_free proc near
    push bp
    mov bp,sp
    push es
    mov es,[bp+4]
    mov ah,49h
    int 21h
    pop es
    pop bp
    ret
_dos_free endp
_dos_print proc near
    push bp
    mov bp,sp
    mov dx,[bp+4]
    mov ah,9
    int 21h
    pop bp
    ret
_dos_print endp
_dos_vector proc near
    push bp
    mov bp,sp
    push bx
    push es
    mov ax,[bp+4]
    mov ah,35h
    int 21h
    mov ax,bx
    mov dx,es
    pop es
    pop bx
    pop bp
    ret
_dos_vector endp
_dos_setvector proc near
    push bp
    mov bp,sp
    mov ax,[bp+4]
    mov dx,[bp+6]
    mov ah,25h
    int 21h
    pop bp
    ret
_dos_setvector endp
_dos_setfarvector proc near
    push bp
    mov bp,sp
    push ds
    mov ax,[bp+4]
    mov dx,[bp+6]
    mov ds,[bp+8]
    mov ah,25h
    int 21h
    pop ds
    pop bp
    ret
_dos_setfarvector endp
_dos_indos proc near
    push bx
    push es
    mov ah,34h
    int 21h
    mov ax,bx
    mov dx,es
    pop es
    pop bx
    ret
_dos_indos endp
_fail24 proc far
    mov al,3
    iret
_fail24 endp
_mux proc near
    push bp
    mov bp,sp
    push bx
    push es
    mov ax,[bp+4]
    int 2fh
    cmp word ptr [bp+4],0ed01h
    jne muxdone
    mov ax,bx
    mov dx,es
muxdone:
    pop es
    pop bx
    pop bp
    ret
_mux endp
_keep proc near
    mov dx,offset _resident_end
    add dx,15
    mov cl,4
    shr dx,cl
    mov ax,3100h
    int 21h
_keep endp

.data?
_bss_start label byte
key_stack db 512 dup (?)
key_stack_top label byte
end start
