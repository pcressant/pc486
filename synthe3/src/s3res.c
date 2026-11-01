/* SPDX-License-Identifier: GPL-3.0-or-later
 * S3RES.EXE : Synthé3 résident pour DOS réel (486SX/386 sans coprocesseur),
 * parole sur Sound Blaster, interface d'octets Apollo II pour Épaudos.
 *
 *   S3RES [/A220] [/I5] [/D1]   charger (défaut : variable BLASTER), sauf si
 *                               EPAUDOS.INI, à côté de S3RES.EXE, contient Synthese=Apollo
 *   S3RES /F ...                charger quel que soit EPAUDOS.INI
 *   S3RES /U                    retirer, si aucun lecteur n'est attaché
 *
 * La pompe (res_pump) est appelée par INT 8h après le BIOS, pile privée,
 * interruptions permises, jamais réentrante : elle applique les Ctrl-X, calcule
 * au plus trois blocs de 512 échantillons, les confie à l'anneau DMA, démarre le
 * DSP et rend les échos @E dont la position a été jouée (IRQ de fin de bloc).
 * Aucun appel DOS ni BIOS, aucune allocation, aucun flottant après l'installation.
 */
#include "s3.h"
#include "ap.h"
#include "s3res.h"
#include "s3time.h"
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RES_STACK 1536          /* = STACK_SIZE de s3res.asm */
#define RES_BUDGET 3

RES_SB res_sb;
RES_INFO res_info = { {'S','3','R','E','S','I','N','F'}, 0x0100 };
extern unsigned char res_stack[RES_STACK];
/* Dans le segment de code de s3res.asm : __far obligatoire, sinon le compilateur
   (modèle large, petite donnée) les supposerait dans DGROUP. */
extern void (__interrupt __far * __far res_old08)(void);
extern void (__interrupt __far * __far res_old2f)(void);
extern void (__interrupt __far * __far res_oldirq)(void);
extern void __interrupt __far res_isr08(void);
extern void __interrupt __far res_isr2f(void);
extern void __interrupt __far res_irq(void);
extern void __far res_api(void);
extern unsigned _STACKTOP;
void __cdecl s3_unsigned8(const short *, unsigned char *, unsigned);
void __cdecl s3_dma8(unsigned, unsigned long, unsigned, unsigned);

/* Dispositions lues par s3res.asm et par les essais. */
typedef char res_layout[(offsetof(RES_SB, ready) == 8 && offsetof(RES_SB, played) == 24 &&
    offsetof(RES_SB, speaking) == 34 && offsetof(RES_SB, dma_off) == 36 &&
    offsetof(RES_SB, irq) == 40 && offsetof(RES_SB, arrived) == 46 &&
    offsetof(AP_LINK, cut_at) == 6 && offsetof(AP_LINK, dropped) == 12 &&
    offsetof(AP_LINK, echo) == 14 && offsetof(AP_LINK, buf) == 46 && AP_INPUT == 1024 && offsetof(RES_INFO, state) == 22 &&
    offsetof(RES_INFO, clients) == 78 && offsetof(RES_INFO, isr[2]) == 88 &&
    offsetof(RES_INFO, vector_irq) == 104 &&
    offsetof(RES_INFO, pic_was_masked) == 106) ? 1 : -1];

static short pcm[RES_BANK];
static unsigned staged, queued, was_running, measuring, measured;
static unsigned char __far *dma;
static unsigned long physical, seen_at;

