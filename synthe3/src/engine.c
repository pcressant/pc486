/* Port pc486 : génération PCM par blocs, noyaux entiers 386 ; voir ../README.md.
 * Adaptations : GPL-3.0-or-later ; moteur historique : notice ci-dessous.
 * Copie de référence et provenance conservées dans ../vendor/.
 */
/* Build with -fsigned-char: tables and phoneme sentinels use signed bytes. */
/*
 * Synthé 3 - A speech synthetizer software for french
 *
 * Copyright (C) 1985-2015 by Michel MOREL <michel.morel@unicaen.fr>.
 *
 * Synthé 3 comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: https://github.com/acceslibreinfo/synthe3
 *
 * This software is maintained by Shérab <Sebastien.Hinderer@ens-lyon.org>.
 */
/* C port: historical phonetics and diphone arithmetic are intentionally retained.
 * One engine instance, owned by the synchronous Speech Dispatcher loop. */
#include "s3.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NM_CAR_TEX 10000
#define NM_CAR_TEX_1 (NM_CAR_TEX - 1)
#define NM_CAR_TEX_2 (NM_CAR_TEX - 2)
#define NM_CAR_TEX_8 (NM_CAR_TEX - 8)
#define MARQ_MARQ ((char)248)
#define MARQ_INDEX ((char)237)
#define HAUTEUR_REF 6
#define DEBIT_REF 3
#define VOLUME_REF 10
#define PI 3.14159
#define FREQ_ECH 10000
#define FREQ_COUPURE 400
#define MUL_AMP 8
#include "../vendor/voice_data.h"

/* sprintf("%d") pour un entier positif, sans printf : le résident S3RES ne lie pas
   le formatage de la bibliothèque C (mêmes octets produits). */
static void decimal(char *s, int n)
{
    char b[6];
    int i = 0, j = 0;
    do { b[i++] = (char)('0' + n % 10); n /= 10; } while (n && i < 5);
    while (i) s[j++] = b[--i];
    s[j] = 0;
}
static int engine_rate, engine_pitch;
static short period_pcm[256];
static unsigned period_n, period_pos;
static unsigned long pcm_size;
static int active, failed;
static long filter_state[2];
/* Indices en échantillons 32 bits, même avec size_t DOS de 16 bits. */
static unsigned long word_samples[241];

/* Existing automatic word indices are silent; retain their PCM positions for
 * server bookmarks without changing the phonetic input or diphone arithmetic. */
static unsigned word_bytes[241], word_count, word_seen;

static short readShort(const char *p) {
    return (short)((unsigned char)p[0] | ((unsigned char)p[1] << 8));
}
#define BVRD 1 // teste le mode bavard
#define NABR 2 // teste le mode non abréviations
#define VOYM 3 // voyelle "mouillée"
#define VOY 4  // voyelle
#define LET 5  // lettre
#define CNS 5  // lettre
#define PONC 6 // ponctuation
#define SYMB 7 // symbole
#define CHIF 8 // chiffre
#define TTT 8  // tout
#define PG 80  // programme (retour au cas général)
#define RB 81  // arbre
#define DC 82  // dictionnaire
#define VOYP 0 // voyelles
#define GLIP 1 // glissantes
#define CNSP 2 // consonnes
#define OU 0
#define WW 0
#define OO 1
#define UU 2
#define II 3
#define AA 4
#define AN 5
#define ON 6
#define EU 7
#define EE 8
#define UN 9
#define YY 10
#define FF 11
#define SS 12
#define HH 13
#define VV 14
#define ZZ 15
#define JJ 16
#define PP 17
#define TT 18
#define KK 19
#define BB 20
#define DD 21
#define GG 22
#define MM 23
#define NN 24
#define LL 25
#define RR 26
#define VR 27 // virgule phon
#define PL 28 // allongement
#define INDEX '_'
#define NULP -2 // nul phon

static char *tTab;

static short aTWin;
static short aTCat;
static short aTPhon1;
static short aTPhon2;
static short aTCatP1;
static short aTCatP2;
static short aChLst1;
static short aChLst2;
static short aChLst3;

static short aCh01;
static short aCh1S;
static short aCh100;
static short aCh100Z;
static short aTabVol;
static short aTabVit;
static short aTabHau;
static short aCateg;
static short aFinAmp;
static short aFinTim;
static short aTraAmp;
static short aTraTim;
static short aDebAmp;
static short aDebTim;
static short aTabAmp;
static short aAdAmp;
static short aTabDeb;
static short a_Huit1;
static short a_Dix1;
static short aXChif;
static short lgTab;
static char *texteLec;
static char *texteEcr;
static short iLec;
static short iEcr;
static unsigned char carac;
static unsigned char categ;
static short ctBava;
static char liaison;
static char *ptArbre;
static char nombre[12];

typedef struct Marq {
    bool DSDecroit; // début sous-diphone décroit
    bool DSCroit;   // début sous-diphone croit
    bool FSDecroit; // fin sous-diphone décroit
    bool FSCroit;   // fin sous-diphone croit
} Marq;

typedef struct UChar {
    unsigned char DSDecroit; // début sous-diphone décroit
    unsigned char DSCroit;   // début sous-diphone croit
    unsigned char FSDecroit; // fin sous-diphone décroit
    unsigned char FSCroit;   // fin sous-diphone croit
} UChar;

typedef struct PUChar {
    unsigned char *DSDecroit; // début sous-diphone décroit
    unsigned char *DSCroit;   // début sous-diphone croit
    unsigned char *FSDecroit; // fin sous-diphone décroit
    unsigned char *FSCroit;   // fin sous-diphone croit
} PUChar;

typedef struct PSChar {
    char *DSDecroit; // début sous-diphone décroit
    char *DSCroit;   // début sous-diphone croit
    char *FSDecroit; // fin sous-diphone décroit
    char *FSCroit;   // fin sous-diphone croit
} PSChar;

#ifdef S3_INTEGER_PERIOD
/* Arithmétique par période sans x87 : tabHau/tabVit sont des millièmes exacts,
 * les positions sont donc tenues en millièmes d'échantillon sur 32 bits.
 * |pas| <= 255 × 4000 ; les accumulateurs oscillent autour de zéro. */
static long hau_milli, vit_milli;
static long xEcr_milli, allonge_milli;
#else
static float mulHauteur;
static float mulDebit;
static float xEcrEchelleDeLec;
static float allonge;
static float perioBase;
#endif
static char *texPhon;
static long iLecPhon;
static long iLecPhonDecroit;
static long iLecPhonCroit;
static char phonGDecroit;
static char phonGCroit;
static char phonDDecroit;
static char phonDCroit;
static char phonMem;
static char catGDecroit;
static char catGCroit;
static char catDDecroit;
static char catDCroit;
static char nSousDiphDecroit;
static char nSousDiphCroit;
static Marq marq;
static UChar nSeg;
static UChar nSegIni;
static PSChar ptSeg;
static UChar perio;
static unsigned char perioResult;
static PUChar ptAmp;
static UChar amp;
static UChar ampAnc;
#ifndef S3_BLOCK
static short iEch;
#endif
static unsigned char FOIS;
#ifdef S3_PROFILE
/* Banc DOS seulement : compteurs et temps PIT par période, hors API. */
#include "s3time.h"
unsigned long s3_prof_periods, s3_prof_prepares, s3_prof_loop_ticks,
              s3_prof_control_ticks, s3_prof_prepare_ticks;
