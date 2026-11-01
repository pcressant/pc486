/* SPDX-License-Identifier: GPL-3.0-or-later
 * S3PROBE.EXE : mesures du résident S3RES tel que les lecteurs d'écran l'utilisent.
 *
 * Client de l'API de S3RES (INT 2Fh D300h/D301h), comme Épaudos : octets Apollo II,
 * Ctrl-X, échos @E. Mesure, en ticks PIT (1 193 182 Hz) puis en microsecondes :
 *   - demande -> DMA lancé (premier son possible) et -> écho de fin de lecture ;
 *   - durée de la parole produite, pour comparer l'écho au temps réel ;
 *   - Ctrl-X -> DSP arrêté par la pompe, et nouvelle demande -> DMA relancé ;
 *   - part du processeur prise par le résident (boucle de premier plan comptée
 *     au repos puis pendant la parole) ;
 *   - sous-alimentations pendant une activité de premier plan soutenue (écritures
 *     disque par DOS, copies mémoire).
 * Le premier son « possible » n'est pas la latence acoustique : le DMA vient de
 * démarrer sur le premier bloc, que la carte convertit ensuite.
 * Sortie : lignes clé=valeur (synthe3/banc86box.py). Aucun calcul flottant.
 */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>
#include "../src/s3time.h"
#include "../src/ap.h"
#include "../src/s3res.h"

#define PIT 1193182UL

static void __far *entry;
static RES_INFO __far *ri;
static RES_SB __far *sb;
static AP_STATS __far *st;
static char text[400];

static unsigned call(unsigned char fn, unsigned char value)
{
    void __far *e = entry;
    unsigned r = 0;
    _asm {
        mov al, fn
        mov bl, value
        call dword ptr e
        mov r, ax
    }
    return r;
}

static unsigned long us(unsigned long ticks)
{
    return ticks / 1193UL * 1000UL + (ticks % 1193UL) * 1000UL / 1193UL;
}

static void say(const char *s)
{
    while (*s)
        if (call(0, (unsigned char)*s)) s++;
}

static int echo_wait(int mark, unsigned long limit, unsigned long *at)
{
    unsigned long t0 = s3_ticks();
    unsigned r;
    while (s3_ticks() - t0 < limit) {
        r = call(1, 0);
        if (r != 0xffff && (int)(r & 0xff) == mark) { *at = s3_ticks(); return 1; }
    }
    return 0;
}

static void quiet(void)
{
    unsigned long t0 = s3_ticks();
    call(0, 0x18);
    while (sb->flags && s3_ticks() - t0 < 2 * PIT)
        ;
    while (call(1, 0) != 0xffff)
        ;
}

/* Itérations d'une boucle de calcul pendant ticks. */
static unsigned long spin(unsigned long ticks)
{
    unsigned long t0 = s3_ticks(), n = 0;
    volatile unsigned x = 1;
    unsigned i;
    while (s3_ticks() - t0 < ticks) {
        for (i = 0; i < 64; i++) x = x * 3 + i;
        n++;
    }
    return n;
}

static char block[4096];

/* Activité soutenue : écritures de 4 Kio par DOS (INT 21h, disque) et copies. */
static unsigned long churn(unsigned long ticks)
{
    unsigned long t0 = s3_ticks(), n = 0;
    int h;
    unsigned w;
    if (_dos_creat("C:\\S3PROBE.TMP", _A_NORMAL, &h)) return 0;
    while (s3_ticks() - t0 < ticks) {
        _dos_write(h, block, sizeof(block), &w);
        _fmemmove(block + 1, block, sizeof(block) - 1);
        if (++n % 16 == 0) { _dos_close(h); _dos_creat("C:\\S3PROBE.TMP", _A_NORMAL, &h); }
    }
    _dos_close(h);
    remove("C:\\S3PROBE.TMP");
    return n;
}