static int dsp_write(unsigned v)
{
    unsigned n = 65535;
    while (inp(res_sb.base + 12) & 128) if (!--n) return 0;
    outp(res_sb.base + 12, v);
    return 1;
}
static int dsp_read(void)
{
    unsigned n = 65535;
    while (!(inp(res_sb.base + 14) & 128)) if (!--n) return -1;
    return inp(res_sb.base + 10);
}
/* Remise à zéro : >= 3 µs à 1, puis AAh en moins de 100 µs (attente bornée). */
static int dsp_reset(void)
{
    int i;
    outp(res_sb.base + 6, 1);
    for (i = 0; i < 8; i++) inp(res_sb.base + 6);
    outp(res_sb.base + 6, 0);
    return dsp_read() == 0xaa;
}
static void pic_unmask(void)
{
    unsigned irq = res_sb.irq;
    _disable();
    if (irq < 8) outp(0x21, inp(0x21) & ~(1 << irq));
    else { outp(0xa1, inp(0xa1) & ~(1 << (irq - 8))); outp(0x21, inp(0x21) & ~4); }
    _enable();
}
static unsigned long played(void)
{
    unsigned long p;
    _disable(); p = res_sb.played; _enable();
    return p;
}
/* Tout arrêter et oublier l'anneau (Ctrl-X, carte reprise par un programme). */
static void halt(int reset_dsp)
{
    int i;
    _disable();
    if (res_sb.flags) outp(0x0a, res_sb.channel | 4);
    res_sb.flags = 0; res_sb.playing = 0; res_sb.silent = 0;
    for (i = 0; i < RES_BANKS; i++) res_sb.ready[i] = res_sb.used[i] = 0;
    res_sb.played = 0;
    _enable();
    if (reset_dsp) dsp_reset();
    queued = was_running = 0;
}
/* Bloc suivant de l'anneau, dans l'ordre de lecture, sous exclusion : l'IRQ peut
   y écrire du silence au même instant si la lecture l'atteint. */
static int commit(unsigned n)
{
    unsigned k, target, i;
    unsigned char __far *bank;
    _disable();
    if (res_sb.flags & RES_HALTED) { _enable(); return 0; }
    if (res_sb.flags & RES_RUNNING) {
        for (k = 0; k < RES_BANKS - 1 && res_sb.ready[(res_sb.playing + 1 + k) & 3]; k++) {}
        if (k == RES_BANKS - 1) { _enable(); return 0; }
        target = (res_sb.playing + 1 + k) & 3;
    } else {
        if (queued == RES_BANKS) { _enable(); return 0; }
        target = queued++;
    }
    bank = dma + target * RES_BANK;
    s3_unsigned8(pcm, bank, n);
    for (i = n; i < RES_BANK; i++) bank[i] = 128;
    res_sb.used[target] = n;
    res_sb.ready[target] = 1;
    _enable();
    return 1;
}
static int start(void)
{
    unsigned i;
    if (!dsp_reset() || !dsp_write(0xd1) || !dsp_write(0x40) || !dsp_write(156) ||
        !dsp_write(0x48) || !dsp_write((RES_BANK - 1) & 255) || !dsp_write((RES_BANK - 1) >> 8)) {
        res_info.state |= RES_DSP_ERROR;
        return 0;
    }
    res_info.state &= ~RES_DSP_ERROR;
    pic_unmask();
    _disable();
    if (res_sb.flags & RES_HALTED) { _enable(); return 0; }
    for (i = queued * RES_BANK; i < RES_BANKS * RES_BANK; i++) dma[i] = 128;
    res_sb.playing = 0; res_sb.silent = 0;
    s3_dma8(res_sb.channel, physical, RES_BANK * RES_BANKS, 1);
    res_sb.flags = RES_RUNNING;
    _enable();
    if (!dsp_write(0x1c)) { halt(1); res_info.state |= RES_DSP_ERROR; return 0; }
    res_info.starts++;
    was_running = 1;
    return 1;
}
/* Un programme a repris le vecteur de l'IRQ : il possède la carte. */
static int available(void)
{
    unsigned long __far *ivt = (unsigned long __far *)MK_FP(0, 0);
    return ivt[res_info.vector_irq] == (unsigned long)res_info.isr[2];
}
static void stack_check(void)
{
    unsigned i = 0;
    while (i < RES_STACK && res_stack[i] == 0x5a) i++;
    if (i < res_info.stack_low) res_info.stack_low = i;
}

