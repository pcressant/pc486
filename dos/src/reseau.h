/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC486_RESEAU_H
#define PC486_RESEAU_H
/* Applique la section [pc486.reseau] de in (n octets) dans out : valeurs « avec
   carte » si on, sinon « sans carte ». Renvoie la longueur écrite, -1 si la section
   manque ou si out (cap octets) est trop petit. Fonction pure, testée sous Linux. */
int reseau_bascule(const char *in, int n, char *out, int cap, int on);
#endif
