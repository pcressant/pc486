/* SPDX-License-Identifier: GPL-3.0-or-later */
/* RESEAU.EXE : active ou neutralise le réseau de Windows pour Workgroups selon la
   présence de la carte NE2000 (0x300), avant WIN.

   Sans carte, WfW configuré pour le réseau s'arrête sur « Votre adaptateur réseau
   MS$NE2CLONE ne fonctionne pas correctement ». La section [pc486.reseau] de
   SYSTEM.INI, écrite par scripts/options-vm, liste les lignes concernées :

       [pc486.reseau]
       386Enh/netcard=ne2000.386|-

   « section/clé=valeur avec carte|valeur sans carte » ; « - » sans carte retire la
   ligne. Le fichier n'est réécrit que si une ligne change : sans carte, SYSTEM.INI
   redevient celui d'avant la configuration du réseau.

   RESEAU [AUTO|OUI|NON] [fichier]   (défaut : AUTO C:\WINDOWS\SYSTEM.INI)
   Code de retour : 0 sans carte, 1 avec carte, 2 erreur (fichier inchangé). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reseau.h"

#define MAXENT 24
#define MAXLIN 256

typedef struct {
    char sec[40], key[40];
    char on[MAXLIN], off[MAXLIN];
    int done;
} ENTRY;

static ENTRY ent[MAXENT];
static int nent;

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same(const char *a, int na, const char *b)
{
    int i;
    for (i = 0; i < na; i++)
        if (!b[i] || lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return b[na] == 0;
}

/* Ligne [debut, fin) sans fin de ligne ; espaces de tête et de queue ignorés. */
static void trim(const char **p, const char **e)
{
    while (*p < *e && (**p == ' ' || **p == '\t')) ++*p;
    while (*e > *p && ((*e)[-1] == ' ' || (*e)[-1] == '\t')) --*e;
}

/* « [nom] » : nom dans sec ; 1 si c'est un titre de section. */
static int header(const char *p, const char *e, const char **s, int *n)
{
    trim(&p, &e);
    if (e - p < 2 || *p != '[') return 0;
    ++p;
    *s = p;
    while (p < e && *p != ']') ++p;
    if (p >= e) return 0;
    *n = (int)(p - *s);
    return 1;
}

/* « clé=valeur » : longueur de la clé, 0 pour un commentaire ou une ligne sans « = ». */
static int keyof(const char *p, const char *e, const char **k)
{
    const char *q;
    trim(&p, &e);
    if (p >= e || *p == ';') return 0;
    for (q = p; q < e && *q != '='; q++) ;
    if (q >= e) return 0;
    *k = p;
    while (q > p && (q[-1] == ' ' || q[-1] == '\t')) --q;
    return (int)(q - p);
}