void __far res_pump(void)
{
    unsigned long t0 = s3_ticks(), t;
    unsigned budget;
    res_info.pumps++;
    if (was_running && !(res_sb.flags & (RES_RUNNING | RES_HALTED))) {
        /* L'IRQ a arrêté le DMA après un silence : tout a été joué. */
        was_running = queued = 0;
        res_info.idle_stops++;
    }
    if (ap_cut()) {
        if (res_sb.flags) res_info.cut_stops++;
        halt(1);
        staged = 0; measuring = 0;
    }
    if (res_sb.arrived && !(res_sb.flags & RES_RUNNING) && !queued) {
        res_info.wait_ticks = *(unsigned __far *)MK_FP(0x40, 0x6c) - res_sb.arrive;
        if (res_info.wait_ticks > res_info.wait_max) res_info.wait_max = res_info.wait_ticks;
        seen_at = t0; measuring = 1; measured = 0;
        res_info.requests++;
    }
    res_sb.arrived = 0;
    if (!available()) {
        /* Rien n'est joué : avancer au rythme du calcul, sans toucher à la carte,
           pour que les échos attendus par le lecteur arrivent quand même. */
        if (!(res_info.state & RES_UNAVAILABLE)) { res_info.state |= RES_UNAVAILABLE; queued = was_running = 0; res_sb.flags = 0; }
        for (budget = 2; budget; budget--) {
            if (!staged) staged = ap_render(pcm, RES_BANK);
            if (!staged) break;
            res_sb.played += staged; res_info.skipped++; staged = 0;
        }
        ap_played(res_sb.played);
        return;
    }
    if (res_info.state & RES_UNAVAILABLE) { res_info.state &= ~RES_UNAVAILABLE; halt(1); }
    for (budget = RES_BUDGET; budget; budget--) {
        if (!staged) {
            staged = ap_render(pcm, RES_BANK);
            if (!staged) break;
            if (measuring && !measured) {
                res_info.first_buffer = s3_ticks() - seen_at; measured = 1;
                if (res_info.first_buffer > res_info.first_buffer_max) res_info.first_buffer_max = res_info.first_buffer;
            }
        }
        if (!commit(staged)) break;
        staged = 0;
    }
    res_sb.speaking = staged || ap_speaking();
    if (!(res_sb.flags & (RES_RUNNING | RES_HALTED)) && queued && (queued >= 2 || !res_sb.speaking)) {
        if (start() && measuring) {
            res_info.submit = s3_ticks() - seen_at; measuring = 0;
            if (res_info.submit > res_info.submit_max) res_info.submit_max = res_info.submit;
        }
    }
    ap_played(played());
    t = s3_ticks() - t0;
    res_info.busy += t;
    if (t > res_info.busy_max) res_info.busy_max = t;
    res_info.clock_fault = s3_clock_fault;
    if (!(res_info.pumps & 63)) stack_check();
}

/* ------------------------------------------------- installation (transitoire) */

