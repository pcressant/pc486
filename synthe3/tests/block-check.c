/* SPDX-License-Identifier: GPL-3.0-or-later
 * Noyaux par période (block.c ou block.asm) contre s3_weighted et s3_output :
 * toutes les combinaisons utiles de périodes, amplitudes, sens et longueurs. */
#include "s3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern const char s3_block_impl[];
static unsigned short weights[256];
static short mix[256], expected[256], out[256], ref[256];
int main(void)
{
    char segment[256];
    unsigned source, result, rising, amp, x, i, length, checks = 0;
    long state[2] = {32768, 32768}, scalar[2] = {32768, 32768};
    for (i = 0; i < 256; i++) segment[i] = (char)((i*73+19) & 255);
    for (source = 1; source <= 255; source += 6)
        for (result = 1; result <= 255; result += 5)
            for (rising = 0; rising < 2; rising++)
                for (amp = 0; amp <= 31; amp += (amp ? 15 : 1)) {
                    length = source < result ? source : result;
                    assert(s3_weights(length, weights));
                    for (x = 0; x < result; x++) {
                        mix[x] = expected[x] = (short)(x*37 - 1000); /* accumulation */
                        expected[x] += s3_weighted((short)x, segment, source, result, amp, rising, weights);
                    }
                    s3_mix(mix, segment, source, result, amp, rising, weights);
                    assert(!memcmp(mix, expected, result*sizeof(short)));
                    checks++;
                }
    /* Cas limites : période 2 et 255, amplitude maximale, rapports extrêmes. */
    for (source = 2; source <= 255; source += 253)
        for (result = 1; result <= 255; result += 254)
            for (rising = 0; rising < 2; rising++) {
                length = source < result ? source : result;
                assert(s3_weights(length, weights));
                for (x = 0; x < result; x++) {
                    mix[x] = 0;
                    expected[x] = s3_weighted((short)x, segment, source, result, 31, rising, weights);
                }
                s3_mix(mix, segment, source, result, 31, rising, weights);
                assert(!memcmp(mix, expected, result*sizeof(short)));
                checks++;
            }
    for (i = 0; i < 256; i++) mix[i] = (short)(i*517 - 32768 + (i & 1 ? 20000 : 0));
    mix[3] = -32768; mix[4] = 32767; mix[5] = 15748; mix[6] = -15748;
    for (i = 0; i < 256; i++) ref[i] = s3_output(mix[i], scalar);
    s3_output_block(mix, out, 100, state);
    s3_output_block(mix+100, out+100, 156, state);
    assert(!memcmp(out, ref, sizeof(out)));
    assert(state[0] == scalar[0] && state[1] == scalar[1]);
    s3_output_block(mix, out, 0, state);
    assert(state[0] == scalar[0] && state[1] == scalar[1]);
    printf("block %s OK (%u)\n", s3_block_impl, checks);
    return 0;
}