#endif
#if defined(S3_BLOCK) && defined(S3_SCALAR_WINDOW)
#error "S3_BLOCK suppose les fenêtres préparées (sans S3_SCALAR_WINDOW)"
#endif
#ifdef S3_BLOCK
/* Somme des quatre contributions d'une période, avant gain et filtre. */
static short mix[256];
extern const char s3_block_impl[];
#endif
#if defined(S3_WINDOW_CACHE) && !defined(S3_SCALAR_WINDOW)
#if S3_WINDOW_CACHE < 4 || S3_WINDOW_CACHE > 64
#error "S3_WINDOW_CACHE : 4 à 64 longueurs"
#endif
/* Cache LRU de S3_WINDOW_CACHE longueurs (512 octets chacune). Les tables
 * demandées dans la période courante ne sont jamais évincées (quatre au plus).
 * Même PCM que le cache historique : seule la politique de remplacement change. */
static unsigned short windows[S3_WINDOW_CACHE][256];
static unsigned window_length[S3_WINDOW_CACHE], window_stamp[S3_WINDOW_CACHE],
                window_period[S3_WINDOW_CACHE], window_clock, window_serial;
static void window_next_period(void)
{
    if (!++window_serial) { /* 65536 périodes : repartir d'une protection vide */
        memset(window_period, 0, sizeof(window_period));
        window_serial = 1;
    }
}
static const unsigned short *prepare_window(unsigned period, unsigned amplitude)
{
    unsigned i, victim = S3_WINDOW_CACHE, age = 0, length = period < perioResult ? period : perioResult;
    if (!amplitude || !length) return NULL;
    for (i = 0; i < S3_WINDOW_CACHE; i++)
        if (window_length[i] == length) {
            window_stamp[i] = ++window_clock;
            window_period[i] = window_serial;
            return windows[i];
        }
    for (i = 0; i < S3_WINDOW_CACHE; i++)
        if (window_period[i] != window_serial && (victim == S3_WINDOW_CACHE ||
                (unsigned)(window_clock - window_stamp[i]) > age)) {
            victim = i;
            age = (unsigned)(window_clock - window_stamp[i]);
        }
    if (victim == S3_WINDOW_CACHE) return NULL; /* impossible : au plus quatre protégées */
#ifdef S3_PROFILE
    {
        unsigned long stamp = s3_ticks();
        s3_prof_prepares++;
#endif
    s3_weights(length, windows[victim]);
#ifdef S3_PROFILE
        s3_prof_prepare_ticks += s3_ticks()-stamp;
    }
#endif
    window_length[victim] = length;
    window_stamp[victim] = ++window_clock;
    window_period[victim] = window_serial;
    return windows[victim];
}
#elif !defined(S3_SCALAR_WINDOW)
/* Quatre longueurs au plus dans une période. Une table utilisée reste protégée
 * jusqu'à sa fin, même si le cache contenait d'autres longueurs de l'ancienne.
 * 2048 octets de tables ; aucun changement de phase, d'arrondi ni de phonème. */
static unsigned short windows[4][256];
static unsigned window_length[4], window_used;
static void window_next_period(void) { window_used = 0; }
static const unsigned short *prepare_window(unsigned period, unsigned amplitude)
{
    unsigned i, length = period < perioResult ? period : perioResult;
    if (!amplitude || !length) return NULL;
    for (i = 0; i < 4; i++)
        if (window_length[i] == length) {
            window_used |= 1 << i;
            return windows[i];
        }
    for (i = 0; i < 4; i++)
        if (!(window_used & (1 << i))) {
#ifdef S3_PROFILE
            unsigned long stamp = s3_ticks();
            s3_prof_prepares++;
#endif
            s3_weights(length, windows[i]);
#ifdef S3_PROFILE
            s3_prof_prepare_ticks += s3_ticks()-stamp;
#endif
            window_length[i] = length;
            window_used |= 1 << i;
            return windows[i];
        }
    return NULL; /* impossible : au plus quatre contributions par période */
}
#endif
static unsigned char carSuiv(void);
static char *voice_segment(unsigned char n) {
    return (char *)voice_segments + readShort((const char *)voice_addresses + n * 2);
}
static char tab_tCat(unsigned char c);
static char *tab_chListUnit(char c);
static char *tab_chListDiz(char c);
static char *tab_chListMil(unsigned short int n);
static char *tab_ch01();
static char *tab_ch1S();
static char *tab_ch100();
static char *tab_ch100Z();
static short tab_tabVit(char n);
static short tab_tabHau(char n);
static char tab_categ(unsigned short int phon);
static char *tab_getPtTabDeb(unsigned char c);
static char *tab_getPtArbre(char *ptArbre);
static char *tab_getPtArbreRoot(char *ptArbre, unsigned char carac);
static char *tab__Huit1();
static char *tab__Dix1();
static char *tab_XChif();
static void minMajNFois(char *texteAlphaLec, char *texteAlphaEcr);
static void graphemePhoneme(char *texte, char *texPhon);
static void traiteNombre();
static void finNombre(short unsigned int *iN);
static void phonChif(char *ptChif);
static void ecritPhon(char c);
static void ecrit(char c);
static bool traiteTextePhonetique(char *chainePhon);
static bool traiteUnePeriode();
static void initDecroitSurCroit();
static bool perioSuiv(bool *marqDS, bool *marqFS, long *iLecPhonX, char *phonG, char *phonD,
                      char *catG, char *catD, char *nSousDiph, unsigned char *nSegIniDS,
                      unsigned char *nSegIniFS, unsigned char *nSegDS, unsigned char *nSegFS,
                      char **ptSegDS, char **ptSegFS, unsigned char *perioDS,
                      unsigned char *perioFS, unsigned char **ptAmpDS, unsigned char **ptAmpFS,
                      unsigned char *ampDS, unsigned char *ampFS, bool mCroit);
static bool nouveauSousDiph(long *iLecPhonX, char *phonG, char *phonD, char *catG, char *catD,
                            char *nSousDiph, unsigned char *nSegIniDS, unsigned char *nSegDS,
                            unsigned char **ptAmpDS, unsigned char *ampDS);
static char phonSuiv(long *iLecPhonX, char phonG);
#ifdef S3_SCALAR_WINDOW
static short calculeEchPerioDecroit(short x, char *ptSegDecroit, unsigned char perioDecroit,
                                    unsigned char ampDecroit);
static short calculeEchPerioCroit(short x, char *ptSegCroit, unsigned char perioCroit,
                                  unsigned char ampCroit);
#endif
static unsigned char tab_tWin(unsigned char carac) {
    if (carac < 32)
        carac = 32;
    if (carac >= 'a' && carac <= 'z')
        carac -= 32;
    if (carac < 123)
        return carac;
    return ((unsigned char *)tTab + aTWin)[carac - 123];
}

static char tab_tCat(unsigned char c) {
    if (c < 32 || c > FOIS)
        return PONC;
    return (tTab + aTCat)[c - 32];
}

static char *tab_chListUnit(char c) { return tTab + readShort(tTab + aChLst1 + 2 * (c - 48)); }

static char *tab_chListDiz(char c) { return tTab + readShort(tTab + aChLst2 + 2 * (c - 49)); }

