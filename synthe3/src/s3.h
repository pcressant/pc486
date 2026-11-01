/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC486_S3_H
#define PC486_S3_H

#define S3_OK 0
#define S3_INVALID (-1)
#define S3_BUSY (-2)
#define S3_HZ 10000

/* API mono-instance, appels sérialisés, pointeurs far en modèle large DOS.
 * Entrée Latin-1, 240 octets au plus ; 0xED et 0xF8 réservés aux marques.
 * PCM signé 16 bits, mono 10 kHz. read() ne touche à aucun périphérique.
 * Avec un tampon valide et une capacité non nulle, read == 0 : fin.
 * cancel() abandonne aussi le bloc en attente.
 * L'appelant peut céder la main entre deux read et choisir une petite capacité.
 * Un start pendant une lecture renvoie BUSY sans altérer le moteur. */
void s3_init(void);
int s3_start(const char *text, int rate, int pitch);
unsigned s3_read(short *buffer, unsigned capacity);
void s3_cancel(void);
/* Position produite, pas position déjà jouée : à synchroniser par le pilote. */
unsigned long s3_sample_at(unsigned offset);
/* Nom de la variante compilée du moteur (options S3_* ci-dessous), pour les rapports. */
const char *s3_variant(void);

/* ABI assembleur DOS : __cdecl, fonctions far, int=16 et long=32 bits. */
#ifdef __WATCOMC__
#define S3_CDECL __cdecl
#else
#define S3_CDECL
#endif
short S3_CDECL s3_window(short x, const char *segment, unsigned source_period,
                         unsigned result_period, unsigned amplitude, unsigned rising);
long S3_CDECL s3_highpass(long input, long *state);
/* Gain historique fixe (volume moteur 10), filtre et saturation PCM. */
short S3_CDECL s3_output(short mixed, long *state);
/* Fenêtres exactes du port Q15 : préparation une fois, réutilisation sans division.
 * length=1..255 ; table d'au moins length mots. Retour 0 si longueur invalide.
 * Les bornes et les signes suivent s3_window, sans changer le moteur linguistique. */
int s3_weights(unsigned length, unsigned short *weights);
short S3_CDECL s3_weighted(short x, const char *segment, unsigned source_period,
    unsigned result_period, unsigned amplitude, unsigned rising, const unsigned short *weights);
/* Variante par période (S3_BLOCK) : mêmes résultats que result_period appels à
 * s3_weighted pour x = 0..result_period-1, ajoutés dans mix[] (somme des quatre
 * contributions, bornée à 4 × 31 × 127 en valeur absolue). Un seul appel par
 * contribution et par période : indices et bornes calculés une fois. */
void S3_CDECL s3_mix(short *mix, const char *segment, unsigned source_period,
    unsigned result_period, unsigned amplitude, unsigned rising, const unsigned short *weights);
/* count appels à s3_output enchaînés sur mix[] vers out[], même état de filtre. */
void S3_CDECL s3_output_block(const short *mix, short *out, unsigned count, long *state);
#endif
