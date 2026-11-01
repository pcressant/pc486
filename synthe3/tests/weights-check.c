/* SPDX-License-Identifier: GPL-3.0-or-later
 * Vérifie la préparation incrémentale contre la formule exacte et le noyau
 * scalaire, y compris sources plus courtes/longues et produits signés. */
#include "s3.h"
#include "window.h"
#include <assert.h>
#include <stdio.h>
static unsigned short weights[256];
int main(void)
{
    char segment[256];
    unsigned length, x, p, result, rising, amp;
    unsigned i;
    for (i = 0; i < 256; i++) segment[i] = (char)((i*73+19) & 255);
    assert(!s3_weights(0, weights) && !s3_weights(256, weights) && !s3_weights(1, NULL));
    for (length = 1; length <= 255; length++) {
        assert(s3_weights(length, weights));
        for (x = 0; x < length; x++) {
            unsigned long phase = ((unsigned long)x << 16) / length;
            unsigned index = (unsigned)(phase >> 8), fraction = (unsigned)(phase & 255);
            unsigned expected = window_q15[index] +
                (unsigned)(((unsigned long)(window_q15[index+1]-window_q15[index])*fraction) >> 8);
            assert(weights[x] == expected);
        }
    }
    for (p = 1; p <= 255; p += 7)
        for (result = 1; result <= 255; result += 11) {
            assert(s3_weights(p < result ? p : result, weights));
            for (x = 0; x < result; x++)
                for (rising = 0; rising < 2; rising++)
                    for (amp = 0; amp <= 31; amp += (amp ? 30 : 1))
                        assert(s3_window((short)x, segment, p, result, amp, rising) ==
                            s3_weighted((short)x, segment, p, result, amp, rising, weights));
        }
    puts("weights OK");
    return 0;
}
