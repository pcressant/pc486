/* SPDX-License-Identifier: GPL-3.0-or-later
 * Apollo local (ap.c) devant le vrai moteur : flux réellement émis par Épaudos et
 * Épaulard, fragmentation, Ctrl-X, marques @E rendues à la restitution seulement. */
#include "ap.h"
#include "s3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static short pcm[512];

static void put(const char *s)
{
    while (*s) assert(ap_put((unsigned char)*s++));
}
static unsigned long drain(void)
{
    unsigned long total = 0;
    unsigned n;
    while ((n = ap_render(pcm, 512)) != 0) total += n;
    return total;
}
static int peak;
static unsigned long drain_peak(void)
{
    unsigned long total = 0;
    unsigned n, i;
    peak = 0;
    while ((n = ap_render(pcm, 512)) != 0) {
        for (i = 0; i < n; i++) if (pcm[i] > peak) peak = pcm[i]; else if (-pcm[i] > peak) peak = -pcm[i];
        total += n;
    }
    return total;
}
static int traced(const char *s) { return strstr(ap_trace, s) != NULL; }
static unsigned long say_at(const char *setting)
{
    unsigned long n;
    ap_init();
    put(setting);
    put("Bonjour Michel, un \x82l\x8ave prend un caf\x82.\r");
    n = drain();
    assert(n > 5000);
    return n;
}