static void print(const char *s)
{
    unsigned written;
    _dos_write(1, s, (unsigned)strlen(s), &written);
}
static void number(unsigned long v, int base)
{
    char b[12]; int i = 11;
    b[i] = 0;
    do { b[--i] = "0123456789ABCDEF"[v % base]; v /= base; } while (v);
    print(b + i);
}
unsigned __far res_mux(unsigned ax);
extern unsigned res_mux_es, res_mux_di;
static unsigned mux(unsigned ax, void __far **es_di)
{
    unsigned r = res_mux(ax);
    if (es_di) *es_di = MK_FP(res_mux_es, res_mux_di);
    return r;
}
/* Réglages « A220 I5 D1 », de BLASTER puis de la ligne de commande (/A /I /D). */
static int parse(const char __far *p, int strict)
{
    while (*p && *p != '\r') {
        unsigned long v = 0;
        unsigned digits = 0, base;
        char key;
        while (*p == ' ' || *p == '/' || *p == '\t') p++;
        if (!*p || *p == '\r') break;
        key = (char)(*p++ & ~32);
        base = key == 'A' ? 16 : 10;
        for (;; p++, digits++) {
            unsigned d = *p >= '0' && *p <= '9' ? *p - '0' :
                         base == 16 && (*p | 32) >= 'a' && (*p | 32) <= 'f' ? (*p | 32) - 'a' + 10 : 99;
            if (d >= base || v > 0xffffUL) break;
            v = v * base + d;
        }
        if (!digits) { if (strict && key != 'F') return 0; while (*p && *p != ' ') p++; continue; }
        if (key == 'A') res_sb.base = (unsigned)v;
        else if (key == 'I') res_sb.irq = (unsigned)v;
        else if (key == 'D') res_sb.channel = (unsigned)v;
        else if (strict) return 0;
    }
    return 1;
}
static const char __far *blaster(void)
{
    const char __far *e = (const char __far *)MK_FP(*(unsigned __far *)MK_FP(_psp, 0x2c), 0);
    while (*e) {
        if (e[0] == 'B' && e[1] == 'L' && e[2] == 'A' && e[3] == 'S' && e[4] == 'T' &&
            e[5] == 'E' && e[6] == 'R' && e[7] == '=') return e + 8;
        while (*e) e++;
        e++;
    }
    return 0;
}
static int valid(void)
{
    if (res_sb.irq == 2) res_sb.irq = 9;
    return res_sb.base >= 0x210 && res_sb.base <= 0x280 && !(res_sb.base & 15) &&
        (res_sb.irq == 3 || res_sb.irq == 5 || res_sb.irq == 7 || res_sb.irq == 9 ||
         res_sb.irq == 10) && (res_sb.channel <= 1 || res_sb.channel == 3);
}
static int uninstall(void)
{
    RES_INFO __far *info;
    void __far * __far *ivt = (void __far * __far *)MK_FP(0, 0);
    void __far *p;
    unsigned vectors[3], i, port;
    if (mux(0xd301, &p) != 0xd3ff) { print("S3RES n'est pas résident.\r\n"); return 1; }
    info = (RES_INFO __far *)p;
    if (info->clients) {
        print("S3RES est utilisé par un lecteur d'écran : retirer la ligne de AUTOEXEC.BAT et redémarrer.\r\n");
        return 1;
    }
    vectors[0] = 8; vectors[1] = 0x2f; vectors[2] = info->vector_irq;
    for (i = 0; i < 3; i++) if (ivt[vectors[i]] != info->isr[i]) {
        print("Un programme chargé après S3RES utilise ses interruptions : retirer d'abord ce programme.\r\n");
        return 1;
    }
    _disable();
    for (i = 0; i < 3; i++) ivt[vectors[i]] = info->old[i];
    port = info->dma;
    outp(0x0a, port | 4);
    outp(info->base + 6, 1);
    for (i = 0; i < 8; i++) inp(info->base + 6);
    outp(info->base + 6, 0);
    if (info->pic_was_masked) {
        if (info->irq < 8) outp(0x21, inp(0x21) | (1 << info->irq));
        else outp(0xa1, inp(0xa1) | (1 << (info->irq - 8)));
    }
    _enable();
    _dos_freemem(info->dma_block);
    _dos_freemem(info->psp);
    print("S3RES retiré.\r\n");
    return 0;
}
/* 2 Kio de DMA en mémoire basse (identité physique sous EMM386), sans franchir une
   frontière de 64 Kio : si le premier bloc la franchit, le suivant ne la franchit pas. */