static char *tab_chListMil(unsigned short int n) {
    return tTab + readShort(tTab + aChLst3 + 2 * (n));
}

static char *tab_ch01() { return tTab + aCh01; }

static char *tab_ch1S() { return tTab + aCh1S; }

static char *tab_ch100() { return tTab + aCh100; }

static char *tab_ch100Z() { return tTab + aCh100Z; }


static short tab_tabVit(char n) { return readShort(tTab + aTabVit + 2 * (n + 3)); }

static short tab_tabHau(char n) { return readShort(tTab + aTabHau + 2 * (n + 6)); }

static char tab_categ(unsigned short int phon) { return (tTab + aCateg)[phon]; }

static unsigned char tab_finAmp(char catG, char catD) { return (tTab + aFinAmp)[catG * 10 + catD]; }

static unsigned char tab_finTim(unsigned short int phon) {
    return ((unsigned char *)tTab + aFinTim)[phon];
}

static unsigned char tab_traAmp(char catG, char catD) { return (tTab + aTraAmp)[catG * 10 + catD]; }

static unsigned char tab_traTim(char phonG, char phonD) {
    return ((unsigned char *)tTab + aTraTim)[phonG * 32 + phonD];
}

static unsigned char tab_debAmp(char catG, char catD) { return (tTab + aDebAmp)[catG * 10 + catD]; }

static unsigned char tab_debTim(unsigned short int phon) {
    return ((unsigned char *)tTab + aDebTim)[phon];
}

static unsigned char *tab_getPtAmp(short nAmp) {
    short adr = readShort(tTab + aAdAmp + 2 * (nAmp));
    return (unsigned char *)(tTab + aTabAmp + adr);
}

static char *tab_getPtTabDeb(unsigned char c) {
    short adr = readShort(tTab + aTabDeb + 2 * (c - 32));
    return tTab + adr;
}

static char *tab_getPtArbre(char *ptArbre) {
    short adr = readShort(ptArbre);
    return tTab + adr;
}

static char *tab_getPtArbreRoot(char *ptArbre, unsigned char carac) {
    short adr = readShort(ptArbre);
    adr = readShort(tTab + adr + 2 * (carac - 32));
    return tTab + adr;
}

/* Isolated accented vowels: "e accent aigu" instead of "é aigu", "a accent grave"
 * instead of "a grave"; "e tréma" instead of "é tréma". The historical tables are
 * unchanged: only the final rule (TTT) of these word-start trees is replaced,
 * and its continuation, which names the diacritic, is kept. */
static const struct {
    unsigned char latin1;
    char letter, accent; // accent: insert "accent" before the diacritic
} isolated_vowels[] = {
    {0xE0, AA, 1}, {0xE2, AA, 1}, {0xE1, AA, 1}, {0xE8, EE, 1}, {0xEA, EE, 1},
    {0xEB, EE, 0}, {0xEC, II, 1}, {0xEE, II, 1}, {0xED, II, 1}, {0xF2, OO, 1},
    {0xF4, OO, 1}, {0xF3, OO, 1}, {0xF9, UU, 1}, {0xFB, UU, 1}, {0xFA, UU, 1},
};
static struct {
    const char *rule; // body of the replaced rule in tTab
    char retract;     // phoneme already written by a shared prefix rule, or -1
    char body[12];
} rule_overrides[16];
static size_t rule_override_count;

/* Body (after the condition) of the TTT rule of a tree, or NULL. */
static const char *tab_ruleTTT(const char *tree) {
    for (int rule = 0; rule < 64; rule++) {
        char condition = *tree++;
        const char *body = tree;
        while (*tree < PG)
            tree++;
        if (condition == TTT)
            return body;
        tree += 3;
    }
    return NULL;
}

static void add_override(const char *rule, int skip, char retract, char letter, int accent) {
    static const char name[] = {PL, AA, KK, SS, AN};
    char *p = rule_overrides[rule_override_count].body;
    /* The continuation must start with a pause before the diacritic name. */
    const char *next = tab_getPtArbre((char *)rule + skip + 1);
    if (rule[skip] != RB || next[0] != TTT || next[1] != PL)
        return;
    if (letter == EE) {
        *p++ = EU; // letter name "e"
        *p++ = EU;
    } else
        *p++ = letter;
    if (accent)
        for (size_t i = 0; i < sizeof name; i++)
            *p++ = name[i];
    memcpy(p, rule + skip, 3);
    rule_overrides[rule_override_count].rule = rule;
    rule_overrides[rule_override_count++].retract = retract;
}

static void init_rule_overrides(void) {
    rule_override_count = 0;
    for (size_t i = 0; i < sizeof isolated_vowels / sizeof *isolated_vowels; i++) {
        const char *rule = tab_ruleTTT(tab_getPtTabDeb(tab_tWin(isolated_vowels[i].latin1)));
        if (rule && rule[0] == isolated_vowels[i].letter)
            add_override(rule, 1, -1, isolated_vowels[i].letter, isolated_vowels[i].accent);
    }
    /* "é" is written by the shared rule of every word starting with é; the
     * isolated case is the TTT rule of its continuation tree. */
    const char *rule = tab_ruleTTT(tab_getPtTabDeb(tab_tWin(0xE9)));
    if (rule && rule[0] == EE && rule[1] == RB) {
        rule = tab_ruleTTT(tab_getPtArbre((char *)rule + 2));
        if (rule)
            add_override(rule, 0, EE, EE, 1);
    }
}

static char *rule_override(char *rule) {
    for (size_t i = 0; i < rule_override_count; i++)
        if (rule == rule_overrides[i].rule) {
            if (rule_overrides[i].retract >= 0) {
                if (!iEcr || texteEcr[iEcr - 1] != rule_overrides[i].retract + 65)
                    return rule;
                iEcr--;
            }
            return rule_overrides[i].body;
        }
    return rule;
}

static char *tab__Huit1() { return tTab + a_Huit1; }

static char *tab__Dix1() { return tTab + a_Dix1; }

static char *tab_XChif() { return tTab + aXChif; }

static void minMajNFois(char *texteAlphaLec, char *texteAlphaEcr) {
    short nbFois;
    char chaine[8]; // pour nbFois
    short iLecCherche;

    texteLec = texteAlphaLec;
    texteEcr = texteAlphaEcr;
    iLec = 0;
    iEcr = 0;
    while (true) {
        carac = (unsigned char)texteLec[iLec];
        if (carac)
            iLec++;
        if ((char)carac == MARQ_MARQ) {
            if (texteLec[iLec] == MARQ_INDEX) {
                if (iEcr < NM_CAR_TEX_2) {
                    texteEcr[iEcr++] = MARQ_MARQ;
                    texteEcr[iEcr++] = MARQ_INDEX;
                }
                iLec++;
                continue;
            }
        }
        if (carac == 0)
            break;
        if ((carac < 48 && carac > 32) || carac > 57) { // pas les chiffres ni esp ni tab
            if (texteLec[iLec] == carac) {
                if (texteLec[iLec + 1] == carac) {
                    nbFois = 3; // déjà trois fois
                    for (iLecCherche = iLec + 2; texteLec[iLecCherche] == carac; iLecCherche++)
                        nbFois++;
                    if (iEcr < NM_CAR_TEX_8) {
                        decimal(chaine, nbFois);
                        texteEcr[iEcr] = 0; // pour concaténer
                        strcat(texteEcr, chaine);
                        iEcr += strlen(chaine);
                        texteEcr[iEcr++] = FOIS;
                        texteEcr[iEcr++] = (char)tab_tWin(carac);
                        texteEcr[iEcr++] = ' ';
                        iLec = iLecCherche;
                        continue;
                    }
                }
            }
        }
        carac = tab_tWin(carac);
        ecrit(carac);
    }
    texteEcr[iEcr] = 0;
}

