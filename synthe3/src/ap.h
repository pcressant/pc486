/* SPDX-License-Identifier: GPL-3.0-or-later
 * Apollo II local : sous-ensemble du protocole réellement émis par Épaudos et
 * Épaulard, devant le moteur Synthé3. Aucune entrée/sortie, aucun matériel.
 *
 * Écrivain (le lecteur d'écran) : ap_put, ap_echo. Sous DOS, ce sont les deux
 * fonctions assembleur de s3res.asm, appelées depuis l'interruption du lecteur :
 * elles ne touchent qu'à AP_LINK, dont la disposition est donc fixe.
 * Producteur (pompe audio) : ap_cut, ap_render, ap_played, jamais réentrant.
 * Files à producteur et consommateur uniques ; chacun n'écrit que son indice.
 */
#ifndef PC486_AP_H
#define PC486_AP_H

#ifndef AP_INPUT
#define AP_INPUT 1024           /* octets du lecteur en attente ; puissance de deux */
#endif
#define AP_ECHOES 32            /* échos @E rendus au lecteur ; puissance de deux */
#define AP_MARKS 32             /* marques @E en attente de restitution */

typedef struct {
    volatile unsigned head;     /* écrivain : prochain octet écrit */
    volatile unsigned tail;     /* producteur : prochain octet lu */
    volatile unsigned cuts;     /* écrivain : Ctrl-X reçus, publiés après cut_at */
    volatile unsigned cut_at;   /* écrivain : position de la file après le dernier Ctrl-X.
                                   Un Ctrl-X est accepté même file pleine : il n'attend
                                   jamais derrière le texte qu'il annule. */
    volatile unsigned echo_head;    /* producteur : échos libérés */
    volatile unsigned echo_tail;    /* écrivain : échos lus */
    volatile unsigned dropped;  /* écrivain : octets refusés, file pleine */
    unsigned char echo[AP_ECHOES];
    unsigned char buf[AP_INPUT];
} AP_LINK;
extern AP_LINK ap_link;

/* Écrivain. ap_put : 1 accepté, 0 file pleine (réessayer ; jamais pour Ctrl-X).
   ap_echo : -1 si rien. */
int ap_put(unsigned char b);
int ap_echo(void);

typedef struct {
    unsigned long propositions; /* propositions confiées au moteur */
    unsigned long samples;      /* échantillons produits depuis le démarrage */
    unsigned cuts;              /* Ctrl-X appliqués */
    unsigned marks;             /* échos libérés */
    unsigned lost_marks;        /* marques libérées d'avance faute de place */
    unsigned refused;           /* propositions refusées par le moteur */
    int rate, pitch, voice, punct;  /* derniers réglages Apollo reçus (@W @F @V @P) */
} AP_STATS;
extern AP_STATS ap_stats;

/* Dernières propositions confiées au moteur, Latin-1, séparées par '|' : trace de
   diagnostic équivalente au journal d'Apolo-emu, lue par les essais. */
#define AP_TRACE 512
extern char ap_trace[AP_TRACE];
extern unsigned ap_trace_pos;

void ap_init(void);
/* Applique les Ctrl-X publiés : 1 si l'audio en file doit être abandonné ; la
   position produite repart alors de zéro, comme la position jouée du pilote. */
int ap_cut(void);
/* Échantillons PCM 16 bits de la parole qui suit, au plus capacity. 0 : rien à
   dire pour l'instant (proposition incomplète ou file vide). Les marques
   rencontrées sont rattachées à la position produite courante. */
unsigned ap_render(short *pcm, unsigned capacity);
/* Une proposition est en cours de calcul : un manque d'audio serait une
   sous-alimentation, pas une fin de parole. */
int ap_speaking(void);
/* Position effectivement jouée (échantillons de parole, silence exclu) : libère
   les échos des marques atteintes. Jamais appelé avec la position produite. */
void ap_played(unsigned long played);
/* Marques encore attendues (essais et diagnostic). */
unsigned ap_pending_marks(void);
#endif