static unsigned dos_alloc(unsigned paragraphs)
{
    union REGS r;
    r.h.ah = 0x48; r.w.bx = paragraphs; intdos(&r, &r);
    return r.x.cflag ? 0 : r.w.ax;
}
static int allocate_dma(void)
{
    union REGS r;
    unsigned strategy, link, segment, first = 0;
    r.w.ax = 0x5800; intdos(&r, &r); strategy = r.w.ax;
    r.w.ax = 0x5802; intdos(&r, &r); link = r.h.al;
    r.w.ax = 0x5803; r.w.bx = 0; intdos(&r, &r);   /* hors UMB */
    r.w.ax = 0x5801; r.w.bx = 0; intdos(&r, &r);   /* premier bloc libre, en bas */
    segment = dos_alloc(128);
    if (segment && ((unsigned long)segment << 4 & 0xffffUL) + 2048 > 0x10000UL) {
        first = segment; segment = dos_alloc(128);
    }
    if (first) _dos_freemem(first);
    r.w.ax = 0x5801; r.w.bx = strategy; intdos(&r, &r);
    r.w.ax = 0x5803; r.w.bx = link; intdos(&r, &r);
    if (!segment) return 0;
    res_info.dma_block = segment;
    physical = (unsigned long)segment << 4;
    if ((physical & 0xffffUL) + RES_BANK * RES_BANKS > 0x10000UL ||
        physical + RES_BANK * RES_BANKS > 0xa0000UL) { _dos_freemem(segment); return 0; }
    dma = (unsigned char __far *)MK_FP(segment, 0);
    res_sb.dma_off = 0; res_sb.dma_seg = segment;
    return 1;
}

/* Choix d'Épaudos : EPAUDOS.INI à côté de S3RES.EXE, ligne « Synthese= ».
   1 : Synthé3 (Synthe3, S3 ou 1, ou clé ou fichier absents : défaut) ; 0 : Apollo. Le
   chemin du programme est lu après l'environnement, pas dans argv : la bibliothèque
   C allouerait pour argv un bloc DOS de 8 Kio qui resterait résident. */
static int chosen(void)
{
    const char __far *self = (const char __far *)MK_FP(*(unsigned __far *)MK_FP(_psp, 0x2c), 0);
    char path[80], buf[512];
    const char *key = "Synthese=";
    unsigned i, j, n, last = 0;
    int h;
    while (self[0] || self[1]) self++;
    self += 4;                  /* deux zéros, puis le nombre de chaînes (1) */
    for (i = 0; self[i] && i < sizeof(path) - 12; i++) {
        path[i] = self[i];
        if (self[i] == '\\' || self[i] == ':') last = i + 1;
    }
    strcpy(path + last, "EPAUDOS.INI");
    if (_dos_open(path, 0, &h)) return 1;
    if (_dos_read(h, buf, sizeof(buf), &n)) n = 0;
    _dos_close(h);
    for (i = 0; i < n; i++) {
        if (i && buf[i - 1] != '\n') continue;
        for (j = 0; key[j] && i + j < n && buf[i + j] == key[j]; j++) {}
        if (!key[j] && i + j < n) return (buf[i + j] | 32) == 's' || buf[i + j] == '1';
    }
    return 1;                   /* sans choix : Synthé3, synthèse par défaut */
}