static void graphemePhoneme(char *texte, char *texPhon) {
    char cArbre;
    unsigned char caracAnc = 0;
    unsigned char categAnc = 0;
    char *ptArbreAnc = NULL;
    short iLecAnc = 0;
    short iEcrAnc = 0;
    bool sigle = false; // pas de sigle au départ
    short bavard = 0;
    short abrev = 1 - 0;

    texteLec = texte;
    texteEcr = texPhon;
    ctBava = 0;  // pas de bavard provisoire
    liaison = 0; // normal
    iLec = 0;
    iEcr = 0;
    ptArbre = tab_getPtTabDeb(' '); // comme si on était derrière un espace
    if (!carSuiv()) {
        texPhon[iEcr] = 0;
        return;
    }
    while (true) { // traite caractère par caractère
        while (true) { // cherche la règle qui s'applique
            cArbre = *ptArbre++;
            if ((unsigned char)cArbre == carac) {
                iLecAnc = iLec;
                iEcrAnc = iEcr;
                ptArbreAnc = ptArbre;
                caracAnc = carac;
                categAnc = categ; // garde anciennes valeurs pour dico non
                carSuiv();
                break; // caractère trouvé
            }
            if (cArbre <= TTT && cArbre >= categ)
                break; // catégorie trouvée
            if (cArbre == BVRD && bavard)
                break;
            if (cArbre == NABR && !abrev)
                break;
            while (true) { // règle non validée, passer à la suivante
                cArbre = *ptArbre++;
                if (cArbre >= PG)
                    break;
            }
            ptArbre += 2;
        }
        while (true) {     // traite la règle validée et prépare l'adresse suivante
            ptArbre = rule_override(ptArbre);
            while (true) { // lit les phonèmes et les écrit
                cArbre = *ptArbre++;
                if (cArbre >= PG)
                    break;
                ecritPhon(cArbre);
            }
            ptArbre = tab_getPtArbre(ptArbre); // prépare adresse suivante
            if (cArbre == RB) {                // arbre : branche
                break;
            } else if (cArbre == PG) { // arbre : racine
                if (*ptArbre > 0)
                    liaison = *ptArbre;
                ptArbre++;
                if (carac == 0) { // fin de l'énoncé
                    liaison = -1;
                    break;
                }
                if (categ == CHIF)
                    traiteNombre();
                else {
                    ptArbre = tab_getPtArbreRoot(ptArbre, carac); // racine
                    if (ptArbre == tab_getPtTabDeb(carac)) {      // début de mot : cherche si sigle
                        if (categ == CNS) {
                            iLecAnc = iLec;
                            iEcrAnc = iEcr;
                            while (true) {
                                carSuiv();
                                if (categ != CNS) {
                                    if (categ == VOY || categ == VOYM)
                                        sigle = false;
                                    else
                                        sigle = true;
                                    break;
                                }
                            }
                            iLec = iLecAnc;
                            iEcr = iEcrAnc;
                        }
                    }
                    carSuiv();
                    if (sigle) {
                        if (categ == LET)
                            categ = PONC;
                        else
                            sigle = false;
                    }
                }
                break;
            } else if (cArbre == DC) {  // dictionnaire
                while (*ptArbre != 0) { // vérifie si la chaine correspond
                    if ((unsigned char)*ptArbre != carac)
                        break;
                    ptArbre++;
                    carSuiv();
                }
                if (*ptArbre == 0) {
                    ptArbre++;
                    sigle = false;
                    continue; // le mot est dans le dico
                } else {      // dico non, restaure les anciennes valeurs
                    carac = caracAnc;
                    categ = categAnc;
                    ptArbre = ptArbreAnc + 3; // début règle suivante
                    iLec = iLecAnc;
                    iEcr = iEcrAnc;
                    break;
                }
            }
        }
        if (liaison < 0)
            break;
    }
    texPhon[iEcr] = 0;
}

