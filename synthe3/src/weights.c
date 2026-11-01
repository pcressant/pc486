/* SPDX-License-Identifier: GPL-3.0-or-later
 * Préparation exacte de floor(x*65536/length) par quotient/reste incrémental.
 * Phase/interpolation mutualisées, aucun flottant, aucune allocation. */
#include "s3.h"
#include "window.h"

int s3_weights(unsigned length, unsigned short *weights)
{
    unsigned i, error = 0, remainder;
    unsigned long phase = 0, step;
    if (!weights || !length || length > 255) return 0;
    step = 65536UL / length;
    remainder = (unsigned)(65536UL % length);
    for (i = 0; i < length; i++) {
        unsigned index = (unsigned)(phase >> 8), fraction = (unsigned)(phase & 255);
        unsigned delta = window_q15[index+1] - window_q15[index];
        /* delta <= 202, fraction <= 255 : produit < 65536 même en C 16 bits. */
        weights[i] = window_q15[index] + ((delta * fraction) >> 8);
        phase += step;
        error += remainder;
        if (error >= length) { phase++; error -= length; }
    }
    return 1;
}

#ifndef S3_ASM_WEIGHTED
short S3_CDECL s3_weighted(short x, const char *segment, unsigned source_period,
    unsigned result_period, unsigned amplitude, unsigned rising, const unsigned short *weights)
{
    unsigned length;
    int local = x, at;
    long weight, value;
    if (!amplitude || !source_period || !result_period) return 0;
    at = x + (int)source_period / 4;
    if (rising) at += (int)source_period - (int)result_period;
    if (at >= (int)source_period) at -= source_period;
    length = result_period < source_period ? result_period : source_period;
    if (result_period > source_period && rising) local += (int)source_period - (int)result_period;
    if (local < 0 || local >= (int)length) return 0;
    weight = weights[local];
    if (!rising) weight = 32768L-weight;
    value = weight * (signed char)segment[at] * amplitude;
    return (short)(value / 32768L);
}
#endif
