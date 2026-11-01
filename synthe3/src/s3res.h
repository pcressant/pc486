/* SPDX-License-Identifier: GPL-3.0-or-later
 * S3RES.EXE : Synthé3 résident pour DOS réel, parole sur Sound Blaster.
 * Dispositions partagées avec s3res.asm (offsets vérifiés à la compilation) et lues
 * par les essais à travers RES_INFO (signature « S3RESINF », segment DGROUP). */
#ifndef PC486_S3RES_H
#define PC486_S3RES_H

#define RES_BANK 512            /* échantillons par bloc, 51,2 ms à 10 kHz */
#define RES_BANKS 4             /* anneau DMA de 2048 octets, auto-initialisé */
#define RES_IDLE 6              /* blocs de silence avant arrêt du DSP (~0,3 s) */

/* res_sb.flags */
#define RES_RUNNING 1           /* DMA et DSP en marche, IRQ attendue */
#define RES_HALTED 2            /* Ctrl-X : DMA masqué par l'écrivain, arrêt à finir */

typedef struct {
    unsigned base, channel;
    volatile unsigned flags, playing;
    volatile unsigned ready[RES_BANKS], used[RES_BANKS];
    volatile unsigned long played;  /* échantillons de parole joués, silence exclu */
    volatile unsigned completed;    /* IRQ de fin de bloc */
    volatile unsigned silent;       /* blocs de silence consécutifs insérés par l'IRQ */
    volatile unsigned underruns;    /* bloc manquant alors qu'une proposition était en calcul */
    volatile unsigned speaking;     /* la pompe a encore de la parole à fournir */
    unsigned dma_off, dma_seg;      /* anneau DMA, mémoire conventionnelle basse */
    unsigned irq;
    volatile unsigned windows;      /* Windows en cours (INT 2Fh 1605h..1606h) : silence */
    volatile unsigned arrive;       /* tick BIOS du premier octet reçu file vide */
    volatile unsigned arrived;      /* écrivain : 1 ; pompe : remis à 0 */
} RES_SB;
extern RES_SB res_sb;

/* res_info.state */
#define RES_UNAVAILABLE 1       /* vecteur IRQ repris par un programme : parole non jouée */
#define RES_DSP_ERROR 2         /* le DSP n'a pas répondu au dernier démarrage */

typedef struct {
    char sig[8];                /* « S3RESINF » */
    unsigned version;
    unsigned dgroup, psp, dma_block;
    unsigned base, irq, dma;
    unsigned state;
    unsigned off_sb, off_link, off_stats, off_trace;
    unsigned long pumps;        /* passages de la pompe (INT 8h) */
    unsigned long busy;         /* ticks PIT passés dans la pompe, cumulés */
    unsigned long busy_max;     /* plus long passage */
    unsigned starts, idle_stops, cut_stops, skipped;  /* skipped : blocs non joués */
    unsigned stack_low;         /* plus petite marge de pile observée, octets */
    unsigned clock_fault;
    /* Dernière demande de parole partie d'un silence : ticks PIT depuis le passage
       de la pompe qui l'a vue, et ticks BIOS d'attente de ce passage. */
    unsigned long first_buffer, submit, first_buffer_max, submit_max;
    unsigned wait_ticks, wait_max, requests;
    unsigned clients;           /* lecteurs attachés (AL=3) : pas de retrait à chaud */
    void __far *isr[3];         /* INT 8h, INT 2Fh, IRQ : vecteurs posés et précédents */
    void __far *old[3];
    unsigned vector_irq, pic_was_masked;
} RES_INFO;
extern RES_INFO res_info;
#endif