static void traiteNombre() {
    unsigned short int nbC = 0;   // nombre de chiffres dans le dernier groupe de 3 chiffres (1 à 3)
    unsigned short int nbGr3 = 0; // nombre de groupes de 3 chiffres (1 à 3)
    unsigned short int iN = 0;    // indice chiffre du nombre
    unsigned short int groupe = 0;    // compte les chiffres dans un groupe continu
    bool petitGroupeInterdit = false; // groupe <3 chiffres interdit (sauf au début)
    short iLecPrec = 0;
    short iEcrPrec = 0;
    short iLecAnc = 0;
    short iEcrAnc = 0;
    unsigned short int nbCAnc = 0;
    unsigned short int nbGr3Anc = 0;
    char c;  // chiffre
    char c1; // chiffre
    char c2; // chiffre

    while (
        true) { // Fabrique un nombre de 12 chiffres max, en enlevant les séparateurs en mode compta
        /* A space may introduce a thousands group, never a punctuation mark
         * or a letter. Speech Dispatcher's symbol pass can insert "5 ,58". */
        if (categ != CHIF) {
            iLec = iLecPrec;
            iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
            break;           // et arrête le nombre
        }
        if (nbC == 3) {
            if (nbGr3 == 3) {              // dépasse la taille max du nombre
                if (petitGroupeInterdit) { // il y a déjà eu au moins un séparateur (mode compta)
                    nbGr3 = nbGr3Anc;
                    nbC = nbCAnc;
                    iLec = iLecAnc;
                    iEcr = iEcrAnc; // reprend les valeurs anciennes
                    break;          // et arrête le nombre
                }
                iLec = iLecPrec;
                iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                break;           // et arrête le nombre
            } else {
                nbGr3++;
                nbC = 1;
            }
        } else
            nbC++;
        nombre[iN++] = carac;
        groupe++;
        iLecPrec = iLec;
        iEcrPrec = iEcr; // sauvegarde
        carSuiv();
        if (1) {
            if (carac == ' ') {                // séparateur
                if (groupe != 3) {             // pas groupe 3 chiffres
                    if (petitGroupeInterdit) { // c'est interdit
                        nbGr3 = nbGr3Anc;
                        nbC = nbCAnc;
                        iLec = iLecAnc;
                        iEcr = iEcrAnc;      // reprend les valeurs anciennes
                        break;               // et arrête le nombre
                    } else if (groupe > 3) { // groupe>3
                        iLec = iLecPrec;
                        iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                        break;           // et arrête le nombre
                    }
                }
                groupe = 0;
                petitGroupeInterdit = true; // maintenant, groupe<3 chiffres interdit
                nbGr3Anc = nbGr3;
                nbCAnc = nbC;
                iLecAnc = iLecPrec;
                iEcrAnc = iEcrPrec; // sauve les valeurs actuelles (avant carSuiv précédent)
                carSuiv();
            } else if (categ != CHIF) {                    // fin obligée
                if (groupe == 3 || !petitGroupeInterdit) { // groupe OK
                    iLec = iLecPrec;
                    iEcr = iEcrPrec; // reprend les valeurs avant carSuiv
                    break;           // et arrête le nombre
                } else {             // interdit
                    nbGr3 = nbGr3Anc;
                    nbC = nbCAnc;
                    iLec = iLecAnc;
                    iEcr = iEcrAnc; // reprend les valeurs anciennes
                    break;          // et arrête le nombre
                }
            }
        }
    }
    iN = 0;
    while (nombre[iN] == '0') { // nombre commençant par zéro
        phonChif(tab_ch01());   //"zéro"
        iN++;
        nbC--;
        if (nbC == 0) {
            nbC = 3;
            if (nbGr3 == 0) {
                finNombre(&(iN));
                return;
            } else {
                nbGr3--;
            }
        }
    }
    while (true) {
        c = nombre[iN++]; // nbC=1, 2, 3
        switch (nbC) {
        case 3: //... nnn ...
            nbC--;
            if (c == '0')
                continue;              //... 0nn ...
            else if (c == '1')         //... 1nn ...
                phonChif(tab_ch100()); //"sâ[t]"
            else {
                phonChif(tab_chListUnit(c)); //"de:", "trwa:", "kat", ...
                phonChif(tab_ch100Z());      //"sâ:[z]"
            }
            break;
        case 2: //...nn ...
            nbC--;
            if (c == '0')
                continue;                          //...0n ...
            else if (c == '1' && nombre[iN] < '7') //...10 ... à ...16 ...
                break;
            else {
                phonChif(tab_chListDiz(c)); //"diz", "vî:[t]", "trât", "karâ:t", ...
                if (c == '2') {             //...2n ...
                    if (nombre[iN] > '0')   //...21 ... à ...29 ...
                        ecritPhon(TT);
                } else if ((c == '7' || c == '9') && nombre[iN] > '6')
                    phonChif(tab_chListDiz('1'));
            }
            break;
        case 1: //...n ...
            nbC--;
            c1 = 0;
            c2 = 0;
            if (iN > 1)
                c1 = nombre[iN - 2];
            if (iN > 2)
                c2 = nombre[iN - 3];
            if (c == '1') {
                if (c1 > '1' && c1 < '8') { //...21 ... à ...71 ...
                    ecritPhon(EE);
                    ecritPhon(PL);    //"é:"
                } else if (c1 == '8') //...81 ...
                    ecritPhon(VR);    //","
            }
            if (c < '7' &&
                (c1 == '1' || c1 == '7' ||
                 c1 == '9')) //...10 ... à ...16 ..., ...70 ... à ...76 ..., ...90 ... à ...96 ...
                phonChif(tab_chListUnit(c + 10)); //"di", "ô:z", "dùz", ...
            else if (iN == 1 ||
                     (c1 == '0' && c2 == '0')) { // n, n nnn, n nnn nnn, n nnn nnn nnn, ... 00n ...
                if (c == '1') {                  // 1, 1 nnn, 1 nnn nnn, 1 nnn nnn nnn, ... 001 ...
                    if (nbGr3 != 1)
                        phonChif(tab_ch1S()); //"î:"
                } else
                    phonChif(tab_chListUnit(c)); //"de:", "trwa:", "kat", ...
            } else
                phonChif(tab_chListUnit(c)); //"î", "de:", "trwa:", "kat", ...
            if (nbGr3 == 0) {
                finNombre(&(iN));
                return;
            } else { //... nnn
                nbGr3--;
                nbC = 3;
                if (c != '0' || c1 != '0' || c2 != '0') // sauf ... 000 ...
                    phonChif(tab_chListMil(nbGr3));     //"mil", "milyô", "milya::r"
            }
        }
    }
}

static void finNombre(short unsigned int *iN) {
    char c;
    char c1;

    c = nombre[--(*iN)];
    if (c == '8')
        ptArbre = tab__Huit1();
    else {
        c1 = 0;
        if ((*iN) > 0)
            c1 = nombre[(*iN) - 1];
        if ((c1 == '0' || (c1 > '1' && c1 < '7') || c1 == '8' || c1 == 0) && c == '5')
            ecritPhon(KK);
        if (((c1 == '1' || c1 == '7' || c1 == '9') && c == '0') ||
            (c1 != '1' && c1 != '7' && c1 != '9' && c == '6'))
            ptArbre = tab__Dix1();
        else
            ptArbre = tab_XChif();
    }
    carSuiv();
}

static void phonChif(char *ptChif) {
    char c;

    if (*ptChif == 80)
        return; //...0
    while (true) {
        c = *ptChif++;
        if (c == 80)
            break;
        ecritPhon(c);
    }
    liaison = *ptChif;
}

static unsigned char carSuiv(void) {
    while (true) {
        carac = (unsigned char)texteLec[iLec];
        if (carac)
            iLec++;
        if ((char)carac == MARQ_MARQ) {
            if (texteLec[iLec] == MARQ_INDEX) {
                ecrit(INDEX);
                iLec++;
                continue;
            }
        }
        break;
    }
    categ = tab_tCat(carac);
    return carac;
}

static void ecritPhon(char c) {
    if (liaison > 0) {
        if ((liaison < 10 && c > 9) || (liaison > 9 && c < 10)) { // liaison possible
            ecrit(liaison + 65); // évite 0 et rend visibles les caractères
        }
    }
    ecrit(c + 65); // évite 0 et rend visibles les caractères
    liaison = 0;
}

static void ecrit(char c) {
    if (iEcr < NM_CAR_TEX_1)
        texteEcr[iEcr++] = c;
    else
        failed = 1;
}

static bool traiteTextePhonetique(char *chainePhon) {

    filter_state[0] = filter_state[1] = 32768; // init car utilisés dès le 1er échantillon

#ifdef S3_INTEGER_PERIOD
    hau_milli = tab_tabHau(engine_pitch - HAUTEUR_REF);
    vit_milli = tab_tabVit(engine_rate - DEBIT_REF);
#else
    mulHauteur = (float)tab_tabHau(engine_pitch - HAUTEUR_REF) / 1000;
    mulDebit = (float)tab_tabVit(engine_rate - DEBIT_REF) / 1000;
#endif
    texPhon = chainePhon;
    if (!strcmp(texPhon, "IXIXIZTEESI_RI"))
        strcpy(texPhon, "XDNIZXB[IZ\\IZITS[IZ\\[HR[BVCTMKG]J]SI[VDS_");
    iLecPhon = 0; // 1er caractère du texte phonétique
    iLecPhonCroit = iLecPhon;
    phonDCroit = VR; // le texte commence par une virgule (va passer gauche)
    catDCroit = tab_categ(phonDCroit);
    nSousDiphCroit = 2;                       // avant le 1er sous-diphone (prochain=0)
    nSegIni.DSCroit = tab_finTim(phonDCroit); // segment de VR
    nSeg.DSCroit = nSegIni.DSCroit;
    marq.DSCroit = true;  // début de sous-diphone activé
    marq.FSCroit = false; // pas de fin de sous-diphone
    if (!nouveauSousDiph(&(iLecPhonCroit), &(phonGCroit), &(phonDCroit), &(catGCroit), &(catDCroit),
                         &(nSousDiphCroit), &(nSegIni.DSCroit), &(nSeg.DSCroit), &(ptAmp.DSCroit),
                         &(amp.DSCroit))) // fournit ts les DS
        return false;
    ptAmp.DSCroit++; // prépare la prochaine période
    initDecroitSurCroit();
    amp.DSDecroit = amp.DSCroit;
    amp.FSDecroit = amp.FSCroit = 0;             // pas de fin de sous-diphone
    ptSeg.DSCroit = voice_segment(nSeg.DSCroit); // segment de VR
    perio.DSDecroit = perio.DSCroit = (unsigned char)*ptSeg.DSCroit++;
    ptSeg.DSDecroit = ptSeg.DSCroit;
#ifdef S3_INTEGER_PERIOD
    perioResult = (unsigned char)(perio.DSCroit * 1000L / hau_milli); // troncature, comme le flottant
    xEcr_milli = (perioResult / 2) * vit_milli; // simule l'écriture à l'échelle de la lecture
    allonge_milli = -xEcr_milli;                // pour l'allongement des voyelles
#else
    perioResult = (unsigned char)(perio.DSCroit / mulHauteur);
    xEcrEchelleDeLec =
        perioResult / 2 * mulDebit; // simule l'écriture (envoi des éch) à l'échelle de la lecture
    allonge = -xEcrEchelleDeLec;    // pour l'allongement des voyelles
#endif

    return true;
}