static void copy(char *d, const char *s, int n, int cap)
{
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

/* Lit [pc486.reseau] ; 0 si absente ou mal formée. */
static int entries(const char *in, int n)
{
    const char *p = in, *end = in + n, *e, *s, *k, *v, *bar, *slash;
    int inside = 0, ns, nk;
    nent = 0;
    while (p < end) {
        for (e = p; e < end && *e != '\n'; e++) ;
        s = e;
        if (s > p && s[-1] == '\r') --s;
        if (header(p, s, &k, &ns))
            inside = same(k, ns, "pc486.reseau");
        else if (inside && (nk = keyof(p, s, &k)) != 0) {
            for (slash = k; slash < k + nk && *slash != '/'; slash++) ;
            for (v = k; *v != '='; v++) ;
            ++v;
            for (bar = v; bar < s && *bar != '|'; bar++) ;
            if (slash >= k + nk || bar >= s || nent >= MAXENT) return 0;
            copy(ent[nent].sec, k, (int)(slash - k), sizeof ent[0].sec);
            copy(ent[nent].key, slash + 1, (int)(k + nk - slash - 1), sizeof ent[0].key);
            copy(ent[nent].on, v, (int)(bar - v), MAXLIN);
            copy(ent[nent].off, bar + 1, (int)(s - bar - 1), MAXLIN);
            ent[nent].done = 0;
            nent++;
        }
        p = e < end ? e + 1 : e;
    }
    return nent;
}

static int put(char *out, int *o, int cap, const char *s, int n)
{
    if (*o + n > cap) return 0;
    memcpy(out + *o, s, n);
    *o += n;
    return 1;
}

static const char *value(const ENTRY *x, int on)
{
    return on ? x->on : x->off;
}

/* Ajoute les entrées de la section sec (nsec octets) non encore rencontrées. */
static int flush(char *out, int *o, int cap, const char *sec, int nsec, int on, const char *nl)
{
    int i;
    for (i = 0; i < nent; i++) {
        const char *v = value(&ent[i], on);
        if (ent[i].done || !same(sec, nsec, ent[i].sec)) continue;
        ent[i].done = 1;
        if (!strcmp(v, "-")) continue;
        if (!put(out, o, cap, ent[i].key, (int)strlen(ent[i].key)) || !put(out, o, cap, "=", 1)
            || !put(out, o, cap, v, (int)strlen(v)) || !put(out, o, cap, nl, (int)strlen(nl)))
            return 0;
    }
    return 1;
}

int reseau_bascule(const char *in, int n, char *out, int cap, int on)
{
    const char *p = in, *end = in + n, *e, *s, *k, *sec = "", *nl = "\r\n";
    const char *pend = 0;       /* lignes vides en attente, rendues après les ajouts */
    int o = 0, nsec = 0, nk, i, ns;
    if (!entries(in, n)) return -1;
    for (e = in; e < end && *e != '\n'; e++) ;
    if (e < end && (e == in || e[-1] != '\r')) nl = "\n";
    while (p < end) {
        for (e = p; e < end && *e != '\n'; e++) ;
        s = e;
        if (s > p && s[-1] == '\r') --s;
        if (e < end) ++e;
        if (header(p, s, &k, &ns)) {
            if (!flush(out, &o, cap, sec, nsec, on, nl)) return -1;
            if (pend && !put(out, &o, cap, pend, (int)(p - pend))) return -1;
            pend = 0;
            sec = k; nsec = ns;
        } else if (p == s || (s - p == 1 && *p == 0x1a)) {
            if (!pend) pend = p;            /* vide ou fin de fichier DOS */
            p = e;
            continue;
        } else if ((nk = keyof(p, s, &k)) != 0) {
            for (i = 0; i < nent; i++)
                if (same(sec, nsec, ent[i].sec) && same(k, nk, ent[i].key) && !ent[i].done)
                    break;
            if (i < nent) {
                const char *v = value(&ent[i], on);
                ent[i].done = 1;
                if (pend && !put(out, &o, cap, pend, (int)(p - pend))) return -1;
                pend = 0;
                if (strcmp(v, "-")
                    && (!put(out, &o, cap, k, nk) || !put(out, &o, cap, "=", 1)
                        || !put(out, &o, cap, v, (int)strlen(v))
                        || !put(out, &o, cap, s, (int)(e - s))))
                    return -1;
                p = e;
                continue;
            }
        }
        if (pend && !put(out, &o, cap, pend, (int)(p - pend))) return -1;
        pend = 0;
        if (!put(out, &o, cap, p, (int)(e - p))) return -1;
        p = e;
    }
    if (!flush(out, &o, cap, sec, nsec, on, nl)) return -1;
    /* Sections absentes : ajoutées à la fin, avant les lignes vides finales. */
    for (i = 0; i < nent; i++) {
        const char *v = value(&ent[i], on);
        int j;
        if (ent[i].done || !strcmp(v, "-")) continue;
        if (o && out[o - 1] != '\n' && !put(out, &o, cap, nl, (int)strlen(nl))) return -1;
        if (!put(out, &o, cap, "[", 1) || !put(out, &o, cap, ent[i].sec, (int)strlen(ent[i].sec))
            || !put(out, &o, cap, "]", 1) || !put(out, &o, cap, nl, (int)strlen(nl)))
            return -1;
        for (j = i; j < nent; j++)
            if (!ent[j].done && !strcmp(ent[j].sec, ent[i].sec)) {
                ent[j].done = 1;
                v = value(&ent[j], on);
                if (!strcmp(v, "-")) continue;
                if (!put(out, &o, cap, ent[j].key, (int)strlen(ent[j].key)) || !put(out, &o, cap, "=", 1)
                    || !put(out, &o, cap, v, (int)strlen(v)) || !put(out, &o, cap, nl, (int)strlen(nl)))
                    return -1;
            }
    }
    if (pend && !put(out, &o, cap, pend, (int)(end - pend))) return -1;
    return o;
}

#ifndef RESEAU_TEST
#include <conio.h>
#include <dos.h>

#define BASE 0x300
#define MAXINI 24000

/* NE2000 (DP8390) : le registre de commande relit ce qu'on y écrit ; sans carte,
   le bus ISA rend FFh. Page 0 puis page 1, carte arrêtée, DMA distant interrompu. */
static int ne2000(void)
{
    int a, b;
    outp(BASE, 0x21);
    a = inp(BASE);
    outp(BASE, 0x61);
    b = inp(BASE);
    outp(BASE, 0x21);
    return (a & 0xff) != 0xff && (a & 0xe3) == 0x21 && (b & 0xe3) == 0x61;
}

static int load(const char *name, char *buf, int cap)
{
    FILE *f = fopen(name, "rb");
    int n;
    if (!f) return -1;
    n = (int)fread(buf, 1, cap, f);
    if (n >= cap || ferror(f)) n = -1;
    fclose(f);
    return n;
}

static int save(const char *name, const char *buf, int n)
{
    char tmp[128];
    char *dot;
    FILE *f;
    if (strlen(name) + 1 > sizeof tmp) return 0;
    strcpy(tmp, name);
    dot = strrchr(tmp, '.');
    if (dot && !strchr(dot, '\\')) strcpy(dot, ".$$$");
    else strcat(tmp, ".$$$");
    f = fopen(tmp, "wb");
    if (!f) return 0;
    if ((int)fwrite(buf, 1, n, f) != n || fclose(f)) {
        remove(tmp);
        return 0;
    }
    if (remove(name) || rename(tmp, name)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *file = "C:\\WINDOWS\\SYSTEM.INI";
    char *in, *out;
    int mode = -1, i, n, m, on;
    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "AUTO")) mode = -1;
        else if (!stricmp(argv[i], "OUI")) mode = 1;
        else if (!stricmp(argv[i], "NON")) mode = 0;
        else if (argv[i][0] == '/' || argv[i][0] == '-') {
            puts("RESEAU [AUTO|OUI|NON] [fichier] : réseau de Windows selon la carte NE2000 (300h).");
            return 2;
        } else file = argv[i];
    }
    on = mode < 0 ? ne2000() : mode;
    in = malloc(MAXINI);
    out = malloc(MAXINI);
    if (!in || !out) {
        puts("RESEAU : mémoire insuffisante.");
        return 2;
    }
    n = load(file, in, MAXINI);
    if (n < 0) {
        printf("RESEAU : lecture impossible de %s.\n", file);
        return 2;
    }
    m = reseau_bascule(in, n, out, MAXINI, on);
    if (m < 0) {
        printf("RESEAU : section [pc486.reseau] absente ou fichier trop long : %s inchangé.\n", file);
        return 2;
    }
    if (m != n || memcmp(in, out, n)) {
        if (!save(file, out, m)) {
            printf("RESEAU : écriture impossible de %s.\n", file);
            return 2;
        }
        puts(on ? "Réseau de Windows activé (carte NE2000 en 300h)."
                : "Réseau de Windows désactivé (aucune carte NE2000).");
    }
    return on;
}
#endif
