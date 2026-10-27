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

extern volatile BYTE ed_windows, ed_disabled, ed_save, ed_resume;
extern BYTE ed_cfg[ED_CFG];
extern char ed_line[ED_TEXT];
extern int ed_cols, ed_rows, ed_x, ed_y, ed_flat;
void ed_init(void);
void ed_tick(void);
int ed_scan(BYTE scan, BYTE shifts);
void ed_suspend(void);
#endif