static bool traiteUnePeriode() {
#ifndef S3_BLOCK
    short ech;        // échantillon final à envoyer à la carte son
    short echDecroit; // échantillon de la période Decroit
    short echCroit;   // échantillon de la période Croit
    short echT;
#endif
    bool result = true;
#ifndef S3_SCALAR_WINDOW
    const unsigned short *ds_dec, *fs_dec, *ds_croit, *fs_croit;
#endif
#ifdef S3_PROFILE
    unsigned long t0 = s3_ticks(), t1, t2;
#endif

    while (true) {
#ifdef S3_INTEGER_PERIOD
        if (xEcr_milli < 0)
            break;
#else
        if (xEcrEchelleDeLec < 0)
            break;
#endif
        if (!perioSuiv(&(marq.DSCroit), &(marq.FSCroit), &(iLecPhonCroit), &(phonGCroit),
                       &(phonDCroit), &(catGCroit), &(catDCroit), &(nSousDiphCroit),
                       &(nSegIni.DSCroit), &(nSegIni.FSCroit), &(nSeg.DSCroit), &(nSeg.FSCroit),
                       &(ptSeg.DSCroit), &(ptSeg.FSCroit), &(perio.DSCroit), &(perio.FSCroit),
                       &(ptAmp.DSCroit), &(ptAmp.FSCroit), &(amp.DSCroit), &(amp.FSCroit), true))
            break; // fin du texte : on garde la dernière période valide
    }
#ifndef S3_SCALAR_WINDOW
    window_next_period();
    ds_dec = prepare_window(perio.DSDecroit, amp.DSDecroit);
    fs_dec = prepare_window(perio.FSDecroit, amp.FSDecroit);
    ds_croit = prepare_window(perio.DSCroit, amp.DSCroit);
    fs_croit = prepare_window(perio.FSCroit, amp.FSCroit);
#endif
#ifdef S3_PROFILE
    t1 = s3_ticks();
    s3_prof_periods++;
#endif
#ifdef S3_TRACE_PERIODS
    /* Débogage natif : période résultante et longueurs de fenêtre demandées. */
    fprintf(stderr, "P %u %u %u %u %u %u %u %u %u\n", perioResult, perio.DSDecroit, amp.DSDecroit,
            perio.FSDecroit, amp.FSDecroit, perio.DSCroit, amp.DSCroit, perio.FSCroit, amp.FSCroit);
#endif
#ifdef S3_BLOCK
    /* Un appel par contribution et par période, puis gain/filtre/écrêtage en bloc.
     * period_n vaut 0 ici : s3_read vide le tampon avant de demander une période. */
    memset(mix, 0, perioResult * sizeof(short));
    s3_mix(mix, ptSeg.DSDecroit, perio.DSDecroit, perioResult, amp.DSDecroit, 0, ds_dec);
    s3_mix(mix, ptSeg.FSDecroit, perio.FSDecroit, perioResult, amp.FSDecroit, 0, fs_dec);
    s3_mix(mix, ptSeg.DSCroit, perio.DSCroit, perioResult, amp.DSCroit, 1, ds_croit);
    s3_mix(mix, ptSeg.FSCroit, perio.FSCroit, perioResult, amp.FSCroit, 1, fs_croit);
    s3_output_block(mix, period_pcm + period_n, perioResult, filter_state);
    period_n += perioResult;
    pcm_size += perioResult;
#else
    for (iEch = 0; iEch < perioResult; iEch++) {
#ifdef S3_SCALAR_WINDOW
        echDecroit = calculeEchPerioDecroit(iEch, ptSeg.DSDecroit, perio.DSDecroit, amp.DSDecroit) +
                     calculeEchPerioDecroit(iEch, ptSeg.FSDecroit, perio.FSDecroit, amp.FSDecroit);
        echCroit = calculeEchPerioCroit(iEch, ptSeg.DSCroit, perio.DSCroit, amp.DSCroit) +
                   calculeEchPerioCroit(iEch, ptSeg.FSCroit, perio.FSCroit, amp.FSCroit);
#else
        echDecroit = s3_weighted(iEch, ptSeg.DSDecroit, perio.DSDecroit, perioResult,
                                 amp.DSDecroit, 0, ds_dec) +
                     s3_weighted(iEch, ptSeg.FSDecroit, perio.FSDecroit, perioResult,
                                 amp.FSDecroit, 0, fs_dec);
        echCroit = s3_weighted(iEch, ptSeg.DSCroit, perio.DSCroit, perioResult,
                               amp.DSCroit, 1, ds_croit) +
                   s3_weighted(iEch, ptSeg.FSCroit, perio.FSCroit, perioResult,
                               amp.FSCroit, 1, fs_croit);
#endif
        echT = echDecroit + echCroit;
        ech = s3_output(echT, filter_state);
        period_pcm[period_n++] = ech;
        pcm_size++;
    }
#endif
#ifdef S3_PROFILE
    t2 = s3_ticks();
    s3_prof_loop_ticks += t2-t1;
#endif
    ampAnc = amp; // on repartira de l'amplitude actuelle (4 .valeurs)
#ifdef S3_INTEGER_PERIOD
    if (allonge_milli > 0)
        allonge_milli -= perioResult * vit_milli; // cas du ':'
    else
        xEcr_milli += perioResult * vit_milli; // période écrite
#else
    if (allonge > 0)
        allonge -= perioResult * mulDebit; // cas du ':'
    else
        xEcrEchelleDeLec += perioResult * mulDebit; // période écrite
#endif
    if (iLecPhonCroit ==
        iLecPhon) { // Croit = courant : on s'y réfère avant de calculer Decroit (cas général)
        initDecroitSurCroit(); // Initialise Décroit sur Croit
        if (!perioSuiv(&(marq.DSDecroit), &(marq.FSDecroit), &(iLecPhonDecroit), &(phonGDecroit),
                       &(phonDDecroit), &(catGDecroit), &(catDDecroit), &(nSousDiphDecroit),
                       &(nSegIni.DSDecroit), &(nSegIni.FSDecroit), &(nSeg.DSDecroit),
                       &(nSeg.FSDecroit), &(ptSeg.DSDecroit), &(ptSeg.FSDecroit),
                       &(perio.DSDecroit), &(perio.FSDecroit), &(ptAmp.DSDecroit),
                       &(ptAmp.FSDecroit), &(amp.DSDecroit), &(amp.FSDecroit), false))
            result = false; // fin du message phonétique
    } // sinon le décroit est à jour
#ifdef S3_PROFILE
    s3_prof_control_ticks += (t1-t0) + (s3_ticks()-t2);
#endif
    return result;
}