int main(void)
{
    unsigned long n, slow, fast, low, high;
    int i;
    char longer[400];
    s3_init();

    /* Épaudos : Ctrl-X, réglages en décimal puis texte et écho du dire tout. */
    ap_init();
    put("\x18@W3\r@F8\r@V1\rBonjour.\r@EA\r");
    assert(ap_cut() == 1 && ap_cut() == 0);
    n = drain();
    assert(n > 1000 && traced("Bonjour.|"));
    assert(ap_stats.rate == 3 && ap_stats.pitch == 8 && ap_stats.voice == 1);
    assert(ap_echo() == -1);            /* produit n'est pas joué */
    ap_played(n - 1);
    assert(ap_echo() == -1 && ap_pending_marks() == 1);
    ap_played(n);
    assert(ap_echo() == 'A' && ap_echo() == -1);

    /* Marque sans parole avant elle : rendue dès la position zéro. */
    ap_init();
    put("@Ez\r");
    assert(drain() == 0);
    ap_played(0);
    assert(ap_echo() == 'z');

    /* Coupure pendant une phrase longue, reprise immédiate, marque abandonnée. */
    ap_init();
    put("Une phrase assez longue pour remplir plusieurs blocs de restitution\r@Eb\r");
    assert(ap_render(pcm, 512) == 512);
    assert(ap_speaking());
    put("\x18" "Fin\r@Ec\r");
    assert(ap_cut() == 1);
    assert(!ap_speaking() && ap_pending_marks() == 0);
    n = drain();
    assert(n > 500 && traced("Fin|"));
    ap_played(n);
    assert(ap_echo() == 'c' && ap_echo() == -1);
    assert(ap_stats.rate == 3);         /* réglages conservés */

    /* Ctrl-X publié pendant le calcul : la lecture de la file s'arrête net, rien
       de ce qui le précède n'est dit après lui. */
    ap_init();
    put("avant\r");
    assert(ap_render(pcm, 512) == 512);
    put("encore\r\x18" "apres\r");
    assert(ap_render(pcm, 512) == 512);   /* proposition déjà commencée */
    while (ap_render(pcm, 512) == 512) {}
    assert(!traced("encore|"));
    assert(ap_cut() == 1 && drain() > 0 && traced("apres|") && !traced("encore|"));

    /* File pleine de texte périmé : le Ctrl-X passe quand même et l'annule. */
    ap_init();
    for (i = 0; ap_put('x'); i++) {}
    assert(i == AP_INPUT - 1 && ap_link.dropped == 1);
    assert(ap_put(0x18));
    assert(!ap_put('n'));               /* la place revient quand la pompe coupe */
    assert(ap_cut() == 1);
    put("neuf\r");
    assert(drain() > 0 && traced("neuf|") && !traced("xxx"));

    /* Commandes fragmentées entre deux pompages, hauteur hexadécimale d'Épaulard. */
    ap_init();
    put("@");
    assert(drain() == 0);
    put("W");
    assert(drain() == 0);
    put("7@FC@V2@P0\r");
    assert(drain() == 0 && ap_stats.rate == 7 && ap_stats.pitch == 12 && ap_stats.voice == 2);

    /* Fins de proposition, ponctuation nommée, CP850 converti en Latin-1. */
    ap_init();
    put("caf\x82, th\x82: oui\r");
    drain();
    assert(traced("caf\xe9,|") && traced("th\xe9:|") && traced("oui|"));
    ap_init();
    put("@P1\ra+b. c\r");
    drain();
    assert(traced("a plus b point |") && traced("c|"));
    /* í et ø (marques internes du moteur en Latin-1) ne sont pas refusés. */
    ap_init();
    put("\xa1\x9b\r");
    assert(drain() > 0 && ap_stats.refused == 0);

    /* Au-delà de 240 octets sans fin : découpage à une espace, rien de perdu. */
    ap_init();
    for (i = 0; i < 300; i++) longer[i] = (char)(i % 10 == 9 ? ' ' : 'a' + i % 7);
    longer[300] = '\r'; longer[301] = 0;
    put(longer);
    drain();
    assert(ap_stats.propositions == 2 && ap_stats.refused == 0);

    /* File pleine : refus sans perte de ce qui est déjà accepté. */
    ap_init();
    for (i = 0; i < AP_INPUT - 1; i++) assert(ap_put('x'));
    assert(!ap_put('x') && ap_link.dropped == 1);

    /* Débit et hauteur : W9 plus rapide que W1 ; F hexadécimal pris en compte.
       Échelle des lecteurs : W1 débit naturel (3 du moteur), W5 deux fois plus vite. */
    slow = say_at("@W1\r");
    fast = say_at("@W9\r");
    {
        unsigned long w5 = say_at("@W5\r"), w3 = say_at("@W3\r");
        printf("W1 %lu W3 %lu W5 %lu W9 %lu\n", slow, w3, w5, fast);
        assert(fast < w5 && w5 < w3 && w3 < slow);
        assert(w5 > slow * 45 / 100 && w5 < slow * 55 / 100);
        assert(fast > slow * 20 / 100 && fast < slow * 30 / 100);
    }
    low = say_at("@F0\r");
    high = say_at("@FF\r");
    printf("W1 %lu W9 %lu F0 %lu FF %lu\n", slow, fast, low, high);
    assert(fast < slow);

    /* Volume @A : même parole, crête proportionnelle ; @A3 = niveau du moteur. */
    {
        unsigned long n3, n7, n0, nf;
        int p3, p7, p0, pf;
        ap_init(); put("@A3\rBonjour Michel.\r"); n3 = drain_peak(); p3 = peak;
        ap_init(); put("Bonjour Michel.\r"); assert(drain_peak() == n3 && peak == p3);
        ap_init(); put("@A7\rBonjour Michel.\r"); n7 = drain_peak(); p7 = peak;
        ap_init(); put("@A0\rBonjour Michel.\r"); n0 = drain_peak(); p0 = peak;
        ap_init(); put("@AF\rBonjour Michel.\r"); nf = drain_peak(); pf = peak;
        printf("A0 %d A3 %d A7 %d AF %d\n", p0, p3, p7, pf); fflush(stdout);
        assert(n0 == n3 && n7 == n3 && nf == n3);
        assert(p0 <= p3 / 4 + 1 && p0 >= p3 / 4 - 1);
        assert(p7 >= 2 * p3 - 2 && p7 <= 32767 && pf <= 32767 && pf >= p7);
        /* Ctrl-X ne remet pas le volume à zéro. */
        ap_init(); put("@A0\r\x18" "Bonjour Michel.\r"); ap_cut(); drain_peak();
        printf("A0 puis Ctrl-X %d\n", peak); fflush(stdout);
        assert(peak == p0);
        /* Réglages envoyés juste avant un Ctrl-X, sans pompe entre les deux
           (Épaulard au démarrage) : appliqués comme par l'Apollo II, qui les exécute
           à réception ; l'écho @E et le texte abandonnés. */
        ap_init(); put("@W9@F2@AF\rBonjour Michel.\r"); drain_peak(); pf = peak;
        ap_init(); put("@W9@F2@AF\rperdu\r@EZ\r\x18" "Bonjour Michel.\r");
        assert(ap_cut());
        drain_peak();
        assert(ap_stats.rate == 9 && ap_stats.pitch == 2 && peak == pf);
        assert(ap_echo() < 0 && !traced("perdu"));
    }
    puts("apollo OK");
    return 0;
}
