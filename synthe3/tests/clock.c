/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "s3time.h"
#include <dos.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned i;
    unsigned long before, after, previous, max_read = 0, elapsed;
    assert(s3_clock_init());
    previous = s3_ticks();
    for (i = 0; i < 20000; i++) {
        after = s3_ticks();
        if (after-previous >= S3_PIT_HZ) {
            printf("horloge incoherente : avant=%lu apres=%lu delta=%lu fault=%u\n",
                   previous, after, after-previous, s3_clock_fault);
            fflush(stdout);
        }
        assert(after-previous < S3_PIT_HZ);
        previous = after;
    }
    before = s3_ticks();
    delay(1000);
    after = s3_ticks();
    elapsed = after-before;
    assert(elapsed > S3_PIT_HZ*9/10 && elapsed < S3_PIT_HZ*11/10);
    assert(!s3_clock_fault);
    for (i = 0; i < 100; i++) {
        before = s3_ticks(); after = s3_ticks();
        if (after-before > max_read) max_read = after-before;
    }
    printf("PIT OK : delai1000=%lu, lecture_max=%lu, hz=%lu\n", elapsed, max_read, S3_PIT_HZ);
    return 0;
}