int main(void)
{
    const char __far *tail = (const char __far *)MK_FP(_psp, 0x81), *env;
    unsigned paragraphs, envseg, mask, i, major, minor;
    int unload = 0, force = 0;
    for (i = 0; tail[i] && tail[i] != '\r'; i++) {
        if (tail[i] == '/' && (tail[i + 1] & ~32) == 'U') unload = 1;
        if (tail[i] == '/' && (tail[i + 1] & ~32) == 'F') force = 1;
        if (tail[i] == '/' && tail[i + 1] == '?') goto usage;
    }
    if (mux(0x1600, NULL) & 0x7f) { print("S3RES : à charger avant Windows, jamais dans une fenêtre DOS.\r\n"); return 1; }
    if (unload) return uninstall();
    if (mux(0xd300, NULL) == 0xd3ff) { print("S3RES est déjà résident.\r\n"); return 0; }
    if (!force && !chosen()) {
        print("S3RES non chargé : Épaudos utilise l'Apollo II (EPAUDOS.INI : Synthese=Apollo ;"
              " S3RES /F pour charger quand même).\r\n");
        return 0;
    }
    if (FP_SEG(&ap_link) != FP_SEG(&res_sb) || FP_SEG(&res_info) != FP_SEG(&res_sb) ||
        FP_SEG(res_stack) != FP_SEG(&res_sb)) {
        print("S3RES : compilation incorrecte (segments).\r\n"); return 1;
    }
    res_sb.base = 0x220; res_sb.irq = 5; res_sb.channel = 1;
    if ((env = blaster()) != 0) parse(env, 0);
    if (!parse(tail, 1)) goto usage;
    if (!valid()) {
        print("S3RES : réglage Sound Blaster refusé (A210..A280, I3/5/7/9/10, D0/1/3).\r\n");
        return 1;
    }
    if (!dsp_reset() || !dsp_write(0xe1) || (int)(major = (unsigned)dsp_read()) < 0 ||
        (int)(minor = (unsigned)dsp_read()) < 0 || major < 2) {
        print("S3RES : aucune Sound Blaster (DSP 2.0 ou plus) à l'adresse A"); number(res_sb.base, 16);
        print(". Vérifier BLASTER ou /A /I /D.\r\n");
        return 1;
    }
    if (!allocate_dma()) { print("S3RES : mémoire basse insuffisante pour le DMA.\r\n"); return 1; }
    s3_init();
    ap_init();
    memset(res_stack, 0x5a, sizeof(res_stack));
    res_info.stack_low = RES_STACK;
    res_info.dgroup = FP_SEG(&res_info); res_info.psp = _psp;
    res_info.base = res_sb.base; res_info.irq = res_sb.irq; res_info.dma = res_sb.channel;
    res_info.off_sb = FP_OFF(&res_sb); res_info.off_link = FP_OFF(&ap_link);
    res_info.off_stats = FP_OFF(&ap_stats); res_info.off_trace = FP_OFF(ap_trace);
    s3_clock_init();
    res_info.vector_irq = res_sb.irq < 8 ? 8 + res_sb.irq : 0x70 + res_sb.irq - 8;
    mask = res_sb.irq < 8 ? inp(0x21) >> res_sb.irq : inp(0xa1) >> (res_sb.irq - 8);
    res_info.pic_was_masked = mask & 1;
    res_old08 = _dos_getvect(8);
    res_old2f = _dos_getvect(0x2f);
    res_oldirq = _dos_getvect(res_info.vector_irq);
    res_info.old[0] = (void __far *)res_old08; res_info.old[1] = (void __far *)res_old2f;
    res_info.old[2] = (void __far *)res_oldirq;
    res_info.isr[0] = (void __far *)res_isr08; res_info.isr[1] = (void __far *)res_isr2f;
    res_info.isr[2] = (void __far *)res_irq;
    _dos_setvect(res_info.vector_irq, res_irq);
    _dos_setvect(0x2f, res_isr2f);
    _dos_setvect(8, res_isr08);
    pic_unmask();
    envseg = *(unsigned __far *)MK_FP(_psp, 0x2c);
    *(unsigned __far *)MK_FP(_psp, 0x2c) = 0;
    if (envseg) _dos_freemem(envseg);
    paragraphs = FP_SEG(&res_info) + (_STACKTOP + 15) / 16 - _psp;
    print("Synthé3 résident : DSP "); number(major, 10); print("."); number(minor, 10);
    print(", A"); number(res_sb.base, 16); print(" I"); number(res_sb.irq, 10);
    print(" D"); number(res_sb.channel, 10); print(", ");
    number(((unsigned long)paragraphs * 16 + 1023) / 1024, 10);
    print(" Kio de mémoire conventionnelle.\r\n");
    _dos_keep(0, paragraphs);
    return 0;
usage:
    print("S3RES [/F] [/A220] [/I5] [/D1] : Synthé3 résident sur Sound Blaster (défaut : BLASTER),\r\n"
          "sauf si EPAUDOS.INI contient Synthese=Apollo (/F : toujours).\r\n"
          "S3RES /U : retirer (si aucun lecteur d'écran ne l'utilise).\r\n");
    return 1;
}