int main(int argc, char **argv)
{
    union REGS r;
    struct SREGS s;
    FILE *f;
    unsigned long t0, t_dma, t_echo, base, load, samples, n, cut_t, under;
    unsigned mcb_seg, paras, cuts;
    int len;

    if (!s3_clock_init()) { puts("erreur=horloge"); return 1; }
    memset(&s, 0, sizeof(s));
    r.w.ax = 0xd300; r.w.bx = 0; r.w.di = 0;
    int86x(0x2f, &r, &r, &s);
    if (r.w.ax != 0xd3ff) { puts("erreur=S3RES absent"); return 1; }
    entry = MK_FP(s.es, r.w.di);
    r.w.ax = 0xd301;
    int86x(0x2f, &r, &r, &s);
    ri = MK_FP(s.es, r.w.di);
    sb = MK_FP(ri->dgroup, ri->off_sb);
    st = MK_FP(ri->dgroup, ri->off_stats);
    strcpy(text, "Bonjour Michel. Un eleve prend un cafe.");
    if (argc > 1 && (f = fopen(argv[1], "rb")) != NULL) {
        len = (int)fread(text, 1, sizeof(text) - 1, f);
        text[len] = 0;
        fclose(f);
        while (len && (text[len - 1] == '\r' || text[len - 1] == '\n')) text[--len] = 0;
    }
    /* Mémoire : bloc du programme résident (MCB du PSP) et anneau DMA. */
    mcb_seg = ri->psp - 1;
    paras = *(unsigned __far *)MK_FP(mcb_seg, 3);
    printf("resident_octets=%lu dma_octets=%u etat=%u dsp=A%X_I%u_D%u\n",
           (unsigned long)paras * 16, RES_BANKS * RES_BANK, ri->state, ri->base, ri->irq, ri->dma);

    quiet();
    base = spin(2 * PIT);
    printf("boucle_repos=%lu\n", base);

    /* 1. Demande depuis le silence : DMA lancé, écho de fin, durée produite. */
    samples = st->samples;
    under = sb->underruns;
    t0 = s3_ticks();
    say(text);
    say("\r@EA\r");
    while (!(sb->flags & RES_RUNNING) && s3_ticks() - t0 < 5 * PIT)
        ;
    t_dma = s3_ticks();
    if (!echo_wait('A', 30 * PIT, &t_echo)) { puts("erreur=echo A absent"); return 1; }
    samples = st->samples - samples;
    printf("demande_dma_us=%lu pompe_premier_bloc_us=%lu pompe_soumission_us=%lu attente_pompe_ticks_bios=%u\n",
           us(t_dma - t0), us(ri->first_buffer), us(ri->submit), ri->wait_ticks);
    printf("demande_echo_us=%lu parole_us=%lu sous_alimentations=%u\n",
           us(t_echo - t0), samples * 100UL, sb->underruns - under);

    /* 2. Charge : la même boucle pendant une parole longue. */
    quiet();
    for (n = 0; n < 4; n++) { say(text); say("\r"); }
    say("@EB\r");
    t0 = s3_ticks();
    while (!(sb->flags & RES_RUNNING) && s3_ticks() - t0 < 5 * PIT)
        ;
    load = spin(2 * PIT);
    printf("boucle_parole=%lu charge_pour_mille=%lu\n", load,
           base ? (base - (load < base ? load : base)) * 1000UL / base : 0);

    /* 3. Coupure : Ctrl-X pendant la parole, puis reprise. */
    t0 = s3_ticks();
    while (sb->played < 4000 && s3_ticks() - t0 < 5 * PIT)
        ;
    cuts = ri->cut_stops;
    t0 = s3_ticks();
    call(0, 0x18);
    n = s3_ticks();
    while (sb->flags && s3_ticks() - t0 < 2 * PIT)
        ;
    cut_t = s3_ticks();
    printf("ctrlx_dma_masque_us=%lu ctrlx_dsp_arrete_us=%lu arrets_coupure=%u\n",
           us(n - t0), us(cut_t - t0), ri->cut_stops - cuts);
    t0 = s3_ticks();
    say("Reprise.\r@EC\r");
    while (!(sb->flags & RES_RUNNING) && s3_ticks() - t0 < 5 * PIT)
        ;
    printf("reprise_dma_us=%lu\n", us(s3_ticks() - t0));
    if (!echo_wait('C', 30 * PIT, &t_echo)) { puts("erreur=echo C absent"); return 1; }
    while (call(1, 0) != 0xffff)
        ;

    /* 4. Activité soutenue : disque et mémoire au premier plan pendant la parole. */
    under = sb->underruns;
    for (n = 0; n < 6; n++) { say(text); say("\r"); }
    say("@ED\r");
    t0 = s3_ticks();
    n = churn(6 * PIT);
    printf("activite_blocs_4k=%lu sous_alimentations_activite=%u\n", n, sb->underruns - under);
    if (!echo_wait('D', 60 * PIT, &t_echo)) { puts("erreur=echo D absent"); return 1; }

    printf("pompes=%lu pompe_max_us=%lu pompe_cumul_us=%lu pile_marge=%u horloge_fautes=%u "
           "refus=%u marques_perdues=%u octets_refuses=%u\n",
           ri->pumps, us(ri->busy_max), us(ri->busy), ri->stack_low, ri->clock_fault,
           st->refused, st->lost_marks, ((AP_LINK __far *)MK_FP(ri->dgroup, ri->off_link))->dropped);
    puts("S3PROBE=fin");
    return 0;
}