static void initDecroitSurCroit() {
    iLecPhonDecroit = iLecPhonCroit;
    phonDDecroit = phonDCroit;
    phonGDecroit = phonGCroit;
    catDDecroit = catDCroit;
    catGDecroit = catGCroit;
    nSousDiphDecroit = nSousDiphCroit;
    marq.DSDecroit = marq.DSCroit;
    marq.FSDecroit = marq.FSCroit;
    ptAmp.DSDecroit = ptAmp.DSCroit;
    ptAmp.FSDecroit = ptAmp.FSCroit;
    nSegIni.DSDecroit = nSegIni.DSCroit;
    nSegIni.FSDecroit = nSegIni.FSCroit;
    nSeg.DSDecroit = nSeg.DSCroit;
    nSeg.FSDecroit = nSeg.FSCroit;
}

static bool perioSuiv(bool *marqDS, bool *marqFS, long *iLecPhonX, char *phonG, char *phonD,
                      char *catG, char *catD, char *nSousDiph, unsigned char *nSegIniDS,
                      unsigned char *nSegIniFS, unsigned char *nSegDS, unsigned char *nSegFS,
                      char **ptSegDS, char **ptSegFS, unsigned char *perioDS,
                      unsigned char *perioFS, unsigned char **ptAmpDS, unsigned char **ptAmpFS,
                      unsigned char *ampDS, unsigned char *ampFS, bool mCroit) {
    if (!(*marqDS) && !(*marqFS))
        return false;
    (*ampDS) = (*ampFS) = 0; // par défaut si pas marq
    if ((*marqDS)) {
        while ((*marqDS)) {
            (*ampDS) = *(*ptAmpDS);
            if (((*ampDS) & 128) == 128) { // début du sous-phonème suivant
                (*ampDS) -= 128;
                (*marqFS) = (*marqDS);
                (*ptAmpFS) = (*ptAmpDS);
                (*ampFS) = (*ampDS);
                (*nSegIniFS) = (*nSegIniDS);
                (*nSegFS) = (*nSegDS);
                (*ptSegFS) = (*ptSegDS);
                (*perioFS) = (*perioDS);
                (*ampDS) = 0; // par defaut si pas marq
                (*marqDS) =
                    nouveauSousDiph(&((*iLecPhonX)), &((*phonG)), &((*phonD)), &((*catG)),
                                    &((*catD)), &((*nSousDiph)), &((*nSegIniDS)), &((*nSegDS)),
                                    &((*ptAmpDS)), &((*ampDS))); // fournit ts les DS
            } else
                break;
        }
        if (((*ampDS) & 32) == 32) { // phonème avec bruit (fshvzjr), segment suivant
            (*ampDS) -= 32;
            (*nSegDS)++;
        } else
            (*nSegDS) = (*nSegIniDS);
        (*ptSegDS) = voice_segment((*nSegDS));
        (*perioDS) = (unsigned char)*(*ptSegDS)++;
    }
    if ((*marqFS)) {
        (*ampFS) = *(*ptAmpFS) & 127;
        if (((*ampFS) & 32) == 32) { // phonème avec bruit (fshvzjr), segment suivant
            (*ampFS) -= 32;
            (*nSegFS)++;
        } else
            (*nSegFS) = (*nSegIniFS);
        if (((*ampFS) & 64) == 64) { // dernière période du sous-phonème
            (*ampFS) -= 64;
            (*marqFS) = false; // pour le prochain tour
        }
        (*ptSegFS) = voice_segment((*nSegFS));
        (*perioFS) = (unsigned char)*(*ptSegFS)++;
    }
    if ((*marqDS))
        (*ptAmpDS)++;
    if ((*marqFS))
        (*ptAmpFS)++;
    unsigned sum_amp = amp.DSDecroit + amp.FSDecroit + amp.DSCroit + amp.FSCroit;
#ifdef S3_INTEGER_PERIOD
    if (sum_amp) {
        /* arrondi(moyenne pondérée des périodes × 1000 / tabHau) en rationnel exact :
         * numérateur <= 31620 × 1000, dénominateur <= 124 × 1682, tout en 32 bits. */
        unsigned long num = ((unsigned long)perio.DSDecroit * amp.DSDecroit +
                             (unsigned long)perio.FSDecroit * amp.FSDecroit +
                             (unsigned long)perio.DSCroit * amp.DSCroit +
                             (unsigned long)perio.FSCroit * amp.FSCroit) * 1000UL;
        unsigned long den = (unsigned long)sum_amp * hau_milli;
        perioResult = (unsigned char)((2 * num + den) / (2 * den));
    } else
        perioResult = 0;
    if (mCroit)
        xEcr_milli -= perioResult * hau_milli; // période lue mais pas forcément écrite
#else
    perioBase =
        sum_amp ? ((float)perio.DSDecroit * amp.DSDecroit + (float)perio.FSDecroit * amp.FSDecroit +
                   (float)perio.DSCroit * amp.DSCroit + (float)perio.FSCroit * amp.FSCroit) /
                      sum_amp
                : 0;
    perioResult = (unsigned char)(perioBase / mulHauteur + .5);
    if (mCroit)
        xEcrEchelleDeLec -= perioResult * mulHauteur; // période lue mais pas forcément écrite
#endif
    return true;
}

static bool nouveauSousDiph(long *iLecPhonX, char *phonG, char *phonD, char *catG, char *catD,
                            char *nSousDiph, unsigned char *nSegIniDS, unsigned char *nSegDS,
                            unsigned char **ptAmpDS, unsigned char *ampDS) {
    short nAmp;

    if ((*nSousDiph) == 0) {
        (*nSousDiph)++;
        (*ptAmpDS) = tab_getPtAmp(tab_traAmp((*catG), (*catD)));
        (*ampDS) = *(*ptAmpDS);
        (*nSegIniDS) = tab_traTim((*phonG), (*phonD));
    } else if ((*nSousDiph) == 1) {
        (*nSousDiph)++;
        (*ptAmpDS) = tab_getPtAmp(tab_debAmp((*catG), (*catD)));
        (*ampDS) = *(*ptAmpDS);
        (*nSegIniDS) = tab_debTim((*phonD));
    } else {
        (*nSousDiph) = 0;
        (*phonG) = (*phonD);
        (*catG) = (*catD);
        (*phonD) = phonSuiv(&((*iLecPhonX)), (*phonG));
        if ((*phonD) == -1)
            return false;
        (*catD) = tab_categ((*phonD));
        nAmp = tab_finAmp((*catG), (*catD));
        if (nAmp > 100) {
            nAmp -= 100;
            (*nSousDiph)++;
        }
        (*ptAmpDS) = tab_getPtAmp(nAmp);
        (*ampDS) = *(*ptAmpDS);
        if ((*phonG) == YY && (*phonD) > YY && (*phonD) < VR)
            (*nSegIniDS) = tab_traTim(II, EU);
        else
            (*nSegIniDS) = tab_finTim((*phonG));
    }
    (*nSegDS) = (*nSegIniDS);
    return true;
}

