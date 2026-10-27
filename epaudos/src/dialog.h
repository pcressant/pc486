/* SPDX-License-Identifier: GPL-3.0-or-later
 * Boîtes de dialogue CUA en mode texte (bibliothèque « Character Windows » de
 * Microsoft : Word 6 pour DOS, EDIT, QBasic, DOSSHELL...). Analyse pure de la
 * mémoire texte autour du curseur : aucun état, aucun appel DOS, réutilisable
 * pour toute application qui dessine les mêmes contrôles.
 */
#ifndef EPAUDOS_DIALOG_H
#define EPAUDOS_DIALOG_H
#include "core.h"

#define DK_NONE 0
#define DK_FIELD 1  /* [texte·····] (Word) ou zone encadrée (EDIT) */
#define DK_CHECK 2  /* [X] */
#define DK_RADIO 3  /* (*) : puce 07h entre les parenthèses */
#define DK_BUTTON 4 /* cadre de 3 rangées (Word) ou < OK > (EDIT) */
#define DK_LIST 5   /* cadre de plus de 3 rangées ; l'élément est sous le curseur */

typedef struct {
    BYTE kind, state;             /* state : case cochée, bouton radio choisi */
    int x0, x1, y;                /* identité ; pour une liste, son cadre */
    int left, top, right, bottom; /* intérieur de la boîte */
    int gx, gy;                   /* cadre de groupe, -1 sans titre de groupe */
    WORD id;                      /* empreinte de la boîte : titre et position */
    char title[48], label[64], value[64], group[40];
} ED_DIALOG;

/* 1 si le curseur (x, y) est sur un contrôle d'une boîte reconnue. */
int dlg_find(const WORD *cells, int cols, int rows, int x, int y, ED_DIALOG *d);
/* Texte libre de la boîte (message d'une boîte d'alerte), rangées sans contrôle. */
void dlg_static(const WORD *cells, int cols, int rows, const ED_DIALOG *d, char *out, int cap);
#endif
