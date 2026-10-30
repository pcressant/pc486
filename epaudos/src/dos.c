/* SPDX-License-Identifier: GPL-3.0-or-later
 * Adaptation DOS 3.3+, BIOS AT, UART 8250. Aucun appel DOS dans le timer.
 */
#include "core.h"
#include <i86.h>

extern int __cdecl port_in(unsigned port);
extern void __cdecl port_out(unsigned port, unsigned value);
extern int __cdecl uart_out(unsigned base, unsigned value);
extern int __cdecl uart_in(unsigned base);
extern int __cdecl dos_open(const char *path);
extern int __cdecl dos_create(const char *path);
extern int __cdecl dos_read(int h, void *buf, int n);
extern int __cdecl dos_write(int h, const void *buf, int n);
extern void __cdecl dos_close(int h);
extern unsigned __cdecl dos_psp(void);
extern void __cdecl dos_setpsp(unsigned seg);
extern void __cdecl dos_free(unsigned seg);
extern void __cdecl dos_print(const char *s);
typedef void (__far *VECTOR)(void);
extern VECTOR __cdecl dos_vector(unsigned n);
extern void __cdecl dos_setvector(unsigned n, void *fn);
extern void __cdecl dos_setfarvector(unsigned n, VECTOR fn);
extern BYTE __far * __cdecl dos_indos(void);
extern unsigned long __cdecl mux(unsigned fn);
extern unsigned __cdecl cur_vm(void);
extern void __cdecl keep(void);
extern void isr15(void), isr08(void), isr28(void), isr2f(void);
extern void isr0c(void), isr71(void);
extern void fail24(void);
extern VECTOR old15, old08, old28, old2f;
extern VECTOR old0c, old71;
volatile BYTE uart_head[2], uart_tail[2];
BYTE uart_buf[512];
volatile BYTE uart_rxhead[2], uart_rxtail[2];
BYTE uart_rxbuf[512];
static unsigned psp;
static BYTE armed;
static char config_path[128];
static int saved_row = -1, saved_cols;
static WORD saved_cells[ED_COLS];
static WORD __far *saved_video;
static BYTE __far *indos;
static const char *const cfg_names[ED_CFG] = {"Debit","Hauteur","Voix","Ponctuation","Echo","Majuscules","EchoEcran"};

static BYTE __far *bda = (BYTE __far *)MK_FP(0x40,0);

/* Boîte aux lettres d'Épaulard (INT 2Fh AX=ED02h -> ES:BX). Sous Windows, COM1
   et COM3 appartiennent à Épaulard : dans une machine DOS, Épaudos laisse sa parole
   dans uart_buf (port 0) et lit les échos de l'Apollo II dans uart_rxbuf ; version
   2, ses trames Baum dans uart_buf+256 (port 1), d'où Épaulard tire les cellules,
   et les touches de la plage, réencodées par Épaulard, dans uart_rxbuf+256.
   Mémoire chargée avant Windows, donc commune à toutes les machines. Producteur et
   consommateur uniques par file : chacun n'écrit que son propre indice. */
EDBOX ed_box = { {'E','P','B','X'}, 2 };
static WORD box_alive, box_ticks;

