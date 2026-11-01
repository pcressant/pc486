/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC486_S3_TIME_H
#define PC486_S3_TIME_H
#define S3_PIT_HZ 1193182UL
/* Banc DOS AT/8254, période BIOS 65536, modes 2/3, pas de timer résident accéléré.
 * Ne reprogramme ni fréquence PIT ni masque PIC. Timestamp modulo ~1 heure.
 * Différences valables pour des essais courts, hors changement de jour BIOS.
 * Lire l'IRR du PIC permet de corriger un IRQ0 en attente pendant le latch.
 * Appels au premier plan, interruptions permises ; pas depuis une ISR. */
int __cdecl s3_clock_init(void);
unsigned long __cdecl s3_ticks(void);
/* Signal sticky si aucune paire cohérente en 8 essais. Un banc doit alors refuser
 * ses résultats ; aucune monotonie n'est artificiellement forcée. */
extern volatile unsigned s3_clock_fault;
#endif
