/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "s3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static short first[40000], other[40000];
static unsigned render(short *out, unsigned block)
{
    unsigned n, total = 0;
    assert(s3_start("Bonjour Michel.", 3, 5) == S3_OK);
    assert(s3_start("autre texte", 3, 5) == S3_BUSY);
    while ((n = s3_read(out + total, block)) != 0) {
        total += n;
        assert(total < 39000);
    }
    return total;
}
int main(void)
{
    unsigned count, other_count;
    short sample;
    char too_long[242];
    s3_init();
    assert(s3_start("", 3, 5) == S3_OK);
    s3_cancel();
    assert(s3_start(NULL, 3, 5) == S3_INVALID);
    assert(s3_start("bonjour", -1, 5) == S3_INVALID);
    assert(s3_start("bonjour", 3, 16) == S3_INVALID);
    memset(too_long, 'a', 241); too_long[241] = 0;
    assert(s3_start(too_long, 3, 5) == S3_INVALID);
    assert(s3_start("\xed", 3, 5) == S3_INVALID);
    count = render(first, 512);
    other_count = render(other, 1);
    assert(count > 1000 && other_count == count);
    assert(!memcmp(first, other, count * sizeof(short)));
    assert(s3_start("bonjour", 3, 5) == S3_OK);
    assert(s3_read(&sample, 0) == 0);
    assert(s3_read(&sample, 1) == 1);
    s3_cancel();
    assert(s3_read(&sample, 1) == 0);
    assert(render(other, 127) == count);
    assert(!memcmp(first, other, count * sizeof(short)));
    puts("stream OK");
    return 0;
}