/* Épaulard vivant : son compteur a changé il y a moins de 2 s */
static int relay_alive(void)
{
    if (!ed_box.relay) return 0;
    if (ed_box.alive != box_alive) { box_alive = ed_box.alive; box_ticks = 0; return 1; }
    return ++box_ticks < 36;
}
int hw_guest(int key)
{
    unsigned vm;
    if (!relay_alive()) { ed_box.active = 0; return ED_SLEEP; }
    vm = cur_vm();
    if (vm <= 1) return key ? ED_SLEEP : ED_SKIP;   /* machine système : Épaulard lit */
    /* La machine qui reçoit les touches est celle de la fenêtre DOS active ; sans
       touche encore, la première machine DOS qui tourne. */
    if (key || !ed_box.vm) ed_box.vm = vm;
    if (vm != ed_box.vm) return ED_SKIP;
    ed_box.active = 1;
    if (key) ++ed_box.keys;     /* Épaulard se tient prêt à relayer (dosbox.c) */
    return ED_RUN;
}
int hw_tx(int port, BYTE value)
{
    unsigned base = port ? 0x3e8 : 0x3f8;
    BYTE next = (BYTE)(uart_head[port]+1);
    WORD flags;
    if (ed_windows) {
        /* Mode invité : aucun accès au port ; Épaulard vide la file. */
        if (next == uart_tail[port]) return 0;
        uart_buf[port*256+uart_head[port]] = value; uart_head[port] = next;
        return 1;
    }
    if (next == uart_tail[port]) {
        /* File pleine : si l'UART est pourtant libre, l'interruption THRE
           s'est perdue ; relancer l'émission à la main, ISR exclue. */
        flags = hw_lock();
        if (port_in(base+5) & 0x20) {
            port_out(base,uart_buf[port*256+uart_tail[port]]); ++uart_tail[port];
        }
        port_out(base+1,3);
        hw_unlock(flags);
        return 0;
    }
    uart_buf[port*256+uart_head[port]] = value; uart_head[port] = next;
    port_out(base+1,3);
    return 1;
}
int hw_rx(int port)
{
    BYTE b;
    if (uart_rxhead[port] == uart_rxtail[port]) return -1;
    b = uart_rxbuf[port*256+uart_rxtail[port]]; ++uart_rxtail[port];
    return b;
}
void hw_quiet(void)
{
    /* Windows a déjà reçu les ports par INT 2Fh : ne plus les toucher. Les files
       partagées avec Épaulard : ne vider que ce que l'on consomme. */
    if (ed_windows) {
        uart_rxtail[0] = uart_rxhead[0]; uart_rxtail[1] = uart_rxhead[1];
        uart_head[1] = uart_tail[1];    /* trame interrompue : Épaulard se recale */
        return;
    }
    port_out(0x3f9,0); port_out(0x3e9,0);
    uart_out(0x3f8,24);
    uart_head[0] = uart_tail[0] = uart_head[1] = uart_tail[1] = 0;
    uart_rxhead[0] = uart_rxtail[0] = uart_rxhead[1] = uart_rxtail[1] = 0;
}
void hw_serial(int port, int divisor)
{
    unsigned base = port ? 0x3e8 : 0x3f8;
    if (ed_windows) { hw_quiet(); return; }     /* mode invité : ports à Windows */
    port_out(base+1,0); port_out(base+3,0x80);
    port_out(base,divisor); port_out(base+1,0);
    port_out(base+3,3); port_out(base+4,11);
    port_out(base+2,0); /* État 8250 connu, y compris après Windows/16550. */
    uart_head[port] = uart_tail[port] = 0;
    uart_rxhead[port] = uart_rxtail[port] = 0;
    /* IRQ4 COM1 ; IRQ9 COM3, cascade IRQ2. Réception dès l'ouverture. */
    port_out(0x21,port_in(0x21)&~(port ? 4 : 16));
    if (port) port_out(0xa1,port_in(0xa1)&~2);
    port_out(base+1,1);
}
int hw_screen(int *cols, int *rows, int *x, int *y, WORD *cells)
{
    unsigned mode = bda[0x49], page = bda[0x62], off, i, n;
    WORD __far *v;
    if (mode > 3 && mode != 7) return 0;
    *cols = *(WORD __far *)(bda+0x4a); *rows = bda[0x84]+1;
    if (*rows == 1) *rows = 25;
    if (*cols < 40 || *cols > ED_COLS || *rows < 1 || *rows > ED_ROWS || page > 7) return 0;
    *x = bda[0x50+page*2]; *y = bda[0x51+page*2];
    if (*x >= *cols) *x = *cols-1; if (*y >= *rows) *y = *rows-1;
    off = *(WORD __far *)(bda+0x4e);
    n = (unsigned)(*cols * *rows);
    if ((unsigned long)off+n*2UL > 0x8000UL) return 0;
    v = (WORD __far *)MK_FP(mode == 7 ? 0xb000 : 0xb800,off);
    for (i = 0; i < n; ++i) cells[i] = v[i];
    return 1;
}
void hw_overlay(int row, const char *text)
{
    unsigned off,mode; int i,ended = 0;
    mode = bda[0x49]; off = *(WORD __far *)(bda+0x4e);
    if (saved_row < 0) {
        saved_row = row; saved_cols = ed_cols;
        saved_video = (WORD __far *)MK_FP(mode == 7 ? 0xb000 : 0xb800,off+row*ed_cols*2);
        for (i = 0; i < ed_cols; ++i) saved_cells[i] = saved_video[i];
    }
    for (i = 0; i < saved_cols; ++i) {
        if (!text[i]) ended = 1;
        saved_video[i] = 0x7000 | (ended ? ' ' : (BYTE)text[i]);
        if (ended) break;
    }
    while (++i < saved_cols) saved_video[i] = 0x7020;
}
void hw_restore(void)
{
    int i;
    if (saved_row < 0) return;
    /* Ne pas réécrire de mémoire vidéo si l'application a changé de mode. */
    if ((!ed_windows || ed_guest) && (bda[0x49] <= 3 || bda[0x49] == 7))
        for (i = 0; i < saved_cols; ++i) saved_video[i] = saved_cells[i];
    saved_row = -1;
}
void timer(void)
{
    if (armed) ed_tick();
}
/* Fichier texte, à côté de l'exécutable ; aucune écriture depuis le timer. */
static void path(void)
{
    BYTE __far *env; WORD envseg; int i,j,last = -1;
    envseg = *(WORD __far *)MK_FP(psp,0x2c);
    env = (BYTE __far *)MK_FP(envseg,0);
    for (i = 0; i < 32760 && (env[i] || env[i+1]); ++i) {}
    i += 4;
    for (j = 0; j < 110 && env[i+j]; ++j) {
        config_path[j] = (char)env[i+j]; if (env[i+j] == '\\' || env[i+j] == '/') last = j;
    }
    ++last;
    { const char *s = "EPAUDOS.INI"; j = 0; do { config_path[last+j] = s[j]; } while (s[j++]); }
}
static void load(void)
{
    int h,i,j,k,n,value; char buf[256];
    h = dos_open(config_path); if (h < 0) return;
    n = dos_read(h,buf,sizeof(buf)); dos_close(h);
    if (n < 0) return;
    for (i = 0; i < n; ++i) {
        if (i && buf[i-1] != '\n') continue;
        for (j = 0; j < ED_CFG; ++j) {
            for (k = 0; cfg_names[j][k] && i+k < n && cfg_names[j][k] == buf[i+k]; ++k) {}
            if (cfg_names[j][k] || i+k >= n || buf[i+k] != '=') continue;
            value = 0; ++k;
            while (i+k < n && buf[i+k] >= '0' && buf[i+k] <= '9' && value < 100)
                value = value*10+buf[i+k++]-'0';
            ed_cfg[j] = (BYTE)(value > 255 ? 255 : value);
        }
    }
}
static int save(const BYTE __far *cfg)
{
    int h,i,j,n,pos = 0; char buf[128];
    for (i = 0; i < ED_CFG; ++i) {
        for (j = 0; cfg_names[i][j]; ++j) buf[pos++] = cfg_names[i][j];
        buf[pos++] = '=';
        if (cfg[i] > 9) buf[pos++] = (char)('0'+cfg[i]/10);
        buf[pos++] = (char)('0'+cfg[i]%10); buf[pos++] = '\r'; buf[pos++] = '\n';
    }
    h = dos_create(config_path); if (h < 0) return 0;
    n = dos_write(h,buf,pos); dos_close(h); return n == pos;
}
void idle(void)
{
    unsigned caller; VECTOR critical; int ok;
    /* Mode invité : parole ou trame braille en attente d'Épaulard. Céder le reste
       de la tranche de temps de cette machine (INT 2Fh 1680h) : la machine système
       les relaie aussitôt, au lieu d'attendre son tour derrière une invite qui ne
       fait qu'attendre une touche (plein écran surtout). */
    if (ed_windows && ed_guest && ed_box.relay &&
        (uart_head[0] != uart_tail[0] || uart_head[1] != uart_tail[1]))
        mux(0x1680);
    /* INT 28h autorise les services DOS > 0Ch. Ne pas entrer pendant une
       erreur critique. Le PSP résident possède les handles temporaires. */
    if (ed_save != 1 || (ed_windows && !ed_guest) || *indos > 1 || *(indos-1)) return;
    caller = dos_psp(); critical = dos_vector(0x24);
    dos_setpsp(psp); dos_setvector(0x24,fail24);
    ok = save((const BYTE __far *)MK_FP(psp,(unsigned)ed_cfg));
    dos_setfarvector(0x24,critical); dos_setpsp(caller);
    ed_save = (BYTE)(ok ? 2 : 3);
}
void install(void)
{
    unsigned env; BYTE __far *args; unsigned long resident;
    psp = dos_psp(); path(); args = (BYTE __far *)MK_FP(psp,0x80);
    resident = mux(0xed00);
    if ((unsigned)resident == 0xedff) {
        if (args[0] >= 5 && args[2] == '/' && (args[3] == 'S' || args[3] == 's')) {
            resident = mux(0xed01);
            dos_print(save((BYTE __far *)resident) ? "Réglages enregistrés.\r\n$" : "Échec de sauvegarde.\r\n$");
        } else dos_print("Épaudos est déjà résident. /SAVE : enregistrer les réglages.\r\n$");
        return;
    }
    if (args[0]) {
        dos_print("Épaudos 0.1 : lancer sans argument. Après réglages : EPAUDOS /SAVE.\r\n$"); return;
    }
    if ((unsigned)mux(0x1600) & 0x7f) {
        dos_print("Lancer Épaudos avant Windows.\r\n$"); return;
    }
    load(); indos = dos_indos();
    ed_box.head = (WORD)&uart_head[0]; ed_box.tail = (WORD)&uart_tail[0];
    ed_box.buf = (WORD)uart_buf;
    ed_box.rxhead = (WORD)&uart_rxhead[0]; ed_box.rxtail = (WORD)&uart_rxtail[0];
    ed_box.rxbuf = (WORD)uart_rxbuf;
    old15 = dos_vector(0x15); old08 = dos_vector(0x08);
    old28 = dos_vector(0x28); old2f = dos_vector(0x2f);
    old0c = dos_vector(0x0c); old71 = dos_vector(0x71);
    dos_setvector(0x0c,isr0c); dos_setvector(0x71,isr71);
    dos_setvector(0x15,isr15); dos_setvector(0x08,isr08);
    dos_setvector(0x28,isr28); dos_setvector(0x2f,isr2f);
    ed_init(); armed = 1;
    dos_print("Épaudos résident. Inser+H : aide ; Inser+E : réglages.\r\n$");
    env = *(WORD __far *)MK_FP(psp,0x2c);
    *(WORD __far *)MK_FP(psp,0x2c) = 0; if (env) dos_free(env);
    keep();
}
