/* SPDX-License-Identifier: GPL-3.0-or-later
 * Même vecteur sous DOS (ASM et C) et sur Linux : hash des résultats scalaires. */
#include "s3.h"
#include <stdio.h>
#include <assert.h>
static unsigned long hash = 2166136261UL;
static void word(unsigned short n)
{
    hash = ((hash ^ (n & 255)) * 16777619UL) & 0xffffffffUL;
    hash = ((hash ^ (n >> 8)) * 16777619UL) & 0xffffffffUL;
}
int main(void)
{
    char segment[256];
    int p, result, x, rising, amp, i;
    long state[2] = {32768,32768};
    long scalar[2] = {32768,32768}, mixed;
    for (i = 0; i < 256; i++) segment[i] = (char)((i * 73 + 19) & 255);
    for (p = 1; p <= 255; p += 7)
        for (result = 1; result <= 255; result += 11)
            for (x = 0; x < result; x++)
                for (rising = 0; rising < 2; rising++)
                    for (amp = 0; amp <= 31; amp += (amp ? 30 : 1))
                        word((unsigned short)s3_window((short)x, segment,
                             p, result, amp, rising));
    for (i = 0; i < 20000; i++) {
        long out = s3_highpass(32768L + ((i * 97L) % 65533L) - 32766, state);
        word((unsigned short)out);
        word((unsigned short)((unsigned long)out >> 16));
    }
    printf("%08lx ", hash);
    hash = 2166136261UL;
    state[0] = state[1] = 32768;
    for (mixed = -32768; mixed <= 32767; mixed++) {
        long input = (long)(mixed * 8.0 + 32768.5);
        long out = s3_highpass(input, scalar) - 32768L;
        short actual = s3_output((short)mixed, state);
        if (out > 32766) out = 32766;
        if (out < -32766) out = -32766;
        assert(actual == out && state[0] == scalar[0] && state[1] == scalar[1]);
        word((unsigned short)actual);
    }
    printf("%08lx\n", hash);
    return 0;
}
