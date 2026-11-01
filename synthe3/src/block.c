
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Référence C portable des noyaux par période (option S3_BLOCK du moteur) ;
 * block.asm fournit les mêmes fonctions en 386 réel. Les bornes, indices et
 * arrondis suivent s3_weighted et s3_output exactement : PCM identique attendu. */
#include "s3.h"
const char s3_block_impl[] = "c";
void S3_CDECL s3_mix(short *mix, const char *segment, unsigned source_period,
    unsigned result_period, unsigned amplitude, unsigned rising, const unsigned short *weights)
{
    unsigned length, x, first, last;
    long scale, value;
    int shift = 0, at;
    if (!amplitude || !source_period || !result_period) return;
    length = result_period < source_period ? result_period : source_period;
    /* local = x + shift doit rester dans [0, length[ : plage de x contiguë. */
    if (result_period > source_period && rising) shift = (int)source_period - (int)result_period;
    first = shift < 0 ? (unsigned)-shift : 0;
    last = length - shift; /* exclu ; shift <= 0 donc pas de débordement */
    if (last > result_period) last = result_period;
    at = (int)first + (int)source_period / 4;
    if (rising) at += (int)source_period - (int)result_period;
    /* Jamais négatif quand la contribution est non nulle ; une seule réduction
     * suffit, comme dans s3_weighted (at < 5/4 source_period). */
    if (at >= (int)source_period) at -= source_period;
    for (x = first; x < last; x++) {
        long weight = weights[x + shift];
        if (!rising) weight = 32768L - weight;
        scale = (long)(signed char)segment[at] * amplitude;
        value = weight * scale;
        mix[x] += (short)(value / 32768L);
        if (++at >= (int)source_period) at -= source_period;
    }
}
void S3_CDECL s3_output_block(const short *mix, short *out, unsigned count, long *state)
{
    unsigned i;
    for (i = 0; i < count; i++) out[i] = s3_output(mix[i], state);
}
