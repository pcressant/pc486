/* SPDX-License-Identifier: GPL-3.0-or-later
 * Référence portable des noyaux entiers de kernels.asm.
 * Les tests DOS comparent les deux implémentations bit pour bit. */
#include "s3.h"
#include "window.h"
#include <stdint.h>

short S3_CDECL s3_window(short x, const char *segment, unsigned source_period,
                         unsigned result_period, unsigned amplitude, unsigned rising)
{
    unsigned length, phase, index, fraction;
    long weight, value;
    int local = x, at;
    if (!amplitude || !source_period || !result_period) return 0;
    at = x + source_period / 4;
    if (rising) at += source_period - result_period;
    if (at >= (int)source_period) at -= source_period;
    length = result_period < source_period ? result_period : source_period;
    if (result_period > source_period && rising) local += source_period - result_period;
    if (local < 0 || local >= (int)length) return 0;
    phase = (unsigned)(((unsigned long)local << 16) / length);
    index = phase >> 8;
    fraction = phase & 255;
    weight = window_q15[index];
    weight += ((long)(window_q15[index+1] - window_q15[index]) * fraction) / 256;
    if (!rising) weight = 32768L - weight;
    value = weight * (signed char)segment[at] * amplitude;
    return (short)(value / 32768L);
}

long S3_CDECL s3_highpass(long input, long *state)
{
    long out = state[1] + input - state[0] -
               (long)(((int64_t)state[1] - 32768L) * 16471L / 65536L);
    state[0] = input;
    state[1] = out;
    return out;
}

short S3_CDECL s3_output(short mixed, long *state)
{
    long input = (long)mixed * 8 + 32768L;
    long out;
    /* Équivalent exact du cast historique de (mixed * 8 + 32768.5). */
    if (input < 0) input++;
    out = s3_highpass(input, state) - 32768L;
    if (out > 32766) return 32766;
    if (out < -32766) return -32766;
    return (short)out;
}

