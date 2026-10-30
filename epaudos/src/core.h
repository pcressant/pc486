/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef EPAUDOS_CORE_H
#define EPAUDOS_CORE_H
typedef unsigned char BYTE;
typedef unsigned short WORD;
#define ED_COLS 132
#define ED_ROWS 50
#define ED_CELLS (ED_COLS * ED_ROWS)
#define ED_TEXT 256
#define ED_CFG 7

/* Interface matérielle : aucune fonction du moteur n'appelle DOS. */
int hw_tx(int port, BYTE value);
int hw_rx(int port);
void hw_serial(int port, int divisor);
int hw_screen(int *cols, int *rows, int *x, int *y, WORD *cells);
void hw_overlay(int row, const char *text);
void hw_restore(void);
void hw_quiet(void);
WORD hw_lock(void);
void hw_unlock(WORD flags);
/* Sous Windows en mode amélioré (ed_windows) : ED_SLEEP, dormir (machine système,
   aucun relais d'Épaulard) ; ED_RUN, lire cette machine DOS (mode invité, parole
   relayée par Épaulard) ; ED_SKIP, autre machine DOS que celle suivie : ne rien
   faire sans oublier l'état. key : appelé pour une touche reçue par cette machine. */
#define ED_SLEEP 0
#define ED_RUN 1
#define ED_SKIP 2
int hw_guest(int key);
/* Boîte aux lettres partagée avec Épaulard (dos.c, epaulard/src/dosbox.c) :
   offsets dans le segment d'Épaudos, renvoyé en ES par INT 2Fh AX=ED02h. */
typedef struct {
    char sig[4];                /* « EPBX » */
    BYTE version;               /* 2 : files du braille (port 1) relayées */
    volatile BYTE relay;        /* Épaulard : relais actif (fenêtre DOS au premier plan) */
    volatile BYTE active;       /* Épaudos : mode invité en marche */
    BYTE reserved;
    volatile WORD vm;           /* machine DOS suivie (0 : la prochaine qui tourne) */
    volatile WORD alive;        /* compteur d'Épaulard, au moins toutes les secondes */
    WORD head, tail, buf;       /* file de parole vers COM1 : uart_head[0]... ; port 1
                                   (Baum) : indices à head+1, tail+1, données à buf+256 */
    WORD rxhead, rxtail, rxbuf; /* échos de l'Apollo II vers Épaudos ; touches Baum :
                                   rxhead+1, rxtail+1, rxbuf+256 */
    volatile WORD keys;         /* version 2 : touches reçues par la machine suivie */
} EDBOX;
extern EDBOX ed_box;

extern volatile BYTE ed_windows, ed_disabled, ed_save, ed_resume, ed_guest;
extern BYTE ed_cfg[ED_CFG];
extern char ed_line[ED_TEXT];
extern int ed_cols, ed_rows, ed_x, ed_y, ed_flat;
void ed_init(void);
void ed_tick(void);
int ed_scan(BYTE scan, BYTE shifts);
void ed_suspend(void);
#endif