static char phonSuiv(long *iLecPhonX, char phonG) {

    if ((*iLecPhonX) < iLecPhon) { // phonème déjà trouvé pour Croit ou Décroit
        (*iLecPhonX) = iLecPhon;
        return phonMem; // gardé en mémoire
    } // phonème suivant
    while (true) {
        phonMem = texPhon[iLecPhon];
        if (phonMem == 0) {
            phonMem = -1;
            break; // fin du texte phonétique
        }
        phonMem -= 65;
        iLecPhon++;
        if ((phonMem < 0 || phonMem > PL) && phonMem + 65 != INDEX)
            continue;
        if (phonMem < YY && phonMem == phonG)
            phonMem = PL;
        if (phonMem == PL)
#ifdef S3_INTEGER_PERIOD
            allonge_milli += 250000L; //+25 ms à débit normal (~25 %)
#else
            allonge += 250; //+25 ms à débit normal (~25 %)
#endif
        else if (phonMem + 65 == INDEX) {
            if (word_seen < word_count)
                word_samples[word_seen++] = pcm_size;
        } else
            break; // rien de particulier donc phonème
    }
    (*iLecPhonX) = iLecPhon;
    if (phonMem == -1 && phonG != VR)
        phonMem = VR;
    return phonMem;
}

#ifdef S3_SCALAR_WINDOW
static short calculeEchPerioDecroit(short x, char *seg, unsigned char period,
                                    unsigned char amplitude) {
    return s3_window(x, seg, period, perioResult, amplitude, 0);
}
static short calculeEchPerioCroit(short x, char *seg, unsigned char period,
                                  unsigned char amplitude) {
    return s3_window(x, seg, period, perioResult, amplitude, 1);
}
#endif

void s3_init(void) {
#ifndef S3_SCALAR_WINDOW
    memset(window_length, 0, sizeof(window_length));
#ifdef S3_WINDOW_CACHE
    memset(window_stamp, 0, sizeof(window_stamp));
    memset(window_period, 0, sizeof(window_period));
    window_clock = window_serial = 0;
#endif
#endif
    tTab = (char *)voice_tables;
    aTWin = readShort(tTab + 0);
    aTCat = readShort(tTab + 2);
    aTPhon1 = readShort(tTab + 4);
    aTPhon2 = readShort(tTab + 6);
    aTCatP1 = readShort(tTab + 8);
    aTCatP2 = readShort(tTab + 10);
    aChLst1 = readShort(tTab + 12);
    aChLst2 = readShort(tTab + 14);
    aChLst3 = readShort(tTab + 16);
    aCh01 = readShort(tTab + 18);
    aCh1S = readShort(tTab + 20);
    aCh100 = readShort(tTab + 22);
    aCh100Z = readShort(tTab + 24);
    aTabVol = readShort(tTab + 26);
    aTabVit = readShort(tTab + 28);
    aTabHau = readShort(tTab + 30);
    aCateg = readShort(tTab + 32);
    aFinAmp = readShort(tTab + 34);
    aFinTim = readShort(tTab + 36);
    aTraAmp = readShort(tTab + 38);
    aTraTim = readShort(tTab + 40);
    aDebAmp = readShort(tTab + 42);
    aDebTim = readShort(tTab + 44);
    aTabAmp = readShort(tTab + 46);
    aAdAmp = readShort(tTab + 48);
    aTabDeb = readShort(tTab + 50);
    a_Huit1 = readShort(tTab + 52);
    a_Dix1 = readShort(tTab + 54);
    aXChif = readShort(tTab + 56);
    lgTab = readShort(tTab + 58);
    FOIS = (unsigned char)tTab[60];
    init_rule_overrides();
}

/* Un seul moteur ; le texte phonétique reste vivant entre deux appels read.
 * Pas de malloc, ni de fichier, ni de matériel dans ce module. */
static char indexed[1024], alpha[NM_CAR_TEX], phon[NM_CAR_TEX];
int s3_start(const char *latin1, int rate, int pitch) {
    unsigned n, j = 0, i;
    int word = 0;
    if (active) return S3_BUSY;
    if (!latin1 || rate < 0 || rate > 15 || pitch < 0 || pitch > 15) return S3_INVALID;
    for (n = 0; latin1[n]; n++) {
        if (n == 240 || (unsigned char)latin1[n] == 248 ||
                        (unsigned char)latin1[n] == 237) return S3_INVALID;
    }
    memset(alpha, 0, sizeof(alpha));
    memset(phon, 0, sizeof(phon));
    word_count = word_seen = 0;
    for (i = 0; i < n; i++) {
        int valid = latin1[i] != ' ' && latin1[i] != '\n' && latin1[i] != '\r';
        if (!valid && word) {
            word_bytes[word_count++] = i;
            indexed[j++] = MARQ_MARQ;
            indexed[j++] = MARQ_INDEX;
        }
        word = valid;
        indexed[j++] = latin1[i];
    }
    word_bytes[word_count++] = n;
    indexed[j++] = MARQ_MARQ;
    indexed[j++] = MARQ_INDEX;
    indexed[j] = 0;
    engine_rate = rate;
    engine_pitch = pitch;
    pcm_size = 0;
    period_n = period_pos = 0;
    failed = 0;
    minMajNFois(indexed, alpha);
    graphemePhoneme(alpha, phon);
    if (failed) return S3_INVALID;
    active = traiteTextePhonetique(phon);
    return S3_OK;
}

unsigned s3_read(short *buffer, unsigned capacity) {
    unsigned n = 0;
    if (!buffer) return 0;
    while (n < capacity) {
        if (period_pos < period_n) {
            buffer[n++] = period_pcm[period_pos++];
        } else {
            period_n = period_pos = 0;
            if (!active) break;
            /* Le dernier appel peut produire des échantillons et annoncer la fin. */
            if (!traiteUnePeriode()) active = 0;
        }
    }
    return n;
}

void s3_cancel(void) { active = 0; period_n = period_pos = 0; }
const char *s3_variant(void) {
#ifdef S3_SCALAR_WINDOW
    return "scalaire";
#else
    static char name[48];
    strcpy(name, "fenetres");
#ifdef S3_BLOCK
    strcat(name, "+bloc-");
    strcat(name, s3_block_impl);
#endif
#ifdef S3_INTEGER_PERIOD
    strcat(name, "+entier");
#endif
#ifdef S3_WINDOW_CACHE
    sprintf(name + strlen(name), "+cache%d", S3_WINDOW_CACHE);
#endif
#ifdef S3_PROFILE
    strcat(name, "+profil");
#endif
    return name;
#endif
}
unsigned long s3_sample_at(unsigned offset) {
    unsigned i;
    unsigned long sample = 0;
    for (i = 0; i < word_seen && word_bytes[i] <= offset; i++) sample = word_samples[i];
    return sample;
}
