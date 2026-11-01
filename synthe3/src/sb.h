/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC486_S3_SB_H
#define PC486_S3_SB_H
#include <stdint.h>

#define SB_HALF 512
#define SB_OK 0
#define SB_ERROR 1
#define SB_CANCELLED 2
#define SB_UNDERRUN 3
#define SB_RUNNING 1
#define SB_DONE 2
#define SB_STARVED 4

/* Disposition partagée avec sbirq.asm, contrôlée à la compilation dans sb.c.
 * ready est publié en dernier, sous une très courte exclusion d'interruption.
 * L'ISR ne synthétise rien, ne fait aucun appel DOS et ne touche pas au x87. */
typedef struct {
    uint16_t base, channel, flags, playing;
    uint16_t ready[2], used[2], final[2];
    uint16_t completed;
    uint32_t played;
    uint16_t underruns;
} SB_STATE;
typedef struct {
    uint32_t produced, played;
    uint16_t underruns;
} SB_STATS;
typedef struct {
    uint32_t total_ticks, source_ticks, idle_ticks;
    uint32_t audio_ticks, first_buffer_ticks, submit_ticks, stop_command_ticks;
} SB_PROFILE;
typedef int (__cdecl *SB_SOURCE)(short *pcm, unsigned capacity, void *context);

/* DOS réel, un seul appelant, depuis le premier plan, interruptions permises.
 * SOURCE doit renvoyer 0..capacity échantillons, ou -1 en erreur.
 * Un bloc court termine la source ; le dernier demi-tampon est complété de silence.
 * Production et lecture se recouvrent. Pas de fichier audio intermédiaire.
 * Ctrl ou une touche interrompt ; retour 3 si le producteur n'a pas suivi.
 * Les statistiques finales excluent le silence ajouté en fin de lecture. */
int sb_run(SB_SOURCE source, void *context, SB_STATS *stats);
/* Variante instrumentée, banc PIT/8254 ; ne mesure pas le haut-parleur.
 * source : temps écoulé dans le producteur ; idle : temps dans STI/HLT.
 * total-idle estime l'activité hors attente, pas du temps CPU matériel :
 * l'attente inclut aussi l'IRQ de réveil ; les autres IRQ et l'instrumentation
 * ne sont pas attribuées exactement entre les sections.
 * audio : soumission DSP -> fin observée au premier plan/arrêt DSP, silence compris.
 * stop_command : détection clavier -> commande DSP acceptée, pas arrêt acoustique.
 * L'API et le coût de sb_run restent distincts de cette mesure optionnelle. */
int sb_run_measured(SB_SOURCE source, void *context, SB_STATS *stats, SB_PROFILE *profile);
#endif
